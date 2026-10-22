#include "wifi_streamer.h"
#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "led_manager.h"
#include "display.h"
#include "button_manager.h"
#include "battery.h"
#include "power_manager.h"
#include "sd_manager.h"
#include "ai_companion.h"
#include "gemini_client.h"
#include <esp_heap_caps.h>

WiFiStreamer wifiStreamer;


static const char* UPNP_DEVICE_XML_TEMPLATE =
    "<?xml version=\"1.0\"?>\r\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <URLBase>http://%s:8080/</URLBase>\r\n"
    "  <device>\r\n"
    "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n"
    "    <friendlyName>Q-Tune Audiophile Player</friendlyName>\r\n"
    "    <manufacturer>Q-Tune</manufacturer>\r\n"
    "    <manufacturerURL>http://q-tunes.local</manufacturerURL>\r\n"
    "    <modelDescription>Q-Tune Audiophile UPnP/DLNA Hi-Res Network Streamer</modelDescription>\r\n"
    "    <modelName>SuperMini S3</modelName>\r\n"
    "    <modelNumber>Q-TUNE-1.0</modelNumber>\r\n"
    "    <modelURL>http://q-tunes.local</modelURL>\r\n"
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

static const char* UPNP_CONNECTION_MANAGER_SCPD_XML =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action>\r\n"
    "      <name>GetProtocolInfo</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>\r\n"
    "        <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetCurrentConnectionIDs</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetCurrentConnectionInfo</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\r\n"
    "        <argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>\r\n"
    "        <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>\r\n"
    "        <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>\r\n"
    "        <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>\r\n"
    "        <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>\r\n"
    "        <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

static const char* UPNP_AVTRANSPORT_SCPD_XML =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action>\r\n"
    "      <name>SetAVTransportURI</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>Play</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Speed</name><direction>in</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>Pause</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>Stop</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetTransportInfo</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentTransportState</name><direction>out</direction><relatedStateVariable>TransportState</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentTransportStatus</name><direction>out</direction><relatedStateVariable>TransportStatus</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentSpeed</name><direction>out</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetMediaInfo</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>NrTracks</name><direction>out</direction><relatedStateVariable>NumberOfTracks</relatedStateVariable></argument>\r\n"
    "        <argument><name>MediaDuration</name><direction>out</direction><relatedStateVariable>CurrentMediaDuration</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentURI</name><direction>out</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentURIMetaData</name><direction>out</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\r\n"
    "        <argument><name>NextURI</name><direction>out</direction><relatedStateVariable>NextAVTransportURI</relatedStateVariable></argument>\r\n"
    "        <argument><name>NextURIMetaData</name><direction>out</direction><relatedStateVariable>NextAVTransportURIMetaData</relatedStateVariable></argument>\r\n"
    "        <argument><name>PlayMedium</name><direction>out</direction><relatedStateVariable>PlaybackStorageMedium</relatedStateVariable></argument>\r\n"
    "        <argument><name>RecordMedium</name><direction>out</direction><relatedStateVariable>RecordStorageMedium</relatedStateVariable></argument>\r\n"
    "        <argument><name>WriteStatus</name><direction>out</direction><relatedStateVariable>RecordMediumWriteStatus</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetPositionInfo</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Track</name><direction>out</direction><relatedStateVariable>CurrentTrack</relatedStateVariable></argument>\r\n"
    "        <argument><name>TrackDuration</name><direction>out</direction><relatedStateVariable>CurrentTrackDuration</relatedStateVariable></argument>\r\n"
    "        <argument><name>TrackMetaData</name><direction>out</direction><relatedStateVariable>CurrentTrackMetaData</relatedStateVariable></argument>\r\n"
    "        <argument><name>TrackURI</name><direction>out</direction><relatedStateVariable>CurrentTrackURI</relatedStateVariable></argument>\r\n"
    "        <argument><name>RelTime</name><direction>out</direction><relatedStateVariable>RelativeTimePosition</relatedStateVariable></argument>\r\n"
    "        <argument><name>AbsTime</name><direction>out</direction><relatedStateVariable>AbsoluteTimePosition</relatedStateVariable></argument>\r\n"
    "        <argument><name>RelCount</name><direction>out</direction><relatedStateVariable>RelativeCounterPosition</relatedStateVariable></argument>\r\n"
    "        <argument><name>AbsCount</name><direction>out</direction><relatedStateVariable>AbsoluteCounterPosition</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetDeviceCapabilities</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>PlayMedia</name><direction>out</direction><relatedStateVariable>PossiblePlaybackStorageMedia</relatedStateVariable></argument>\r\n"
    "        <argument><name>RecMedia</name><direction>out</direction><relatedStateVariable>PossibleRecordStorageMedia</relatedStateVariable></argument>\r\n"
    "        <argument><name>RecQualityModes</name><direction>out</direction><relatedStateVariable>PossibleRecordQualityModes</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetTransportSettings</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>PlayMode</name><direction>out</direction><relatedStateVariable>CurrentPlayMode</relatedStateVariable></argument>\r\n"
    "        <argument><name>RecQualityMode</name><direction>out</direction><relatedStateVariable>CurrentRecordQualityMode</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>PlaybackStorageMedium</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordStorageMedium</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>PossiblePlaybackStorageMedia</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>PossibleRecordStorageMedia</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentPlayMode</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordMediumWriteStatus</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentRecordQualityMode</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>PossibleRecordQualityModes</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>NumberOfTracks</name><dataType>ui4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrack</name><dataType>ui4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackDuration</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentMediaDuration</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackMetaData</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackURI</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURI</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>RelativeTimePosition</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteTimePosition</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>RelativeCounterPosition</name><dataType>i4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteCounterPosition</name><dataType>i4</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

static const char* UPNP_RENDERING_CONTROL_SCPD_XML =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action>\r\n"
    "      <name>SetVolume</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
    "        <argument><name>DesiredVolume</name><direction>in</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetVolume</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentVolume</name><direction>out</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>SetMute</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
    "        <argument><name>DesiredMute</name><direction>in</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "    <action>\r\n"
    "      <name>GetMute</name>\r\n"
    "      <argumentList>\r\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
    "        <argument><name>CurrentMute</name><direction>out</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\r\n"
    "      </argumentList>\r\n"
    "    </action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"yes\"><name>Mute</name><dataType>boolean</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</dataType></stateVariable>\r\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

static const char* UPNP_SCPD_XML = UPNP_AVTRANSPORT_SCPD_XML;

