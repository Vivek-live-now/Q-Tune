#include "wifi_streamer.h"
#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "led_manager.h"
#include "display.h"
#include "button_manager.h"
#include "battery.h"
#include "power_manager.h"
#include <esp_heap_caps.h>

WiFiStreamer wifiStreamer;

static const char* UPNP_DEVICE_XML =
    "<?xml version=\"1.0\"?>\r\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <device>\r\n"
    "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n"
    "    <friendlyName>Q-Tune Audiophile Player</friendlyName>\r\n"
    "    <manufacturer>Q-Tune</manufacturer>\r\n"
    "    <modelName>SuperMini S3</modelName>\r\n"
    "    <modelNumber>Q-TUNE-1.0</modelNumber>\r\n"
    "    <UDN>uuid:q-tune-audiophile-player</UDN>\r\n"
    "    <serviceList>\r\n"
    "      <service>\r\n"
    "        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>\r\n"
    "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\r\n"
    "        <SCPDURL>/RenderingControl/desc.xml</SCPDURL>\r\n"
    "        <controlURL>/RenderingControl/control</controlURL>\r\n"
    "        <eventSubURL>/RenderingControl/event</eventSubURL>\r\n"
    "      </service>\r\n"
    "      <service>\r\n"
    "        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\r\n"
    "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\r\n"
    "        <SCPDURL>/AVTransport/desc.xml</SCPDURL>\r\n"
    "        <controlURL>/AVTransport/control</controlURL>\r\n"
    "        <eventSubURL>/AVTransport/event</eventSubURL>\r\n"
    "      </service>\r\n"
    "      <service>\r\n"
    "        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>\r\n"
    "        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\r\n"
    "        <SCPDURL>/ConnectionManager/desc.xml</SCPDURL>\r\n"
    "        <controlURL>/ConnectionManager/control</controlURL>\r\n"
    "        <eventSubURL>/ConnectionManager/event</eventSubURL>\r\n"
    "      </service>\r\n"
    "    </serviceList>\r\n"
    "  </device>\r\n"
    "</root>\r\n";

static const char* UPNP_SCPD_XML =
    "<?xml version=\"1.0\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList></actionList>\r\n"
    "  <serviceStateTable></serviceStateTable>\r\n"
    "</scpd>\r\n";

WiFiStreamer::WiFiStreamer()
    : wifiState(WIFI_STATE_OFF),
      streamState(STREAM_IDLE),
      currentTrackTitle("Ready for Stream"),
      currentTrackArtist("AirMusic / Poweramp"),
      currentSampleRate(44100),
      currentBitsPerSample(16),
      configServer(80),
      upnpServer(8080),
      streamRingBuffer(NULL),
      ringBufferSize(384 * 1024), // 384 KB (~2.2s of 44.1kHz 16-bit stereo)
      ringHead(0),
      ringTail(0),
      ringCount(0),
      ringMutex(NULL),
      streamTaskHandle(NULL),
      streamTaskRunning(false),
      hasActiveStream(false) {
}

bool WiFiStreamer::begin() {
    prefs.begin("qtune_wifi", false);
    savedSSID = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");

    if (ringMutex == NULL) {
        ringMutex = xSemaphoreCreateMutex();
    }

    // Allocate ring buffer in PSRAM (fallback to internal SRAM if PSRAM unavailable)
    if (streamRingBuffer == NULL) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        streamRingBuffer = (uint8_t*)heap_caps_malloc(ringBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        if (streamRingBuffer == NULL) {
            // Fallback to internal heap with reduced buffer size
            ringBufferSize = 32 * 1024; // 32 KB
            streamRingBuffer = (uint8_t*)malloc(ringBufferSize);
        }
    }

    return (streamRingBuffer != NULL);
}

