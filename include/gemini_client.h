#ifndef GEMINI_CLIENT_H
#define GEMINI_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#include "ai_companion.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "hw_config.h"
#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "led_manager.h"
#include "battery.h"
#include "sd_manager.h"
#include "player_config.h"
#else
#include <string>
#include <cstring>
#include <cctype>

class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(float v) : std::string(std::to_string(v)) {}
    int indexOf(char c, size_t fromIdx = 0) const {
        size_t pos = find(c, fromIdx);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const char* s, size_t fromIdx = 0) const {
        size_t pos = find(s, fromIdx);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const String& s, size_t fromIdx = 0) const {
        size_t pos = find(s, fromIdx);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    String substring(size_t start, size_t end = std::string::npos) const {
        if (start >= length()) return String("");
        if (end == std::string::npos || end > length()) return String(substr(start));
        return String(substr(start, end - start));
    }
    void trim() {
        size_t first = find_first_not_of(" \t\r\n");
        if (first == std::string::npos) { clear(); return; }
        size_t last = find_last_not_of(" \t\r\n");
        *this = substr(first, (last - first + 1));
    }
    void toLowerCase() {
        for (char& c : *this) c = tolower(c);
    }
    void toUpperCase() {
        for (char& c : *this) c = toupper(c);
    }
    bool startsWith(const char* prefix) const {
        return rfind(prefix, 0) == 0;
    }
    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        size_t slen = strlen(suffix);
        if (slen > length()) return false;
        return compare(length() - slen, slen, suffix) == 0;
    }
    bool equalsIgnoreCase(const char* s) const {
        if (!s) return false;
        if (length() != strlen(s)) return false;
        for (size_t i = 0; i < length(); i++) {
            if (tolower((*this)[i]) != tolower(s[i])) return false;
        }
        return true;
    }
    bool equalsIgnoreCase(const String& s) const {
        return equalsIgnoreCase(s.c_str());
    }
    int toInt() const {
        try { return std::stoi(*this); } catch (...) { return 0; }
    }
    void replace(const char* from, const char* to) {
        if (!from || !to) return;
        size_t pos = 0;
        size_t fromLen = strlen(from);
        size_t toLen = strlen(to);
        while ((pos = find(from, pos)) != std::string::npos) {
            std::string::replace(pos, fromLen, to);
            pos += toLen;
        }
    }
    char charAt(size_t idx) const {
        return (idx < length()) ? (*this)[idx] : 0;
    }
};
#endif

class GeminiClient {
public:
    GeminiClient();
    bool begin();

    // API Key Management (SD Card / NVS)
    bool hasApiKey() const;
    const char* getApiKey() const { return api_key; }
    void setApiKey(const char* key);
    bool loadSDConfig();

    // Model selection
    const char* getModelName() const { return model_name; }
    void setModelName(const char* model);

    // Context & State Serialization
    String buildSystemStateJson() const;
    String buildPrompt(const char* userQuery) const;

    // Chat Execution
    bool ask(const char* query, String &outMessage, String &outEmote, String &outAction);
    bool askVoice(const int16_t* pcmSamples, size_t sampleCount, String &outMessage, String &outEmote, String &outAction);

    // Command Parsing & Execution
    bool parseResponse(const String &rawResponse, String &outEmote, String &outAction, String &outMessage);
    bool executeAction(const String &action);

    // Status
    const char* getLastError() const { return last_error; }
    bool isBusy() const { return is_busy; }

private:
    char api_key[128];
    char model_name[32];
    char last_error[64];
    bool is_busy;

    void setError(const char* err);
    String sendHttpsRequest(const String &endpoint, const String &jsonPayload);
};

extern GeminiClient geminiClient;

#endif // GEMINI_CLIENT_H