WiFiStreamer::WiFiStreamer()
    : wifiState(WIFI_STATE_OFF),
      streamState(STREAM_IDLE),
      currentTrackTitle("Ready for Stream"),
      currentTrackArtist("AirMusic / Poweramp"),
      disconnectReason(""),
      currentSampleRate(44100),
      currentBitsPerSample(16),
      lastSSDPAnnounce(0),
      lastUIRenderMs(0),
      wavHeaderParsed(false),
      wavHeaderBytes(0),
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
    memset(wavHeaderBuf, 0, sizeof(wavHeaderBuf));
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

    // Set Wi-Fi mode first before configuring sleep or TX power
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname("q-tunes");
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);                 // Disable modem sleep to prevent latency jitter and dropped frames
    WiFi.setTxPower(WIFI_POWER_17dBm);    // 17dBm for stable SuperMini operation without brownouts

    // Register Wi-Fi event handler for accurate state tracking & disconnect reasons
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            wifiState = WIFI_STATE_CONNECTED;
            disconnectReason = "";
            ssdpUdp.stop();
            ssdpUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
            sendSSDPAnnounce();
            MDNS.begin("q-tunes");
        } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason == 201) disconnectReason = "Network Not Found";
            else if (reason == 202) disconnectReason = "Wrong Password";
            else if (reason == 15 || reason == 204) disconnectReason = "Handshake Timeout";
            else if (reason == 203) disconnectReason = "Association Failed";
            else disconnectReason = "Disconnected (" + String(reason) + ")";

            if (wifiState == WIFI_STATE_CONNECTED) {
                wifiState = WIFI_STATE_CONNECTING;
            }
        }
    });

    // Always host SoftAP for seamless captive portal setup
    WiFi.softAP("Q-Tune-Audio", "12345678");

    // Attempt to connect to saved Wi-Fi credentials
    if (savedSSID.length() > 0) {
        wifiState = WIFI_STATE_CONNECTING;
        disconnectReason = "";
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

    // Initialize mDNS responder (http://q-tunes.local)
    if (MDNS.begin("q-tunes")) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("upnp", "tcp", 8080);
        MDNS.addServiceTxt("http", "tcp", "device", "Q-Tune");
        MDNS.addServiceTxt("http", "tcp", "version", "1.0");
    }

    // SSDP multicast listener on UDP port 1900
    ssdpUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
    sendSSDPAnnounce();
    lastSSDPAnnounce = millis();

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

    MDNS.end();
    dnsServer.stop();
    configServer.stop();
    upnpServer.stop();
    ssdpUdp.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiState = WIFI_STATE_OFF;
}

void WiFiStreamer::toggleWiFi() {
    if (wifiState != WIFI_STATE_OFF) {
        stop();
    } else {
        start();
    }
}

void WiFiStreamer::startAPMode() {
    wifiState = WIFI_STATE_AP_MODE;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Q-Tune-Audio", "12345678");
}

void WiFiStreamer::reconnect() {
    if (savedSSID.length() > 0) {
        wifiState = WIFI_STATE_CONNECTING;
        disconnectReason = "";
        WiFi.disconnect(false);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("Q-Tune-Audio", "12345678");
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    } else {
        startAPMode();
    }
}

String WiFiStreamer::getMDNSHostname() const {
    return "q-tunes.local";
}