bool WiFiStreamer::start() {
    begin();

    // Ensure SD player is paused or stopped
    audioPlayer.prepareForStream(44100);

    // Attempt to connect to saved Wi-Fi credentials
    if (savedSSID.length() > 0) {
        wifiState = WIFI_STATE_CONNECTING;
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());

        uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 8000)) {
            delay(100);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiState = WIFI_STATE_CONNECTED;
    } else {
        startAPMode();
    }

    setupConfigServer();
    setupUpnpServer();

    // SSDP multicast listener on UDP port 1900
    ssdpUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
    sendSSDPAnnounce();

    // Launch FreeRTOS stream background task
    if (streamTaskHandle == NULL) {
        streamTaskRunning = true;
        xTaskCreatePinnedToCore(
            streamTaskFunction,
            "QWiFiStreamTask",
            8192,
            this,
            4,
            &streamTaskHandle,
            0 // Core 0 for background network streaming
        );
    }

    return true;
}

void WiFiStreamer::stop() {
    stopStream();
    streamTaskRunning = false;
    if (streamTaskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(50));
        streamTaskHandle = NULL;
    }

    configServer.stop();
    upnpServer.stop();
    ssdpUdp.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiState = WIFI_STATE_OFF;
}

void WiFiStreamer::startAPMode() {
    wifiState = WIFI_STATE_AP_MODE;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Q-Tune-Audio", "12345678");
}

void WiFiStreamer::reconnect() {
    stop();
    start();
}

void WiFiStreamer::setCredentials(const String &ssid, const String &password) {
    savedSSID = ssid;
    savedPass = password;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
}

void WiFiStreamer::pauseStream() {
    if (streamState == STREAM_PLAYING) {
        streamState = STREAM_PAUSED;
    }
}

void WiFiStreamer::resumeStream() {
    if (streamState == STREAM_PAUSED) {
        streamState = STREAM_PLAYING;
    }
}

void WiFiStreamer::stopStream() {
    hasActiveStream = false;
    streamState = STREAM_IDLE;
    if (activeStreamClient.connected()) {
        activeStreamClient.stop();
    }
    if (ringMutex != NULL) xSemaphoreTake(ringMutex, portMAX_DELAY);
    ringHead = 0;
    ringTail = 0;
    ringCount = 0;
    if (ringMutex != NULL) xSemaphoreGive(ringMutex);
}

bool WiFiStreamer::pushToRingBuffer(const uint8_t *data, size_t length) {
    if (streamRingBuffer == NULL || data == NULL || length == 0) return false;
    if (ringMutex != NULL) xSemaphoreTake(ringMutex, portMAX_DELAY);

    size_t space = ringBufferSize - ringCount;
    if (length > space) {
        length = space; // Drop overflow frames gracefully
    }

    for (size_t i = 0; i < length; i++) {
        streamRingBuffer[ringHead] = data[i];
        ringHead = (ringHead + 1) % ringBufferSize;
    }
    ringCount += length;

    if (ringMutex != NULL) xSemaphoreGive(ringMutex);
    return (length > 0);
}

size_t WiFiStreamer::pullFromRingBuffer(uint8_t *dest, size_t maxLen) {
    if (streamRingBuffer == NULL || dest == NULL || maxLen == 0) return 0;
    if (ringMutex != NULL) xSemaphoreTake(ringMutex, portMAX_DELAY);

    size_t available = ringCount;
    size_t toRead = (maxLen < available) ? maxLen : available;

    for (size_t i = 0; i < toRead; i++) {
        dest[i] = streamRingBuffer[ringTail];
        ringTail = (ringTail + 1) % ringBufferSize;
    }
    ringCount -= toRead;

    if (ringMutex != NULL) xSemaphoreGive(ringMutex);
    return toRead;
}

void WiFiStreamer::sendSSDPAnnounce() {
    IPAddress localIp = (wifiState == WIFI_STATE_AP_MODE) ? WiFi.softAPIP() : WiFi.localIP();
    char notifyPacket[512];
    snprintf(notifyPacket, sizeof(notifyPacket),
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "CACHE-CONTROL: max-age=1800\r\n"
        "LOCATION: http://%s:8080/description.xml\r\n"
        "NT: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "NTS: ssdp:alive\r\n"
        "SERVER: POSIX/1.0 UPnP/1.0 Q-Tune/1.0\r\n"
        "USN: uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n",
        localIp.toString().c_str()
    );

    ssdpUdp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    ssdpUdp.write((const uint8_t*)notifyPacket, strlen(notifyPacket));
    ssdpUdp.endPacket();
}

