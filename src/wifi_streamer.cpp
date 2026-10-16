#include "wifi_streamer.h"
#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "led_manager.h"
#include "display.h"
#include "button_manager.h"
#include "battery.h"
#include "power_manager.h"
#include "sd_manager.h"
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

bool WiFiStreamer::loadSDConfig() {
    if (!sdManager.isMounted()) return false;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    bool fileFound = false;
    File f;
    if (SD.exists("/wifi.txt")) {
        f = SD.open("/wifi.txt", FILE_READ);
        fileFound = true;
    } else if (SD.exists("/wificonfig.txt")) {
        f = SD.open("/wificonfig.txt", FILE_READ);
        fileFound = true;
    } else if (SD.exists("/WIFI.TXT")) {
        f = SD.open("/WIFI.TXT", FILE_READ);
        fileFound = true;
    }

    if (!fileFound || !f) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    String foundSSID = "";
    String foundPass = "";

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("#") || line.startsWith("//") || line.length() == 0) continue;

        int eqIdx = line.indexOf('=');
        if (eqIdx == -1) eqIdx = line.indexOf(':');
        if (eqIdx != -1) {
            String key = line.substring(0, eqIdx);
            key.trim();
            key.toLowerCase();
            String val = line.substring(eqIdx + 1);
            val.trim();
            if ((val.startsWith("\"") && val.endsWith("\"")) ||
                (val.startsWith("'") && val.endsWith("'"))) {
                if (val.length() >= 2) {
                    val = val.substring(1, val.length() - 1);
                }
            }
            if (key == "ssid" || key == "wifi_ssid" || key == "wifi_name") {
                foundSSID = val;
            } else if (key == "pass" || key == "password" || key == "pwd" || key == "wifi_pass" || key == "wifi_password") {
                foundPass = val;
            }
        }
    }
    f.close();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (foundSSID.length() > 0) {
        if (foundSSID != savedSSID || foundPass != savedPass) {
            setCredentials(foundSSID, foundPass);
        }
        return true;
    }
    return false;
}

bool WiFiStreamer::begin() {
    prefs.begin("qtune_wifi", false);
    savedSSID = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");

    // Load credentials from microSD card if wifi.txt exists
    loadSDConfig();

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
    loadSDConfig();

    // Ensure SD player is paused or stopped
    audioPlayer.prepareForStream(44100);

    // Wi-Fi RF & power stabilization
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);                 // Disable modem sleep to prevent latency jitter and dropped frames
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximize TX power for reliable 2.4GHz signal

    // Always host SoftAP for seamless captive portal setup
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Q-Tune-Audio", "12345678");

    // Attempt to connect to saved Wi-Fi credentials
    if (savedSSID.length() > 0) {
        wifiState = WIFI_STATE_CONNECTING;
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());

        // Responsive non-blocking check: wait up to 3s for fast router association
        uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 3000)) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiState = WIFI_STATE_CONNECTED;
        } else {
            // Background connection continues in update() without blocking the UI
            wifiState = WIFI_STATE_CONNECTING;
        }
    } else {
        startAPMode();
    }

    setupConfigServer();
    setupUpnpServer();

    // Start Captive Portal DNS server (port 53)
    dnsServer.start(53, "*", WiFi.softAPIP());

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

    dnsServer.stop();
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
    if (savedSSID.length() > 0) {
        wifiState = WIFI_STATE_CONNECTING;
        WiFi.disconnect(false);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("Q-Tune-Audio", "12345678");
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    } else {
        startAPMode();
    }
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

static String htmlEscape(const String &input) {
    String escaped = "";
    escaped.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        switch (c) {
            case '&':  escaped += "&amp;";  break;
            case '<':  escaped += "&lt;";   break;
            case '>':  escaped += "&gt;";   break;
            case '"':  escaped += "&quot;"; break;
            case '\'': escaped += "&#39;";  break;
            default:   escaped += c;        break;
        }
    }
    return escaped;
}