void WiFiStreamer::playStreamUrl(const String &url) {
    if (url.length() == 0) return;
    activeStreamUrl = url;
    currentTrackTitle = "Direct Stream";
    currentTrackArtist = url;
    if (currentTrackArtist.startsWith("http://")) currentTrackArtist = currentTrackArtist.substring(7);
    int slash = currentTrackArtist.indexOf('/');
    if (slash != -1) currentTrackArtist = currentTrackArtist.substring(0, slash);
    if (activeStreamClient.connected()) {
        activeStreamClient.stop();
    }
    hasActiveStream = true;
    streamState = STREAM_BUFFERING;
    wavHeaderParsed = false;
    wavHeaderBytes = 0;
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
    wavHeaderParsed = false;
    wavHeaderBytes = 0;
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

    auto sendNotify = [&](const char* nt, const char* usn) {
        snprintf(notifyPacket, sizeof(notifyPacket),
            "NOTIFY * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "CACHE-CONTROL: max-age=1800\r\n"
            "LOCATION: http://%s:8080/description.xml\r\n"
            "NT: %s\r\n"
            "NTS: ssdp:alive\r\n"
            "SERVER: POSIX/1.0 UPnP/1.0 Q-Tune/1.0\r\n"
            "USN: %s\r\n"
            "BOOTID.UPNP.ORG: 1\r\n\r\n",
            localIp.toString().c_str(),
            nt,
            usn
        );
        ssdpUdp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
        ssdpUdp.write((const uint8_t*)notifyPacket, strlen(notifyPacket));
        ssdpUdp.endPacket();
    };

    // 1. Announce MediaRenderer:1
    sendNotify("urn:schemas-upnp-org:device:MediaRenderer:1",
               "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1");

    // 2. Announce upnp:rootdevice
    sendNotify("upnp:rootdevice",
               "uuid:q-tune-audiophile-player::upnp:rootdevice");

    // 3. Announce device UUID
    sendNotify("uuid:q-tune-audiophile-player",
               "uuid:q-tune-audiophile-player");
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
             strstr(buffer, "upnp:rootdevice") != NULL ||
             strstr(buffer, "AVTransport") != NULL ||
             strstr(buffer, "ConnectionManager") != NULL ||
             strstr(buffer, "RenderingControl") != NULL)) {

            IPAddress localIp = (wifiState == WIFI_STATE_AP_MODE) ? WiFi.softAPIP() : WiFi.localIP();

            auto sendResponse = [&](const char* st, const char* usn) {
                char response[512];
                snprintf(response, sizeof(response),
                    "HTTP/1.1 200 OK\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "EXT:\r\n"
                    "LOCATION: http://%s:8080/description.xml\r\n"
                    "SERVER: POSIX/1.0 UPnP/1.0 Q-Tune/1.0\r\n"
                    "ST: %s\r\n"
                    "USN: %s\r\n"
                    "BOOTID.UPNP.ORG: 1\r\n\r\n",
                    localIp.toString().c_str(),
                    st,
                    usn
                );
                ssdpUdp.beginPacket(ssdpUdp.remoteIP(), ssdpUdp.remotePort());
                ssdpUdp.write((const uint8_t*)response, strlen(response));
                ssdpUdp.endPacket();
            };

            // If control point queries ssdp:all or upnp:rootdevice, send both MediaRenderer:1 and rootdevice
            if (strstr(buffer, "ssdp:all") != NULL) {
                sendResponse("urn:schemas-upnp-org:device:MediaRenderer:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1");
                sendResponse("upnp:rootdevice",
                             "uuid:q-tune-audiophile-player::upnp:rootdevice");
                sendResponse("uuid:q-tune-audiophile-player",
                             "uuid:q-tune-audiophile-player");
            } else if (strstr(buffer, "upnp:rootdevice") != NULL) {
                sendResponse("upnp:rootdevice",
                             "uuid:q-tune-audiophile-player::upnp:rootdevice");
                sendResponse("urn:schemas-upnp-org:device:MediaRenderer:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1");
            } else if (strstr(buffer, "MediaRenderer") != NULL) {
                sendResponse("urn:schemas-upnp-org:device:MediaRenderer:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1");
            } else if (strstr(buffer, "AVTransport") != NULL) {
                sendResponse("urn:schemas-upnp-org:service:AVTransport:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:service:AVTransport:1");
            } else if (strstr(buffer, "ConnectionManager") != NULL) {
                sendResponse("urn:schemas-upnp-org:service:ConnectionManager:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:service:ConnectionManager:1");
            } else if (strstr(buffer, "RenderingControl") != NULL) {
                sendResponse("urn:schemas-upnp-org:service:RenderingControl:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:service:RenderingControl:1");
            } else {
                sendResponse("urn:schemas-upnp-org:device:MediaRenderer:1",
                             "uuid:q-tune-audiophile-player::urn:schemas-upnp-org:device:MediaRenderer:1");
            }
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
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>Q-Tune Audiophile Remote</title>"
                      "<style>"
                      ":root{--bg:#121214;--card:#1e1e24;--accent:#00e5ff;--pink:#ff007f;--green:#00e676;--text:#f0f0f5;--sub:#8e8e9a;--border:#2c2c36;}"
                      "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,sans-serif;}"
                      "body{background:var(--bg);color:var(--text);padding:16px;display:flex;flex-direction:column;align-items:center;min-height:100vh;}"
                      ".container{width:100%;max-width:440px;display:flex;flex-direction:column;gap:16px;}"
                      ".header{text-align:center;padding:12px 0 6px;}"
                      ".header h1{font-size:22px;letter-spacing:2px;color:var(--accent);font-weight:800;}"
                      ".header .badge{display:inline-block;background:#1a2b35;color:var(--accent);font-size:12px;padding:3px 10px;border-radius:12px;border:1px solid rgba(0,229,255,0.3);margin-top:4px;text-decoration:none;}"
                      ".card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:16px;box-shadow:0 6px 18px rgba(0,0,0,0.4);}"
                      ".card h2{font-size:15px;text-transform:uppercase;letter-spacing:1px;color:var(--sub);margin-bottom:12px;}"
                      ".now-playing{text-align:center;padding:20px 16px;}"
                      ".track-title{font-size:18px;font-weight:700;color:#fff;margin-bottom:4px;word-break:break-word;}"
                      ".track-artist{font-size:14px;color:var(--sub);margin-bottom:12px;}"
                      ".status-pill{display:inline-block;padding:4px 12px;border-radius:20px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:1px;background:#2a2a38;color:var(--green);border:1px solid var(--green);}"
                      ".status-pill.buffering{color:#ffea00;border-color:#ffea00;}"
                      ".status-pill.idle{color:var(--sub);border-color:var(--sub);}"
                      ".bar-wrap{background:#141418;border-radius:6px;height:8px;overflow:hidden;margin:12px 0 4px;border:1px solid var(--border);}"
                      ".bar-fill{background:linear-gradient(90deg,var(--accent),var(--pink));height:100%;width:0%;transition:width 0.3s;}"
                      ".bar-label{font-size:11px;color:var(--sub);display:flex;justify-content:space-between;}"
                      ".controls{display:flex;gap:10px;margin-top:14px;justify-content:center;}"
                      "button,.btn{background:#2a2a36;color:var(--text);border:1px solid var(--border);padding:10px 16px;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;transition:all 0.2s;}"
                      "button:hover,.btn:hover{background:#353545;border-color:var(--accent);}"
                      "button.primary{background:var(--accent);color:#000;border:none;}"
                      "button.primary:hover{background:#33ebff;}"
                      ".vol-box{display:flex;align-items:center;gap:12px;margin-top:14px;}"
                      ".vol-box input[type=range]{flex:1;accent-color:var(--accent);height:6px;cursor:pointer;}"
                      ".info-row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #252530;font-size:13px;}"
                      ".info-row:last-child{border-bottom:none;}"
                      ".info-label{color:var(--sub);}"
                      ".input-group{margin-bottom:12px;}"
                      ".input-group label{display:block;font-size:12px;color:var(--sub);margin-bottom:4px;}"
                      "input[type=text],input[type=password]{width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--border);background:#16161c;color:#fff;font-size:14px;}"
                      "input[type=text]:focus,input[type=password]:focus{outline:none;border-color:var(--accent);}"
                      ".scan-list{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;}"
                      ".scan-item{background:#282834;font-size:12px;padding:4px 10px;border-radius:6px;cursor:pointer;border:1px solid var(--border);}"
                      ".scan-item:hover{border-color:var(--accent);color:var(--accent);}"
                      ".guide-box{font-size:12px;line-height:1.5;color:var(--sub);background:#17171e;padding:12px;border-radius:8px;}"
                      ".guide-box b{color:var(--text);}"
                      "</style></head><body><div class='container'>"
                      "<div class='header'>"
                      "<h1>Q-TUNE AUDIOPHILE</h1>"
                      "<a class='badge' href='http://q-tunes.local' target='_blank'>http://q-tunes.local</a>"
                      "</div>"
                      "<div class='card now-playing'>"
                      "<h2>Live Network Stream</h2>"
                      "<div class='track-title' id='title'>" + htmlEscape(currentTrackTitle) + "</div>"
                      "<div class='track-artist' id='artist'>" + htmlEscape(currentTrackArtist) + "</div>"
                      "<span class='status-pill' id='st-pill'>" + String(streamState == STREAM_PLAYING ? "STREAMING" : (streamState == STREAM_BUFFERING ? "BUFFERING" : (streamState == STREAM_PAUSED ? "PAUSED" : "IDLE"))) + "</span>"
                      "<div class='bar-wrap'><div class='bar-fill' id='buf-bar' style='width:" + String(getBufferFillPercentage()) + "%;'></div></div>"
                      "<div class='bar-label'><span>Buffer Fill</span><span id='buf-pct'>" + String(getBufferFillPercentage()) + "%</span></div>"
                      "<div class='controls'>"
                      "<button onclick='callApi(\"/api/playback?action=toggle\")' class='primary'>Play / Pause</button>"
                      "<button onclick='callApi(\"/toggle_output\")' id='out-btn'>Output: " + String(audioPlayer.getOutputModeShortName()) + "</button>"
                      "</div>"
                      "<div class='vol-box'>"
                      "<span style='font-size:12px;color:var(--sub);'>Vol</span>"
                      "<input type='range' min='0' max='100' id='vol-slider' value='" + String(audioPlayer.getVolume()) + "' oninput='setVol(this.value)'>"
                      "<span id='vol-num' style='font-size:13px;width:32px;font-weight:700;'>" + String(audioPlayer.getVolume()) + "%</span>"
                      "</div></div>"
                      "<div class='card'>"
                      "<h2>Direct Audio Stream URL</h2>"
                      "<div class='input-group'>"
                      "<input type='text' id='stream-url' placeholder='http://host:port/stream.wav'>"
                      "</div>"
                      "<button onclick='playUrl()' class='primary' style='width:100%;'>Stream to Q-Tune</button>"
                      "</div>"
                      "<div class='card'>"
                      "<h2>Wi-Fi Setup & Network</h2>"
                      "<div class='info-row'><span class='info-label'>Status</span><span id='net-status'>" + (wifiState == WIFI_STATE_CONNECTED ? "Connected: " + htmlEscape(savedSSID) : (wifiState == WIFI_STATE_CONNECTING ? "Connecting: " + htmlEscape(savedSSID) : "SoftAP Mode")) + "</span></div>"
                      "<div class='info-row'><span class='info-label'>IP Address</span><span id='net-ip'>" + ip.toString() + "</span></div>"
                      "<div class='info-row'><span class='info-label'>mDNS URL</span><span style='color:var(--accent);font-weight:bold;'>http://q-tunes.local</span></div>"
                      "<div class='info-row'><span class='info-label'>Signal</span><span id='net-rssi'>" + (wifiState == WIFI_STATE_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "N/A") + "</span></div>"
                      "<div style='margin-top:12px;'>"
                      "<div style='display:flex;justify-content:space-between;align-items:center;'>"
                      "<label style='font-size:12px;color:var(--sub);'>Nearby Networks</label>"
                      "<button onclick='scanWifi()' style='padding:4px 10px;font-size:11px;'>Scan</button>"
                      "</div>"
                      "<div class='scan-list' id='scan-results'></div>"
                      "</div>"
                      "<form method='POST' action='/save' style='margin-top:12px;'>"
                      "<div class='input-group'>"
                      "<label>SSID</label>"
                      "<input type='text' name='ssid' id='ssid-in' value='" + htmlEscape(savedSSID) + "' required>"
                      "</div>"
                      "<div class='input-group'>"
                      "<label>Password</label>"
                      "<input type='password' name='pass' id='pass-in'>"
                      "</div>"
                      "<button type='submit' class='primary' style='width:100%;'>Save & Connect</button>"
                      "</form></div>"
                      "<div class='card'>"
                      "<h2>Streaming Quick Guide</h2>"
                      "<div class='guide-box'>"
                      "<p style='margin-bottom:6px;'><b>Android:</b> Open <b>BubbleUPnP</b> or <b>AirMusic</b> &rarr; select <i>Q-Tune Audiophile Player</i> as renderer.</p>"
                      "<p style='margin-bottom:6px;'><b>Windows:</b> Right click any music file &rarr; <i>Cast to Device</i> &rarr; <i>Q-Tune Audiophile Player</i>.</p>"
                      "<p><b>iOS / Mac:</b> Use <b>VLC</b> (Local Network &rarr; UPnP) or <b>Foobar2000 Mobile</b>.</p>"
                      "</div></div></div>"
                      "<script>"
                      "function callApi(ep){fetch(ep,{method:'POST'}).then(()=>updateStatus());}"
                      "function setVol(v){document.getElementById('vol-num').innerText=v+'%';fetch('/api/volume?val='+v,{method:'POST'});}"
                      "function playUrl(){var u=document.getElementById('stream-url').value;if(u){fetch('/api/stream_url?url='+encodeURIComponent(u),{method:'POST'}).then(()=>updateStatus());}}"
                      "function scanWifi(){var el=document.getElementById('scan-results');el.innerHTML='Scanning...';fetch('/scan').then(r=>r.json()).then(d=>{if(d.networks){el.innerHTML='';d.networks.forEach(n=>{var b=document.createElement('div');b.className='scan-item';b.innerText=n.ssid+' ('+n.rssi+'dBm)';b.onclick=function(){document.getElementById('ssid-in').value=n.ssid;};el.appendChild(b);});}else{setTimeout(scanWifi,1500);}}).catch(()=>{el.innerHTML='Scan failed';});}"
                      "function updateStatus(){fetch('/api/status').then(r=>r.json()).then(d=>{"
                      "document.getElementById('title').innerText=d.title||'Ready for Stream';"
                      "document.getElementById('artist').innerText=d.artist||'AirMusic / Poweramp';"
                      "var pill=document.getElementById('st-pill');pill.innerText=d.stream_state;pill.className='status-pill '+(d.stream_state=='STREAMING'?'':d.stream_state.toLowerCase());"
                      "document.getElementById('buf-bar').style.width=d.buf_percent+'%';"
                      "document.getElementById('buf-pct').innerText=d.buf_percent+'%';"
                      "document.getElementById('vol-slider').value=d.volume;"
                      "document.getElementById('vol-num').innerText=d.volume+'%';"
                      "document.getElementById('out-btn').innerText='Output: '+(d.output_short||'SPKR');"
                      "document.getElementById('net-status').innerText=d.state=='CONNECTED'?('Connected ('+d.ssid+')'):(d.state=='CONNECTING'?'Connecting...':'SoftAP');"
                      "document.getElementById('net-ip').innerText=d.ip;"
                      "document.getElementById('net-rssi').innerText=d.state=='CONNECTED'?(d.rssi+' dBm'):'N/A';"
                      "if(!document.getElementById('ssid-in').value&&d.ssid)document.getElementById('ssid-in').value=d.ssid;"
                      "}).catch(()=>{});}"
                      "setInterval(updateStatus,2000);updateStatus();"
                      "</script></body></html>";
        configServer.send(200, "text/html", html);
    });

    configServer.on("/save", HTTP_POST, [this]() {
        if (configServer.hasArg("ssid")) {
            setCredentials(configServer.arg("ssid"), configServer.arg("pass"));
            String resp = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                          "<meta http-equiv='refresh' content='4;url=/'>"
                          "<style>body{font-family:sans-serif;background:#121214;color:#eee;text-align:center;padding:30px;}"
                          ".card{background:#1e1e24;border-radius:10px;padding:20px;margin:auto;max-width:360px;}"
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
        if (configServer.hasArg("redirect")) {
            configServer.sendHeader("Location", "/");
            configServer.send(303);
        } else {
            configServer.send(200, "application/json", "{\"output\":\"" + String(audioPlayer.getOutputModeShortName()) + "\"}");
        }
    });

    // REST API endpoints
    configServer.on("/api/status", HTTP_GET, [this]() {
        IPAddress ip = (wifiState == WIFI_STATE_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        const char *stateStr = (streamState == STREAM_PLAYING) ? "STREAMING" :
                              (streamState == STREAM_BUFFERING) ? "BUFFERING" :
                              (streamState == STREAM_PAUSED) ? "PAUSED" : "IDLE";
        String json = "{";
        json += "\"state\":\"" + String(wifiState == WIFI_STATE_CONNECTED ? "CONNECTED" : (wifiState == WIFI_STATE_CONNECTING ? "CONNECTING" : "AP_MODE")) + "\",";
        json += "\"ip\":\"" + ip.toString() + "\",";
        json += "\"mdns\":\"q-tunes.local\",";
        json += "\"ssid\":\"" + htmlEscape(savedSSID) + "\",";
        json += "\"rssi\":" + String(getRSSI()) + ",";
        json += "\"output\":\"" + String(audioPlayer.getOutputModeName()) + "\",";
        json += "\"output_short\":\"" + String(audioPlayer.getOutputModeShortName()) + "\",";
        json += "\"stream_state\":\"" + String(stateStr) + "\",";
        json += "\"title\":\"" + htmlEscape(currentTrackTitle) + "\",";
        json += "\"artist\":\"" + htmlEscape(currentTrackArtist) + "\",";
        json += "\"volume\":" + String(audioPlayer.getVolume()) + ",";
        json += "\"buf_percent\":" + String(getBufferFillPercentage()) + ",";
        json += "\"buf_bytes\":" + String(ringCount);
        json += "}";
        configServer.send(200, "application/json", json);
    });

    configServer.on("/api/playback", HTTP_POST, [this]() {
        String action = configServer.hasArg("action") ? configServer.arg("action") : "toggle";
        if (action == "pause") {
            pauseStream();
        } else if (action == "resume") {
            resumeStream();
        } else if (action == "stop") {
            stopStream();
        } else {
            if (streamState == STREAM_PLAYING) pauseStream();
            else if (streamState == STREAM_PAUSED) resumeStream();
        }
        configServer.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    configServer.on("/api/volume", HTTP_POST, [this]() {
        if (configServer.hasArg("val")) {
            int vol = configServer.arg("val").toInt();
            audioPlayer.setVolume((uint8_t)constrain(vol, 0, 100));
        } else if (configServer.hasArg("step")) {
            int step = configServer.arg("step").toInt();
            if (step > 0) audioPlayer.volumeUp(step);
            else if (step < 0) audioPlayer.volumeDown(-step);
        }
        configServer.send(200, "application/json", "{\"volume\":" + String(audioPlayer.getVolume()) + "}");
    });

    configServer.on("/api/stream_url", HTTP_POST, [this]() {
        if (configServer.hasArg("url")) {
            String url = configServer.arg("url");
            url.trim();
            if (url.length() > 0) {
                playStreamUrl(url);
                configServer.send(200, "application/json", "{\"status\":\"ok\",\"streaming\":true}");
                return;
            }
        }
        configServer.send(400, "application/json", "{\"error\":\"Missing URL\"}");
    });

    configServer.on("/api/ai/chat", HTTP_POST, [this]() {
        String query = "";
        if (configServer.hasArg("query")) {
            query = configServer.arg("query");
        } else if (configServer.hasArg("plain")) {
            query = configServer.arg("plain");
        }
        query.trim();
        if (query.length() == 0) {
            configServer.send(400, "application/json", "{\"error\":\"Empty query\"}");
            return;
        }

        String reply, emote, action;
        bool ok = geminiClient.ask(query.c_str(), reply, emote, action);
        String json = "{";
        json += "\"status\":\"" + String(ok ? "ok" : "error") + "\",";
        json += "\"reply\":\"" + reply + "\",";
        json += "\"emote\":\"" + emote + "\",";
        json += "\"action\":\"" + action + "\",";
        json += "\"companion\":\"" + String(aiCompanion.getCompanion() == COMPANION_CAT ? "DJ_NEKO" : "MOCHI") + "\"";
        json += "}";
        configServer.send(200, "application/json", json);
    });

    configServer.on("/api/ai/companion", HTTP_POST, [this]() {
        if (configServer.hasArg("type")) {
            String t = configServer.arg("type");
            t.toUpperCase();
            if (t.indexOf("CAT") != -1 || t.indexOf("NEKO") != -1) {
                aiCompanion.setCompanion(COMPANION_CAT);
            } else if (t.indexOf("MOCHI") != -1) {
                aiCompanion.setCompanion(COMPANION_MOCHI);
            }
        } else if (configServer.hasArg("speed")) {
            String s = configServer.arg("speed");
            if (s.indexOf("0.5") != -1) aiCompanion.setSpeedIndex(0);
            else if (s.indexOf("0.75") != -1) aiCompanion.setSpeedIndex(1);
            else if (s.indexOf("1.25") != -1) aiCompanion.setSpeedIndex(3);
            else if (s.indexOf("1.5") != -1) aiCompanion.setSpeedIndex(4);
            else if (s.indexOf("2.0") != -1) aiCompanion.setSpeedIndex(5);
            else aiCompanion.setSpeedIndex(2); // 1.0x
        } else {
            aiCompanion.toggleCompanion();
        }
        configServer.send(200, "application/json", "{\"companion\":\"" + String(aiCompanion.getCompanionName()) + "\",\"speed\":\"" + String(aiCompanion.getSpeedString()) + "\"}");
    });

    configServer.on("/scan", HTTP_GET, [this]() {
        int n = WiFi.scanComplete();
        if (n == -2) {
            WiFi.scanNetworks(true);
            configServer.send(200, "application/json", "{\"status\":\"scanning\"}");
        } else if (n >= 0) {
            String json = "{\"status\":\"ready\",\"networks\":[";
            for (int i = 0; i < n; i++) {
                if (i > 0) json += ",";
                json += "{\"ssid\":\"" + htmlEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
            }
            json += "]}";
            WiFi.scanDelete();
            configServer.send(200, "application/json", json);
        } else {
            configServer.send(200, "application/json", "{\"status\":\"pending\"}");
        }
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
    const char* upnpHeaders[] = {"SOAPAction", "soapaction", "Content-Type", "content-type", "SID", "TIMEOUT", "CALLBACK", "NT"};
    upnpServer.collectHeaders(upnpHeaders, 8);

    upnpServer.on("/description.xml", HTTP_GET, [this]() {
        IPAddress localIp = (wifiState == WIFI_STATE_AP_MODE) ? WiFi.softAPIP() : WiFi.localIP();
        char *descBuf = (char*)malloc(2560);
        if (descBuf) {
            snprintf(descBuf, 2560, UPNP_DEVICE_XML_TEMPLATE, localIp.toString().c_str());
            upnpServer.sendHeader("EXT", "");
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", descBuf);
            free(descBuf);
        } else {
            upnpServer.send(500, "text/plain", "OOM");
        }
    });

    upnpServer.on("/RenderingControl/desc.xml", HTTP_GET, [this]() {
        upnpServer.sendHeader("EXT", "");
        upnpServer.send(200, "text/xml; charset=\"utf-8\"", UPNP_RENDERING_CONTROL_SCPD_XML);
    });
    upnpServer.on("/AVTransport/desc.xml", HTTP_GET, [this]() {
        upnpServer.sendHeader("EXT", "");
        upnpServer.send(200, "text/xml; charset=\"utf-8\"", UPNP_AVTRANSPORT_SCPD_XML);
    });
    upnpServer.on("/ConnectionManager/desc.xml", HTTP_GET, [this]() {
        upnpServer.sendHeader("EXT", "");
        upnpServer.send(200, "text/xml; charset=\"utf-8\"", UPNP_CONNECTION_MANAGER_SCPD_XML);
    });

    // ConnectionManager Control Endpoint (ProtocolInfo & Connection IDs)
    upnpServer.on("/ConnectionManager/control", HTTP_POST, [this]() {
        String body = upnpServer.arg("plain");
        String soapAction = upnpServer.header("SOAPAction");
        if (soapAction.length() == 0) soapAction = upnpServer.header("soapaction");

        upnpServer.sendHeader("EXT", "");

        if (body.indexOf("GetProtocolInfo") != -1 || soapAction.indexOf("GetProtocolInfo") != -1) {
            String soap = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                          "  <s:Body><u:GetProtocolInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">\r\n"
                          "    <Source></Source>\r\n"
                          "    <Sink>http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM,http-get:*:audio/L16;rate=48000;channels=2:DLNA.ORG_PN=LPCM,http-get:*:audio/L16:*,http-get:*:audio/wav:DLNA.ORG_PN=WAV,http-get:*:audio/wav:*,http-get:*:audio/x-wav:*,http-get:*:audio/wave:*,http-get:*:audio/vnd.wave:*,http-get:*:audio/mpeg:DLNA.ORG_PN=MP3,http-get:*:audio/mpeg:*,http-get:*:audio/mp3:*,http-get:*:audio/mp4:*,http-get:*:audio/x-m4a:*,http-get:*:audio/aac:*,http-get:*:audio/flac:*,http-get:*:audio/x-flac:*,http-get:*:audio/ogg:*,http-get:*:application/ogg:*</Sink>\r\n"
                          "  </u:GetProtocolInfoResponse></s:Body>\r\n"
                          "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soap);
        } else if (body.indexOf("GetCurrentConnectionIDs") != -1 || soapAction.indexOf("GetCurrentConnectionIDs") != -1) {
            String soap = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                          "  <s:Body><u:GetCurrentConnectionIDsResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">\r\n"
                          "    <ConnectionIDs>0</ConnectionIDs>\r\n"
                          "  </u:GetCurrentConnectionIDsResponse></s:Body>\r\n"
                          "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soap);
        } else if (body.indexOf("GetCurrentConnectionInfo") != -1 || soapAction.indexOf("GetCurrentConnectionInfo") != -1) {
            String soap = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                          "  <s:Body><u:GetCurrentConnectionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">\r\n"
                          "    <RcsID>0</RcsID>\r\n"
                          "    <AVTransportID>0</AVTransportID>\r\n"
                          "    <ProtocolInfo>http-get:*:audio/wav:*</ProtocolInfo>\r\n"
                          "    <PeerConnectionManager></PeerConnectionManager>\r\n"
                          "    <PeerConnectionID>-1</PeerConnectionID>\r\n"
                          "    <Direction>Input</Direction>\r\n"
                          "    <Status>OK</Status>\r\n"
                          "  </u:GetCurrentConnectionInfoResponse></s:Body>\r\n"
                          "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soap);
        } else {
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body/></s:Envelope>");
        }
    });

    // AVTransport Control Endpoint (SetAVTransportURI, Play, Pause, Stop, GetMediaInfo, GetPositionInfo, GetDeviceCapabilities, GetTransportSettings)
    upnpServer.on("/AVTransport/control", HTTP_POST, [this]() {
        String body = upnpServer.arg("plain");
        String soapAction = upnpServer.header("SOAPAction");
        if (soapAction.length() == 0) soapAction = upnpServer.header("soapaction");

        upnpServer.sendHeader("EXT", "");

        if (body.indexOf("SetAVTransportURI") != -1 || soapAction.indexOf("SetAVTransportURI") != -1) {
            // Extract CurrentURI with CDATA, attribute, and XML entity resilience
            int tagIdx = body.indexOf("CurrentURI");
            if (tagIdx != -1) {
                int openAngle = body.indexOf('>', tagIdx);
                int closeAngle = body.indexOf('<', openAngle);
                if (openAngle != -1 && closeAngle != -1 && closeAngle > openAngle) {
                    String uri = body.substring(openAngle + 1, closeAngle);
                    uri.trim();
                    if (uri.startsWith("<![CDATA[") && uri.endsWith("]]>")) {
                        uri = uri.substring(9, uri.length() - 3);
                        uri.trim();
                    }
                    uri.replace("&amp;", "&");
                    uri.replace("&lt;", "<");
                    uri.replace("&gt;", ">");
                    uri.replace("&quot;", "\"");
                    uri.replace("&apos;", "'");
                    if (uri.length() > 0 && (uri.startsWith("http://") || uri.startsWith("https://"))) {
                        activeStreamUrl = uri;
                    }
                }
            }

            // Extract metadata (Title & Artist)
            int titleIdx = body.indexOf("dc:title");
            if (titleIdx != -1) {
                int openAngle = body.indexOf('>', titleIdx);
                int closeAngle = body.indexOf('<', openAngle);
                if (openAngle != -1 && closeAngle != -1 && closeAngle > openAngle) {
                    String t = body.substring(openAngle + 1, closeAngle);
                    t.trim();
                    t.replace("&amp;", "&");
                    t.replace("&lt;", "<");
                    t.replace("&gt;", ">");
                    t.replace("&quot;", "\"");
                    t.replace("&apos;", "'");
                    if (t.length() > 0) currentTrackTitle = t;
                }
            } else {
                currentTrackTitle = "AirMusic Stream";
            }

            int artistIdx = body.indexOf("upnp:artist");
            if (artistIdx == -1) artistIdx = body.indexOf("dc:creator");
            if (artistIdx != -1) {
                int openAngle = body.indexOf('>', artistIdx);
                int closeAngle = body.indexOf('<', openAngle);
                if (openAngle != -1 && closeAngle != -1 && closeAngle > openAngle) {
                    String a = body.substring(openAngle + 1, closeAngle);
                    a.trim();
                    a.replace("&amp;", "&");
                    a.replace("&lt;", "<");
                    a.replace("&gt;", ">");
                    a.replace("&quot;", "\"");
                    a.replace("&apos;", "'");
                    if (a.length() > 0) currentTrackArtist = a;
                }
            } else {
                currentTrackArtist = "Wi-Fi Cast";
            }

            if (activeStreamClient.connected()) {
                activeStreamClient.stop();
            }
            hasActiveStream = true;
            streamState = STREAM_BUFFERING;
            wavHeaderParsed = false;
            wavHeaderBytes = 0;

            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("Play") != -1 || soapAction.indexOf("Play") != -1) {
            hasActiveStream = true;
            streamState = STREAM_PLAYING;
            wavHeaderParsed = false;
            wavHeaderBytes = 0;
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:PlayResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("Pause") != -1 || soapAction.indexOf("Pause") != -1) {
            pauseStream();
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:PauseResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("Stop") != -1 || soapAction.indexOf("Stop") != -1) {
            stopStream();
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:StopResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetMediaInfo") != -1 || soapAction.indexOf("GetMediaInfo") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetMediaInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
                              "    <NrTracks>1</NrTracks>\r\n"
                              "    <MediaDuration>00:00:00</MediaDuration>\r\n"
                              "    <CurrentURI>" + htmlEscape(activeStreamUrl) + "</CurrentURI>\r\n"
                              "    <CurrentURIMetaData></CurrentURIMetaData>\r\n"
                              "    <NextURI></NextURI>\r\n"
                              "    <NextURIMetaData></NextURIMetaData>\r\n"
                              "    <PlayMedium>NETWORK</PlayMedium>\r\n"
                              "    <RecordMedium>NOT_IMPLEMENTED</RecordMedium>\r\n"
                              "    <WriteStatus>NOT_IMPLEMENTED</WriteStatus>\r\n"
                              "  </u:GetMediaInfoResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetPositionInfo") != -1 || soapAction.indexOf("GetPositionInfo") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
                              "    <Track>1</Track>\r\n"
                              "    <TrackDuration>00:00:00</TrackDuration>\r\n"
                              "    <TrackMetaData></TrackMetaData>\r\n"
                              "    <TrackURI>" + htmlEscape(activeStreamUrl) + "</TrackURI>\r\n"
                              "    <RelTime>00:00:00</RelTime>\r\n"
                              "    <AbsTime>00:00:00</AbsTime>\r\n"
                              "    <RelCount>2147483647</RelCount>\r\n"
                              "    <AbsCount>2147483647</AbsCount>\r\n"
                              "  </u:GetPositionInfoResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetDeviceCapabilities") != -1 || soapAction.indexOf("GetDeviceCapabilities") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetDeviceCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
                              "    <PlayMedia>NETWORK</PlayMedia>\r\n"
                              "    <RecMedia>NOT_IMPLEMENTED</RecMedia>\r\n"
                              "    <RecQualityModes>NOT_IMPLEMENTED</RecQualityModes>\r\n"
                              "  </u:GetDeviceCapabilitiesResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetTransportSettings") != -1 || soapAction.indexOf("GetTransportSettings") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetTransportSettingsResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
                              "    <PlayMode>NORMAL</PlayMode>\r\n"
                              "    <RecQualityMode>NOT_IMPLEMENTED</RecQualityMode>\r\n"
                              "  </u:GetTransportSettingsResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else {
            // Default: GetTransportInfo
            String stateStr = (hasActiveStream && streamState != STREAM_PAUSED && streamState != STREAM_IDLE) ? "PLAYING" :
                              (streamState == STREAM_PLAYING) ? "PLAYING" :
                              (streamState == STREAM_PAUSED) ? "PAUSED_PLAYBACK" : "STOPPED";
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetTransportInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
                              "    <CurrentTransportState>" + stateStr + "</CurrentTransportState>\r\n"
                              "    <CurrentTransportStatus>OK</CurrentTransportStatus>\r\n"
                              "    <CurrentSpeed>1</CurrentSpeed>\r\n"
                              "  </u:GetTransportInfoResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        }
    });

    // RenderingControl Control Endpoint (Volume & Mute control)
    upnpServer.on("/RenderingControl/control", HTTP_POST, [this]() {
        String body = upnpServer.arg("plain");
        String soapAction = upnpServer.header("SOAPAction");
        if (soapAction.length() == 0) soapAction = upnpServer.header("soapaction");

        upnpServer.sendHeader("EXT", "");

        if (body.indexOf("SetVolume") != -1 || soapAction.indexOf("SetVolume") != -1) {
            int volTag = body.indexOf("DesiredVolume");
            if (volTag != -1) {
                int start = body.indexOf('>', volTag);
                int end = body.indexOf('<', start);
                if (start != -1 && end != -1 && end > start) {
                    int vol = body.substring(start + 1, end).toInt();
                    audioPlayer.setVolume((uint8_t)constrain(vol, 0, 100));
                }
            }
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:SetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetVolume") != -1 || soapAction.indexOf("GetVolume") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">\r\n"
                              "    <CurrentVolume>" + String(audioPlayer.getVolume()) + "</CurrentVolume>\r\n"
                              "  </u:GetVolumeResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("SetMute") != -1 || soapAction.indexOf("SetMute") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:SetMuteResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("GetMute") != -1 || soapAction.indexOf("GetMute") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:GetMuteResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">\r\n"
                              "    <CurrentMute>0</CurrentMute>\r\n"
                              "  </u:GetMuteResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else if (body.indexOf("ListPresets") != -1 || soapAction.indexOf("ListPresets") != -1) {
            String soapResp = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
                              "  <s:Body><u:ListPresetsResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">\r\n"
                              "    <CurrentPresetList>FactoryDefaults</CurrentPresetList>\r\n"
                              "  </u:ListPresetsResponse></s:Body>\r\n"
                              "</s:Envelope>\r\n";
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", soapResp);
        } else {
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body/></s:Envelope>");
        }
    });

    // Event Subscription Endpoints
    auto handleEventSub = [this]() {
        upnpServer.sendHeader("EXT", "");
        upnpServer.sendHeader("SID", "uuid:q-tune-sub-12345");
        upnpServer.sendHeader("TIMEOUT", "Second-1800");
        upnpServer.send(200, "text/xml; charset=\"utf-8\"", "");
    };
    upnpServer.on("/AVTransport/event", handleEventSub);
    upnpServer.on("/RenderingControl/event", handleEventSub);
    upnpServer.on("/ConnectionManager/event", handleEventSub);

    upnpServer.onNotFound([this, handleEventSub]() {
        if (upnpServer.uri().endsWith("/event")) {
            handleEventSub();
        } else {
            upnpServer.sendHeader("EXT", "");
            upnpServer.send(200, "text/xml; charset=\"utf-8\"", "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body/></s:Envelope>");
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
                static uint32_t lastConnectAttempt = 0;
                if (millis() - lastConnectAttempt >= 300) {
                    lastConnectAttempt = millis();
                    if (streamer->activeStreamUrl.length() > 0) {
                        // Parse URL e.g. http://192.168.1.50:50000/AirMusic.wav
                        String url = streamer->activeStreamUrl;
                        if (url.startsWith("http://")) url = url.substring(7);
                        else if (url.startsWith("https://")) url = url.substring(8);

                        int slashIdx = url.indexOf('/');
                        String hostPort = (slashIdx != -1) ? url.substring(0, slashIdx) : url;
                        String path = (slashIdx != -1) ? url.substring(slashIdx) : "/";

                        int colonIdx = hostPort.indexOf(':');
                        String host = (colonIdx != -1) ? hostPort.substring(0, colonIdx) : hostPort;
                        uint16_t port = (colonIdx != -1) ? hostPort.substring(colonIdx + 1).toInt() : 80;

                        if (streamer->activeStreamClient.connect(host.c_str(), port)) {
                            streamer->activeStreamClient.setTimeout(3000);
                            streamer->activeStreamClient.printf(
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s:%u\r\n"
                                "User-Agent: Q-Tune/1.0 DLNA/1.50\r\n"
                                "Accept: */*\r\n"
                                "Connection: close\r\n\r\n",
                                path.c_str(), host.c_str(), port
                            );

                            // Wait for HTTP response headers
                            uint32_t t0 = millis();
                            while (!streamer->activeStreamClient.available() && streamer->activeStreamClient.connected() && (millis() - t0 < 3000)) {
                                vTaskDelay(pdMS_TO_TICKS(10));
                            }

                            bool isLpcm = false;
                            while (streamer->activeStreamClient.connected()) {
                                String line = streamer->activeStreamClient.readStringUntil('\n');
                                line.trim();
                                if (line.length() == 0) break;
                                line.toLowerCase();
                                if (line.indexOf("audio/l16") != -1 || line.indexOf("audio/pcm") != -1) {
                                    isLpcm = true;
                                }
                            }

                            streamer->streamState = STREAM_PLAYING;
                            streamer->wavHeaderParsed = isLpcm; // If LPCM, no WAV header needed
                            streamer->wavHeaderBytes = 0;
                            if (isLpcm) {
                                streamer->currentSampleRate = 44100;
                                streamer->currentBitsPerSample = 16;
                                audioPlayer.prepareForStream(44100);
                            }
                        }
                    }
                }
            }

            // Ingest incoming audio data into PSRAM ring buffer
            if (streamer->activeStreamClient.connected() && streamer->activeStreamClient.available() > 0) {
                int avail = streamer->activeStreamClient.available();
                int toRead = (avail > (int)sizeof(readChunk)) ? sizeof(readChunk) : avail;
                int bytesRead = streamer->activeStreamClient.read(readChunk, toRead);
                if (bytesRead > 0) {
                    if (!streamer->wavHeaderParsed) {
                        size_t needed = 44 - streamer->wavHeaderBytes;
                        size_t toCopy = (bytesRead < (int)needed) ? (size_t)bytesRead : needed;
                        memcpy(streamer->wavHeaderBuf + streamer->wavHeaderBytes, readChunk, toCopy);
                        streamer->wavHeaderBytes += toCopy;

                        if (streamer->wavHeaderBytes >= 44) {
                            streamer->wavHeaderParsed = true;
                            if (memcmp(streamer->wavHeaderBuf, "RIFF", 4) == 0 &&
                                memcmp(streamer->wavHeaderBuf + 8, "WAVE", 4) == 0) {
                                uint32_t sr = (uint32_t)streamer->wavHeaderBuf[24] |
                                              ((uint32_t)streamer->wavHeaderBuf[25] << 8) |
                                              ((uint32_t)streamer->wavHeaderBuf[26] << 16) |
                                              ((uint32_t)streamer->wavHeaderBuf[27] << 24);
                                uint16_t bits = (uint16_t)streamer->wavHeaderBuf[34] |
                                                ((uint16_t)streamer->wavHeaderBuf[35] << 8);

                                if (sr >= 8000 && sr <= 192000) streamer->currentSampleRate = sr;
                                if (bits == 16 || bits == 24 || bits == 32) streamer->currentBitsPerSample = (uint8_t)bits;

                                audioPlayer.prepareForStream(streamer->currentSampleRate);

                                if (bytesRead > (int)toCopy) {
                                    streamer->pushToRingBuffer(readChunk + toCopy, bytesRead - toCopy);
                                }
                            } else {
                                // Raw PCM / LPCM stream without RIFF header
                                streamer->currentSampleRate = 44100;
                                streamer->currentBitsPerSample = 16;
                                audioPlayer.prepareForStream(44100);
                                streamer->pushToRingBuffer(streamer->wavHeaderBuf, streamer->wavHeaderBytes);
                                if (bytesRead > (int)toCopy) {
                                    streamer->pushToRingBuffer(readChunk + toCopy, bytesRead - toCopy);
                                }
                            }
                        }
                    } else {
                        streamer->pushToRingBuffer(readChunk, bytesRead);
                    }
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
    if (wifiState == WIFI_STATE_OFF) return;

    configServer.handleClient();
    upnpServer.handleClient();
    handleSSDP();

    if (wifiState == WIFI_STATE_AP_MODE || (WiFi.getMode() & WIFI_AP)) {
        dnsServer.processNextRequest();
    }

    uint32_t now = millis();

    // Periodic SSDP announcement every 15 seconds to keep DLNA controllers informed
    if (now - lastSSDPAnnounce >= 15000) {
        lastSSDPAnnounce = now;
        sendSSDPAnnounce();
    }

    // Dynamic Wi-Fi connection monitoring & auto-reconnect
    static uint32_t lastCheck = 0;
    if (now - lastCheck >= 500) {
        lastCheck = now;
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            if (wifiState != WIFI_STATE_CONNECTED) {
                wifiState = WIFI_STATE_CONNECTED;
                disconnectReason = "";
                // Re-announce on SSDP with station IP address
                ssdpUdp.stop();
                ssdpUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
                sendSSDPAnnounce();
                // Re-announce mDNS responder on station interface
                MDNS.begin("q-tunes");
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

const char* WiFiStreamer::getWiFiStateName() const {
    switch (wifiState) {
        case WIFI_STATE_OFF: return "OFF";
        case WIFI_STATE_CONNECTING: return "CONNECTING";
        case WIFI_STATE_CONNECTED: return "CONNECTED";
        case WIFI_STATE_AP_MODE: return "AP_MODE";
        default: return "UNKNOWN";
    }
}

AudioStreamState WiFiStreamer::getStreamState() const { return streamState; }
String WiFiStreamer::getSSID() const { return savedSSID; }
int8_t WiFiStreamer::getRSSI() const { return (wifiState == WIFI_STATE_CONNECTED) ? WiFi.RSSI() : 0; }

String WiFiStreamer::getGatewayIP() const {
    if (wifiState == WIFI_STATE_CONNECTED) {
        return WiFi.gatewayIP().toString();
    }
    return "0.0.0.0";
}

String WiFiStreamer::getSubnetMask() const {
    if (wifiState == WIFI_STATE_CONNECTED) {
        return WiFi.subnetMask().toString();
    }
    return "0.0.0.0";
}

String WiFiStreamer::getMACAddress() const {
    return WiFi.macAddress();
}

uint8_t WiFiStreamer::getChannel() const {
    return (wifiState == WIFI_STATE_CONNECTED) ? (uint8_t)WiFi.channel() : 1;
}

int8_t WiFiStreamer::getSignalQualityPercent() const {
    if (wifiState != WIFI_STATE_CONNECTED) return 0;
    int rssi = WiFi.RSSI();
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return (int8_t)(2 * (rssi + 100));
}
String WiFiStreamer::getTrackTitle() const { return currentTrackTitle; }
String WiFiStreamer::getTrackArtist() const { return currentTrackArtist; }
uint32_t WiFiStreamer::getSampleRate() const { return currentSampleRate; }
uint8_t WiFiStreamer::getBitsPerSample() const { return currentBitsPerSample; }
bool WiFiStreamer::isStreaming() const { return (streamState == STREAM_PLAYING || streamState == STREAM_BUFFERING); }
String WiFiStreamer::getDisconnectReason() const { return disconnectReason; }

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
        u8g2.drawStr(2, 38, "URL: q-tunes.local");
        u8g2.drawStr(2, 47, "IP:  192.168.4.1");

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

        if (disconnectReason.length() > 0) {
            char reasonBuf[32];
            snprintf(reasonBuf, sizeof(reasonBuf), "Err: %s", disconnectReason.c_str());
            if (strlen(reasonBuf) > 23) {
                reasonBuf[20] = '.';
                reasonBuf[21] = '.';
                reasonBuf[22] = '\0';
            }
            u8g2.drawStr(2, 40, reasonBuf);
        } else {
            u8g2.drawStr(2, 40, "URL: q-tunes.local");
        }

        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(2, 53, "SEL:Retry SD  CANCEL:Exit");
    } else {
        // Connected mode (Streaming HUD)
        u8g2.setFont(u8g2_font_5x7_tr);

        // Network line: mDNS host and signal strength
        char netBuf[32];
        snprintf(netBuf, sizeof(netBuf), "q-tunes.local (%ddBm)", WiFi.RSSI());
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

    uint32_t now = millis();
    if (now - lastUIRenderMs >= 33) { // Cap OLED refresh at ~30 FPS to save CPU for audio & network
        lastUIRenderMs = now;
        renderUI();
    }

    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        ledManager.triggerButtonPulse(CRGB::Red, 1, 60);
        return false; // Exit back to main menu
    } else if (evt == BTN_EVENT_UP_PRESS || evt == BTN_EVENT_UP_HOLD) {
        audioPlayer.volumeUp(5);
        ledManager.triggerButtonPulse(CRGB::Green, 1, 40);
        renderUI();
    } else if (evt == BTN_EVENT_DN_PRESS || evt == BTN_EVENT_DN_HOLD) {
        audioPlayer.volumeDown(5);
        ledManager.triggerButtonPulse(CRGB::Red, 1, 40);
        renderUI();
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
        renderUI();
    } else if (evt == BTN_EVENT_SEL_HOLD) {
        // Toggle Audio Output between I2S Speaker and FiiO KA11 USB DAC
        AudioOutputMode cur = audioPlayer.getOutputMode();
        AudioOutputMode nextMode = (cur == OUTPUT_MODE_SPEAKER_I2S) ? OUTPUT_MODE_FIIO_USB_DAC : OUTPUT_MODE_SPEAKER_I2S;
        audioPlayer.setOutputMode(nextMode);
        ledManager.triggerButtonPulse(CRGB::Magenta, 2, 80);
        renderUI();
    }

    return true;
}