void WiFiStreamer::handleSSDP() {
    int packetSize = ssdpUdp.parsePacket();
    if (packetSize <= 0) return;

    char buffer[512];
    int len = ssdpUdp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        if (strstr(buffer, "M-SEARCH") != NULL &&
            (strstr(buffer, "MediaRenderer") != NULL ||
             strstr(buffer, "ssdp:all") != NULL ||
             strstr(buffer, "upnp:rootdevice") != NULL)) {

            IPAddress localIp = (wifiState == WIFI_STATE_AP_MODE) ? WiFi.softAPIP() : WiFi.localIP();
            char response[512];
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "CACHE-CONTROL: max-age=1800\r\n"
                "EXT:\r\n"
                "LOCATION: http://%s:8080/description.xml\r\n"
                "SERVER: POSIX/1.0 UPnP/1.0 Q-Tune/1.0\r\n"
                "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                "USN: uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                "BOOTID.UPNP.ORG: 1\r\n\r\n",
                localIp.toString().c_str()
            );

            ssdpUdp.beginPacket(ssdpUdp.remoteIP(), ssdpUdp.remotePort());
            ssdpUdp.write((const uint8_t*)response, strlen(response));
            ssdpUdp.endPacket();
        }
    }
}

void WiFiStreamer::setupConfigServer() {
    configServer.on("/", HTTP_GET, [this]() {
        IPAddress ip = (wifiState == WIFI_STATE_AP_MODE) ? WiFi.softAPIP() : WiFi.localIP();
        String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>Q-Tune Audiophile Portal</title>"
                      "<style>body{font-family:sans-serif;background:#121212;color:#eee;padding:20px;text-align:center;}"
                      ".card{background:#1e1e1e;border-radius:10px;padding:20px;margin:15px auto;max-width:380px;box-shadow:0 4px 10px rgba(0,0,0,0.5);}"
                      "h2{color:#00e5ff;}input[type=text],input[type=password]{width:90%;padding:10px;margin:8px 0;border-radius:5px;border:none;background:#2a2a2a;color:#fff;}"
                      "button{background:#00e5ff;color:#000;border:none;padding:12px 20px;font-size:16px;border-radius:5px;cursor:pointer;font-weight:bold;margin:5px;}"
                      ".badge{display:inline-block;padding:4px 10px;border-radius:12px;background:#333;font-size:13px;margin:4px;}"
                      "</style></head><body>"
                      "<div class='card'>"
                      "<h2>Q-TUNE AUDIOPHILE</h2>"
                      "<p><b>Status:</b> <span class='badge'>" + (wifiState == WIFI_STATE_CONNECTED ? "Connected: " + savedSSID : "SoftAP Mode") + "</span></p>"
                      "<p><b>IP Address:</b> " + ip.toString() + "</p>"
                      "<p><b>Output Mode:</b> <span class='badge' style='background:#00e5ff;color:#000;'>" + String(audioPlayer.getOutputModeName()) + "</span></p>"
                      "<p><b>Now Streaming:</b><br><i>" + currentTrackTitle + "</i><br><small>" + currentTrackArtist + "</small></p>"
                      "<form method='POST' action='/toggle_output'><button type='submit'>Switch Output (SPKR/KA11)</button></form>"
                      "</div>"
                      "<div class='card'>"
                      "<h3>Wi-Fi Setup</h3>"
                      "<form method='POST' action='/save'>"
                      "<input type='text' name='ssid' placeholder='Wi-Fi SSID' value='" + savedSSID + "' required><br>"
                      "<input type='password' name='pass' placeholder='Password'><br>"
                      "<button type='submit'>Save & Connect</button>"
                      "</form></div></body></html>";
        configServer.send(200, "text/html", html);
    });

    configServer.on("/save", HTTP_POST, [this]() {
        if (configServer.hasArg("ssid")) {
            setCredentials(configServer.arg("ssid"), configServer.arg("pass"));
            configServer.send(200, "text/html", "<h3>Settings Saved! Connecting...</h3><script>setTimeout(function(){location.href='/';},4000);</script>");
            delay(500);
            reconnect();
        } else {
            configServer.send(400, "text/plain", "Missing SSID");
        }
    });

    configServer.on("/toggle_output", HTTP_POST, [this]() {
        AudioOutputMode cur = audioPlayer.getOutputMode();
        audioPlayer.setOutputMode((cur == OUTPUT_MODE_SPEAKER_I2S) ? OUTPUT_MODE_FIIO_USB_DAC : OUTPUT_MODE_SPEAKER_I2S);
        configServer.sendHeader("Location", "/");
        configServer.send(303);
    });

    configServer.begin();
}