void WiFiStreamer::setupConfigServer() {
    configServer.on("/", HTTP_GET, [this]() {
        IPAddress ip = (wifiState == WIFI_STATE_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
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
                      "<p><b>Status:</b> <span class='badge'>" + (wifiState == WIFI_STATE_CONNECTED ? "Connected: " + htmlEscape(savedSSID) : (wifiState == WIFI_STATE_CONNECTING ? "Connecting to: " + htmlEscape(savedSSID) : "SoftAP Mode")) + "</span></p>"
                      "<p><b>IP Address:</b> " + ip.toString() + "</p>"
                      "<p><b>Output Mode:</b> <span class='badge' style='background:#00e5ff;color:#000;'>" + String(audioPlayer.getOutputModeName()) + "</span></p>"
                      "<p><b>Now Streaming:</b><br><i>" + htmlEscape(currentTrackTitle) + "</i><br><small>" + htmlEscape(currentTrackArtist) + "</small></p>"
                      "<form method='POST' action='/toggle_output'><button type='submit'>Switch Output (SPKR/KA11)</button></form>"
                      "</div>"
                      "<div class='card'>"
                      "<h3>Wi-Fi Setup</h3>"
                      "<form method='POST' action='/save'>"
                      "<input type='text' name='ssid' placeholder='Wi-Fi SSID' value='" + htmlEscape(savedSSID) + "' required><br>"
                      "<input type='password' name='pass' placeholder='Password'><br>"
                      "<button type='submit'>Save & Connect</button>"
                      "</form></div></body></html>";
        configServer.send(200, "text/html", html);
    });

    configServer.on("/save", HTTP_POST, [this]() {
        if (configServer.hasArg("ssid")) {
            setCredentials(configServer.arg("ssid"), configServer.arg("pass"));
            String resp = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                          "<meta http-equiv='refresh' content='4;url=/'>"
                          "<style>body{font-family:sans-serif;background:#121212;color:#eee;text-align:center;padding:30px;}"
                          ".card{background:#1e1e1e;border-radius:10px;padding:20px;margin:auto;max-width:360px;}"
                          "h3{color:#00e5ff;}</style></head><body>"
                          "<div class='card'><h3>Settings Saved!</h3>"
                          "<p>Connecting to <b>" + htmlEscape(savedSSID) + "</b>...</p>"
                          "<p>Redirecting in 4s...</p></div></body></html>";
            configServer.send(200, "text/html", resp);
            delay(100);
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

    // Captive portal assistant endpoints for mobile OS detection
    auto sendCaptiveRedirect = [this]() {
        configServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        configServer.send(302, "text/plain", "");
    };

    configServer.on("/generate_204", sendCaptiveRedirect);
    configServer.on("/hotspot-detect.html", sendCaptiveRedirect);
    configServer.on("/canonical.html", sendCaptiveRedirect);
    configServer.on("/connecttest.txt", sendCaptiveRedirect);
    configServer.on("/redirect", sendCaptiveRedirect);
    configServer.on("/success.txt", [this]() {
        configServer.send(200, "text/plain", "success\n");
    });
    configServer.on("/ncsi.txt", [this]() {
        configServer.send(200, "text/plain", "Microsoft NCSI");
    });

    configServer.onNotFound([this, sendCaptiveRedirect]() {
        if (wifiState == WIFI_STATE_AP_MODE || (WiFi.getMode() & WIFI_AP)) {
            sendCaptiveRedirect();
        } else {
            configServer.send(404, "text/plain", "Not Found");
        }
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

    if (wifiState == WIFI_STATE_AP_MODE || (WiFi.getMode() & WIFI_AP)) {
        dnsServer.processNextRequest();
    }

    // Dynamic Wi-Fi connection monitoring & auto-reconnect
    static uint32_t lastCheck = 0;
    uint32_t now = millis();
    if (now - lastCheck >= 500) {
        lastCheck = now;
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            if (wifiState != WIFI_STATE_CONNECTED) {
                wifiState = WIFI_STATE_CONNECTED;
                // Re-announce on SSDP with station IP address
                sendSSDPAnnounce();
            }
        } else {
            if (savedSSID.length() > 0) {
                if (wifiState == WIFI_STATE_CONNECTED) {
                    wifiState = WIFI_STATE_CONNECTING;
                }
                // Periodic reconnect attempt every 10 seconds if still disconnected
                static uint32_t lastReconnectAttempt = 0;
                if (wifiState == WIFI_STATE_CONNECTING && (now - lastReconnectAttempt > 10000)) {
                    lastReconnectAttempt = now;
                    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
                }
            } else {
                wifiState = WIFI_STATE_AP_MODE;
            }
        }
    }
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
    } else if (wifiState == WIFI_STATE_AP_MODE || wifiState == WIFI_STATE_CONNECTING) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

void WiFiStreamer::renderUI() {
    display.clear();

    // Top status bar with output badge
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "WIFI [%s]", audioPlayer.getOutputModeShortName());
    display.drawTopStatusBar(titleBuf, battery.getPercentage());

    U8G2 &u8g2 = display.getU8g2();

    if (wifiState == WIFI_STATE_AP_MODE) {
        // SoftAP configuration portal screen (crisp 5x7 font, perfectly fitting 128x64)
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(2, 20, "AP: Q-Tune-Audio");
        u8g2.drawStr(2, 29, "Pass: 12345678");
        u8g2.drawStr(2, 38, "Web: http://192.168.4.1");
        u8g2.drawStr(2, 47, "SD: wifi.txt (auto)");

        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(2, 55, "SEL:Retry SD  CANCEL:Exit");
    } else if (wifiState == WIFI_STATE_CONNECTING) {
        // Connecting in progress screen
        u8g2.setFont(u8g2_font_5x7_tr);
        static uint8_t animTick = 0;
        animTick = (animTick + 1) % 4;
        char conStr[32] = "Connecting";
        for (uint8_t d = 0; d < animTick; d++) strcat(conStr, ".");
        u8g2.drawStr(2, 20, conStr);

        char ssidBuf[32];
        snprintf(ssidBuf, sizeof(ssidBuf), "SSID: %s", savedSSID.c_str());
        if (strlen(ssidBuf) > 23) {
            ssidBuf[20] = '.';
            ssidBuf[21] = '.';
            ssidBuf[22] = '\0';
        }
        u8g2.drawStr(2, 30, ssidBuf);

        u8g2.drawStr(2, 40, "AP: 192.168.4.1 (Active)");

        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(2, 53, "SEL:Retry SD  CANCEL:Exit");
    } else {
        // Connected mode (Streaming HUD)
        u8g2.setFont(u8g2_font_5x7_tr);

        // Network line: IP and RSSI
        char netBuf[32];
        snprintf(netBuf, sizeof(netBuf), "IP: %s (%ddBm)", getIPAddress().c_str(), WiFi.RSSI());
        u8g2.drawStr(2, 20, netBuf);

        // Stream status line
        char statusBuf[32];
        const char *stateStr = (streamState == STREAM_PLAYING) ? "STREAM" :
                              (streamState == STREAM_BUFFERING) ? "BUFFER" :
                              (streamState == STREAM_PAUSED) ? "PAUSED" : "READY";
        snprintf(statusBuf, sizeof(statusBuf), "[%s] Buf:%d%% V:%d%%", stateStr, getBufferFillPercentage(), audioPlayer.getVolume());
        u8g2.drawStr(2, 31, statusBuf);

        // Now Playing Track / Artist line
        u8g2.setFont(u8g2_font_6x10_tr);
        String dispTitle = currentTrackTitle;
        if (dispTitle.length() > 20) dispTitle = dispTitle.substring(0, 19) + ".";
        u8g2.drawStr(2, 43, dispTitle.c_str());

        String dispArtist = currentTrackArtist;
        if (dispArtist.length() > 20) dispArtist = dispArtist.substring(0, 19) + ".";
        u8g2.drawStr(2, 54, dispArtist.c_str());
    }

    // Mini spectrum visualizer bar at the bottom
    spectrumAnalyzer.renderMiniBars(u8g2, 2, 57, 124, 7);

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
        if (wifiState == WIFI_STATE_AP_MODE || wifiState == WIFI_STATE_CONNECTING) {
            // Attempt to reload SD card wifi.txt and reconnect
            loadSDConfig();
            reconnect();
            ledManager.triggerButtonPulse(CRGB::Cyan, 1, 60);
        } else if (streamState == STREAM_PLAYING) {
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
