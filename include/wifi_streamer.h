#ifndef WIFI_STREAMER_H
#define WIFI_STREAMER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include "hw_config.h"

enum WiFiStreamState {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE
};

enum AudioStreamState {
    STREAM_IDLE = 0,
    STREAM_BUFFERING,
    STREAM_PLAYING,
    STREAM_PAUSED
};

class WiFiStreamer {
public:
    WiFiStreamer();
    bool begin();
    bool start();
    void stop();
    void update();
    bool updateUI();

    // Stream Controls
    void pauseStream();
    void resumeStream();
    void stopStream();

    // State & Status
    WiFiStreamState getWiFiState() const;
    AudioStreamState getStreamState() const;
    String getIPAddress() const;
    String getSSID() const;
    int8_t getRSSI() const;
    String getTrackTitle() const;
    String getTrackArtist() const;
    uint32_t getSampleRate() const;
    uint8_t getBitsPerSample() const;
    uint8_t getBufferFillPercentage() const;
    bool isStreaming() const;

    // Credentials & Mode Management
    void setCredentials(const String &ssid, const String &password);
    void startAPMode();
    void reconnect();

private:
    WiFiStreamState wifiState;
    AudioStreamState streamState;
    Preferences prefs;

    String savedSSID;
    String savedPass;
    String currentTrackTitle;
    String currentTrackArtist;
    uint32_t currentSampleRate;
    uint8_t currentBitsPerSample;

    // Network Servers
    WebServer configServer;   // Port 80: Web Configuration & Status Portal
    WebServer upnpServer;     // Port 8080: UPnP / DLNA SOAP & XML Server
    WiFiUDP ssdpUdp;          // Port 1900: SSDP Multicast Receiver

    // PSRAM Ring Buffer
    uint8_t *streamRingBuffer;
    size_t ringBufferSize;
    volatile size_t ringHead;
    volatile size_t ringTail;
    volatile size_t ringCount;
    SemaphoreHandle_t ringMutex;

    // FreeRTOS Background Stream Task
    TaskHandle_t streamTaskHandle;
    volatile bool streamTaskRunning;

    // Stream Client / Socket connection for active DLNA stream
    WiFiClient activeStreamClient;
    String activeStreamUrl;
    volatile bool hasActiveStream;

    // Internal UPnP & HTTP Handlers
    void setupConfigServer();
    void setupUpnpServer();
    void handleSSDP();
    void sendSSDPAnnounce();
    void processStreamData();
    bool pushToRingBuffer(const uint8_t *data, size_t length);
    size_t pullFromRingBuffer(uint8_t *dest, size_t maxLen);

    // FreeRTOS Task function
    static void streamTaskFunction(void *param);

    // UI Rendering
    void renderUI();
};

extern WiFiStreamer wifiStreamer;

#endif // WIFI_STREAMER_H