void WiFiStreamer::setupUpnpServer() {
    upnpServer.on("/description.xml", HTTP_GET, [this]() {
        upnpServer.send(200, "text/xml", UPNP_DEVICE_XML);
    });

    upnpServer.on("/RenderingControl/desc.xml", HTTP_GET, [this]() {
        upnpServer.send(200, "text/xml", UPNP_SCPD_XML);
    });
    upnpServer.on("/AVTransport/desc.xml", HTTP_GET, [this]() {
        upnpServer.send(200, "text/xml", UPNP_SCPD_XML);
    });
    upnpServer.on("/ConnectionManager/desc.xml", HTTP_GET, [this]() {
        upnpServer.send(200, "text/xml", UPNP_SCPD_XML);
    });

    // AVTransport Control Endpoint (SetAVTransportURI, Play, Pause, Stop)
    upnpServer.on("/AVTransport/control", HTTP_POST, [this]() {
        String body = upnpServer.arg("plain");

        if (body.indexOf("SetAVTransportURI") != -1) {
            // Extract URI
            int uriStart = body.indexOf("<CurrentURI>");
            int uriEnd = body.indexOf("</CurrentURI>");
            if (uriStart != -1 && uriEnd != -1) {
                activeStreamUrl = body.substring(uriStart + 12, uriEnd);
            }

            // Extract metadata (Title & Artist)
            int titleStart = body.indexOf("&lt;dc:title&gt;");
            int titleEnd = body.indexOf("&lt;/dc:title&gt;");
            if (titleStart != -1 && titleEnd != -1) {
                currentTrackTitle = body.substring(titleStart + 16, titleEnd);
            } else {
                currentTrackTitle = "AirMusic Stream";
            }

            int artistStart = body.indexOf("&lt;upnp:artist&gt;");
            int artistEnd = body.indexOf("&lt;/upnp:artist&gt;");
            if (artistStart != -1 && artistEnd != -1) {
                currentTrackArtist = body.substring(artistStart + 19, artistEnd);
            } else {
                currentTrackArtist = "Poweramp";
            }

            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        } else if (body.indexOf("Play") != -1) {
            hasActiveStream = true;
            streamState = STREAM_BUFFERING;
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:PlayResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        } else if (body.indexOf("Pause") != -1) {
            pauseStream();
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:PauseResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        } else if (body.indexOf("Stop") != -1) {
            stopStream();
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:StopResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        } else {
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetTransportInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                              "    <CurrentTransportState>" + String(streamState == STREAM_PLAYING ? "PLAYING" : (streamState == STREAM_PAUSED ? "PAUSED_PLAYBACK" : "STOPPED")) + "</CurrentTransportState>"
                              "  </u:GetTransportInfoResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        }
    });

    // RenderingControl Control Endpoint (Volume control)
    upnpServer.on("/RenderingControl/control", HTTP_POST, [this]() {
        String body = upnpServer.arg("plain");
        if (body.indexOf("SetVolume") != -1) {
            int volStart = body.indexOf("<DesiredVolume>");
            int volEnd = body.indexOf("</DesiredVolume>");
            if (volStart != -1 && volEnd != -1) {
                int vol = body.substring(volStart + 15, volEnd).toInt();
                audioPlayer.setVolume((uint8_t)constrain(vol, 0, 100));
            }
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:SetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        } else {
            String soapResp = "<?xml version=\"1.0\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                              "    <CurrentVolume>" + String(audioPlayer.getVolume()) + "</CurrentVolume>"
                              "  </u:GetVolumeResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml", soapResp);
        }
    });

    upnpServer.begin();
}

void WiFiStreamer::streamTaskFunction(void *param) {
    WiFiStreamer *streamer = (WiFiStreamer*)param;
    uint8_t readChunk[1024];

    while (streamer->streamTaskRunning) {
        if (streamer->hasActiveStream && streamer->streamState != STREAM_PAUSED) {
            // Check socket connection
            if (!streamer->activeStreamClient.connected()) {
                if (streamer->activeStreamUrl.length() > 0) {
                    // Parse URL e.g. http://192.168.1.50:5000/stream.wav
                    String url = streamer->activeStreamUrl;
                    if (url.startsWith("http://")) url = url.substring(7);
                    int slashIdx = url.indexOf('/');
                    String hostPort = (slashIdx != -1) ? url.substring(0, slashIdx) : url;
                    String path = (slashIdx != -1) ? url.substring(slashIdx) : "/";

                    int colonIdx = hostPort.indexOf(':');
                    String host = (colonIdx != -1) ? hostPort.substring(0, colonIdx) : hostPort;
                    uint16_t port = (colonIdx != -1) ? hostPort.substring(colonIdx + 1).toInt() : 80;

                    if (streamer->activeStreamClient.connect(host.c_str(), port)) {
                        streamer->activeStreamClient.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str());

                        // Read headers
                        while (streamer->activeStreamClient.connected()) {
                            String line = streamer->activeStreamClient.readStringUntil('\n');
                            if (line == "\r" || line.length() == 0) break;
                        }
                        streamer->streamState = STREAM_BUFFERING;
                    }
                }
            }

            // Ingest incoming data into PSRAM ring buffer
            if (streamer->activeStreamClient.connected() && streamer->activeStreamClient.available() > 0) {
                int avail = streamer->activeStreamClient.available();
                int toRead = (avail > (int)sizeof(readChunk)) ? sizeof(readChunk) : avail;
                int bytesRead = streamer->activeStreamClient.read(readChunk, toRead);
                if (bytesRead > 0) {
                    streamer->pushToRingBuffer(readChunk, bytesRead);
                }
            }

            // Buffer state transition
            if (streamer->streamState == STREAM_BUFFERING) {
                // Pre-buffer at least 32 KB (~180ms) before playing
                if (streamer->ringCount >= 32768) {
                    streamer->streamState = STREAM_PLAYING;
                }
            }

            // Audio output consumption
            if (streamer->streamState == STREAM_PLAYING) {
                uint8_t playBuffer[512];
                size_t pulled = streamer->pullFromRingBuffer(playBuffer, sizeof(playBuffer));
                if (pulled >= 4) {
                    // Align to stereo 16-bit frame boundary (4 bytes)
                    pulled &= ~3;
                    size_t frameCount = pulled / 4;
                    audioPlayer.playStreamChunk((const int16_t*)playBuffer, frameCount);
                } else if (pulled == 0 && streamer->ringCount < 4) {
                    // Underflow: brief re-buffer
                    if (streamer->activeStreamClient.connected()) {
                        streamer->streamState = STREAM_BUFFERING;
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    vTaskDelete(NULL);
}

void WiFiStreamer::update() {
    configServer.handleClient();
    upnpServer.handleClient();
    handleSSDP();
}

WiFiStreamState WiFiStreamer::getWiFiState() const { return wifiState; }
AudioStreamState WiFiStreamer::getStreamState() const { return streamState; }
String WiFiStreamer::getSSID() const { return savedSSID; }
int8_t WiFiStreamer::getRSSI() const { return (wifiState == WIFI_STATE_CONNECTED) ? WiFi.RSSI() : 0; }
String WiFiStreamer::getTrackTitle() const { return currentTrackTitle; }
String WiFiStreamer::getTrackArtist() const { return currentTrackArtist; }
uint32_t WiFiStreamer::getSampleRate() const { return currentSampleRate; }
uint8_t WiFiStreamer::getBitsPerSample() const { return currentBitsPerSample; }
bool WiFiStreamer::isStreaming() const { return (streamState == STREAM_PLAYING || streamState == STREAM_BUFFERING); }

uint8_t WiFiStreamer::getBufferFillPercentage() const {
    if (ringBufferSize == 0) return 0;
    return (uint8_t)((ringCount * 100ULL) / ringBufferSize);
}

String WiFiStreamer::getIPAddress() const {
    if (wifiState == WIFI_STATE_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (wifiState == WIFI_STATE_AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

void WiFiStreamer::renderUI() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    // Top status bar with output badge
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "WIFI [%s]", audioPlayer.getOutputModeShortName());
    display.drawTopStatusBar(titleBuf, battery.getPercentage());

    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Network line
    char netBuf[32];
    if (wifiState == WIFI_STATE_CONNECTED) {
        snprintf(netBuf, sizeof(netBuf), "IP: %s", getIPAddress().c_str());
    } else if (wifiState == WIFI_STATE_AP_MODE) {
        snprintf(netBuf, sizeof(netBuf), "AP: Q-Tune (%s)", getIPAddress().c_str());
    } else {
        snprintf(netBuf, sizeof(netBuf), "Wi-Fi: Connecting...");
    }
    u8g2.drawStr(2, 23, netBuf);

    // Stream status line
    char statusBuf[32];
    const char *stateStr = (streamState == STREAM_PLAYING) ? "STREAMING" :
                          (streamState == STREAM_BUFFERING) ? "BUFFERING" :
                          (streamState == STREAM_PAUSED) ? "PAUSED" : "IDLE";
    snprintf(statusBuf, sizeof(statusBuf), "[%s] Buf:%d%% V:%d%%", stateStr, getBufferFillPercentage(), audioPlayer.getVolume());
    u8g2.drawStr(2, 34, statusBuf);

    // Now Playing Track / Artist line
    String dispTitle = currentTrackTitle;
    if (dispTitle.length() > 20) dispTitle = dispTitle.substring(0, 19) + ".";
    u8g2.drawStr(2, 45, dispTitle.c_str());

    String dispArtist = currentTrackArtist;
    if (dispArtist.length() > 20) dispArtist = dispArtist.substring(0, 19) + ".";
    u8g2.drawStr(2, 54, dispArtist.c_str());

    // Mini spectrum visualizer bar at the bottom
    spectrumAnalyzer.renderMiniHUD(2, 57, 124, 7);

    display.sendBuffer();
}

bool WiFiStreamer::updateUI() {
    update();
    renderUI();

    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        ledManager.triggerButtonPulse(CRGB::Red, 1, 60);
        return false; // Exit back to main menu
    } else if (evt == BTN_EVENT_UP_PRESS || evt == BTN_EVENT_UP_HOLD) {
        audioPlayer.volumeUp(5);
        ledManager.triggerButtonPulse(CRGB::Green, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS || evt == BTN_EVENT_DN_HOLD) {
        audioPlayer.volumeDown(5);
        ledManager.triggerButtonPulse(CRGB::Red, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        if (streamState == STREAM_PLAYING) {
            pauseStream();
            ledManager.onPlaybackPause();
        } else if (streamState == STREAM_PAUSED) {
            resumeStream();
            ledManager.onPlaybackResume();
        }
    } else if (evt == BTN_EVENT_SEL_HOLD) {
        // Toggle Audio Output between I2S Speaker and FiiO KA11 USB DAC
        AudioOutputMode cur = audioPlayer.getOutputMode();
        AudioOutputMode nextMode = (cur == OUTPUT_MODE_SPEAKER_I2S) ? OUTPUT_MODE_FIIO_USB_DAC : OUTPUT_MODE_SPEAKER_I2S;
        audioPlayer.setOutputMode(nextMode);
        ledManager.triggerButtonPulse(CRGB::Magenta, 2, 80);
    }

    return true;
}
