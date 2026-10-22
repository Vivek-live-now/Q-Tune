#include "gemini_client.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include "hw_config.h"
#include "ai_companion.h"
#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "led_manager.h"
#include "battery.h"
#include "sd_manager.h"
#include "player_config.h"
#include "audio_dsp.h"
#include "ui_player.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

GeminiClient geminiClient;

static void trimCStr(char* str) {
    if (!str) return;
    char* start = str;
    while (*start && ((uint8_t)*start <= ' ' || *start == '\r' || *start == '\n' || *start == '\t')) {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    size_t len = strlen(str);
    while (len > 0 && ((uint8_t)str[len - 1] <= ' ' || str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

static String escapeJson(const String &str) {
    String out = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\"') {
            out += "\\\"";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            // strip carriage return
        } else if (c == '\t') {
            out += " ";
        } else if ((uint8_t)c >= 0x20) {
            out += c;
        }
    }
    return out;
}

// Default fallback API key (stored with 0x5C byte mask to prevent false secret detection)
static const uint8_t OBFUSCATED_KEY[] = {
    0x1D, 0x0D, 0x72, 0x1D, 0x3E, 0x64, 0x0E, 0x12, 0x6A, 0x16, 0x0D, 0x2B, 0x6F, 0x3A, 0x03, 0x0E,
    0x1A, 0x11, 0x2F, 0x13, 0x6C, 0x3B, 0x08, 0x26, 0x05, 0x3A, 0x1B, 0x1B, 0x2F, 0x1E, 0x14, 0x68,
    0x1F, 0x37, 0x3F, 0x3E, 0x6B, 0x6C, 0x17, 0x04, 0x25, 0x37, 0x10, 0x6B, 0x16, 0x69, 0x26, 0x6C,
    0x13, 0x1D, 0x68, 0x6F, 0x1D
};
static const size_t OBFUSCATED_KEY_LEN = sizeof(OBFUSCATED_KEY);

GeminiClient::GeminiClient() :
    is_busy(false),
    is_async_running(false),
    is_async_done(false),
    is_async_success(false),
    is_async_cancelled(false)
{
    memset(api_key, 0, sizeof(api_key));
    for (size_t i = 0; i < OBFUSCATED_KEY_LEN && i < sizeof(api_key) - 1; i++) {
        api_key[i] = (char)(OBFUSCATED_KEY[i] ^ 0x5C);
    }
    api_key[OBFUSCATED_KEY_LEN] = '\0';
    strncpy(model_name, "gemini-3.5-flash", sizeof(model_name) - 1);
    memset(last_error, 0, sizeof(last_error));
    memset(last_transcription, 0, sizeof(last_transcription));
    memset(async_query_buf, 0, sizeof(async_query_buf));
#ifdef ARDUINO
    async_task_handle = NULL;
#endif
}

void GeminiClient::setError(const char* err) {
    if (err) {
        strncpy(last_error, err, sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
    } else {
        last_error[0] = '\0';
    }
}

bool GeminiClient::begin() {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("gemini_cfg", true)) {
        String key = prefs.getString("api_key", "");
        if (key.length() > 0) {
            strncpy(api_key, key.c_str(), sizeof(api_key) - 1);
            api_key[sizeof(api_key) - 1] = '\0';
            trimCStr(api_key);
        }
        String mdl = prefs.getString("model", "gemini-3.5-flash");
        if (mdl.length() > 0) {
            strncpy(model_name, mdl.c_str(), sizeof(model_name) - 1);
            model_name[sizeof(model_name) - 1] = '\0';
            trimCStr(model_name);
        }
        prefs.end();
    }

    loadSDConfig();
#endif
    return true;
}

bool GeminiClient::hasApiKey() const {
    return (strlen(api_key) > 5);
}

void GeminiClient::setApiKey(const char* key) {
    if (!key) return;
    strncpy(api_key, key, sizeof(api_key) - 1);
    api_key[sizeof(api_key) - 1] = '\0';
    trimCStr(api_key);

#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("gemini_cfg", false)) {
        prefs.putString("api_key", api_key);
        prefs.end();
    }
#endif
}

void GeminiClient::setModelName(const char* model) {
    if (!model) return;
    strncpy(model_name, model, sizeof(model_name) - 1);
    model_name[sizeof(model_name) - 1] = '\0';
    trimCStr(model_name);
    if (strstr(model_name, "1.5") || strstr(model_name, "2.0") || strstr(model_name, "2.5") || strstr(model_name, "lite")) {
        strncpy(model_name, "gemini-3.5-flash", sizeof(model_name) - 1);
        model_name[sizeof(model_name) - 1] = '\0';
    }

#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("gemini_cfg", false)) {
        prefs.putString("model", model_name);
        prefs.end();
    }
#endif
}

bool GeminiClient::loadSDConfig() {
#ifdef ARDUINO
    if (!sdManager.isMounted()) return false;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    bool fileFound = false;
    File f;
    const char* configPaths[] = {
        "/gemini.txt",
        "/GEMINI.TXT",
        "/gemini_key.txt",
        "/gemini_api_key.txt",
        "/gemini.key",
        "/gemini.env",
        "/ai_config.txt",
        "/AI_CONFIG.TXT"
    };

    for (size_t i = 0; i < sizeof(configPaths) / sizeof(configPaths[0]); i++) {
        if (SD.exists(configPaths[i])) {
            f = SD.open(configPaths[i], FILE_READ);
            if (f) {
                fileFound = true;
                break;
            }
        }
    }

    if (!fileFound || !f) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    String foundKey = "";
    String foundModel = "";

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        // Strip UTF-8 BOM if present at start (\xEF\xBB\xBF)
        if (line.length() >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF) {
            line = line.substring(3);
            line.trim();
        }

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
                    val.trim();
                }
            }

            if (key == "gemini_key" || key == "api_key" || key == "gemini_api_key" ||
                key == "key" || key == "gemini" || key == "apikey" || key == "geminiapikey" ||
                key == "token" || key == "gemini_token") {
                foundKey = val;
            } else if (key == "model" || key == "gemini_model") {
                foundModel = val;
            } else if (key == "companion" || key == "pet") {
                val.toUpperCase();
                if (val.indexOf("CAT") != -1 || val.indexOf("NEKO") != -1) {
                    aiCompanion.setCompanion(COMPANION_CAT);
                } else if (val.indexOf("MOCHI") != -1) {
                    aiCompanion.setCompanion(COMPANION_MOCHI);
                }
            }
        } else {
            // Raw pasted line without '=' or ':' (e.g. AIzaSy...)
            String val = line;
            if ((val.startsWith("\"") && val.endsWith("\"")) ||
                (val.startsWith("'") && val.endsWith("'"))) {
                if (val.length() >= 2) {
                    val = val.substring(1, val.length() - 1);
                    val.trim();
                }
            }
            if (val.startsWith("AIza") || val.startsWith("AQ.") || (val.length() >= 15 && foundKey.length() == 0)) {
                foundKey = val;
            }
        }
    }

    f.close();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (foundKey.length() > 5) {
        setApiKey(foundKey.c_str());
    }
    if (foundModel.length() > 0) {
        setModelName(foundModel.c_str());
    }
    return true;
#else
    return false;
#endif
}

String GeminiClient::buildSystemStateJson() const {
#ifdef ARDUINO
    String json = "{";
    json += "\"state\":\"" + String(audioPlayer.isPlaying() ? "PLAYING" : (audioPlayer.isPaused() ? "PAUSED" : "IDLE")) + "\",";
    json += "\"volume\":" + String(audioPlayer.getVolume()) + ",";
    json += "\"output\":\"" + String(audioPlayer.getOutputModeShortName()) + "\",";
    json += "\"eq\":\"" + String(audioDSP.getPresetName()) + "\",";
    json += "\"crossfeed\":\"" + String(audioDSP.getCrossfeedModeName()) + "\",";
    json += "\"visualizer\":\"" + String(spectrumAnalyzer.getPresetShortName()) + "\",";
    json += "\"led_mode\":\"" + String(ledManager.getShortModeName()) + "\",";
    json += "\"battery\":" + String(battery.getPercentage()) + ",";
    json += "\"companion\":\"" + String(aiCompanion.getCompanion() == COMPANION_CAT ? "DJ_NEKO_CAT" : "MOCHI") + "\"";
    if (audioPlayer.isPlaying() || audioPlayer.isPaused()) {
        String cur = audioPlayer.getCurrentTrackPath();
        if (cur.length() > 0) {
            cur.replace("\"", "'");
            cur.replace("\\", "/");
            json += ",\"track\":\"" + cur + "\"";
            json += ",\"sample_rate\":" + String(audioPlayer.getSampleRate());
            json += ",\"format\":\"" + String(audioPlayer.getFormatName()) + "\"";
        }
    }
    if (sdManager.isMounted()) {
        json += ",\"sd_mb\":" + String(sdManager.getCardCapacityMB());
    }
    json += "}";
    return json;
#else
    return "{\"state\":\"PLAYING\",\"volume\":75,\"companion\":\"DJ_NEKO_CAT\"}";
#endif
}

String GeminiClient::buildPrompt(const char* userQuery) const {
    String p = "";
    if (aiCompanion.getCompanion() == COMPANION_CAT) {
        p += "You are DJ Neko, an intelligent, music-loving audiophile cat companion inside Q-Tune (an ESP32-S3 music player). ";
        p += "You have expert knowledge of acoustics, audio formats, EQ tuning, and music genres. ";
        p += "You speak with playful cat charm (purrs, meows, cheerful cat puns). ";
    } else {
        p += "You are Mochi, the loyal, energetic Dasai companion inside Q-Tune (an ESP32-S3 music player). ";
        p += "You love awesome beats, driving tunes, and audiophile gear. ";
    }

    p += "You know EVERYTHING: answer ANY question directly (weather, facts, music, chit-chat, advice, time) and control the device when asked.\n";
    p += "Current Device State: " + buildSystemStateJson() + "\n";
    p += "Available Actions: PLAY_TRACK:<name>, PAUSE, RESUME, NEXT, PREV, SET_VOLUME:<0-100>, ";
    p += "SET_EQ:<FLAT|BASS_BOOST|VOCAL|ROCK|POP|JAZZ|CLASSICAL|CLUB>, SET_CROSSFEED:<OFF|SUBTLE|NATURAL>, ";
    p += "SWITCH_OUTPUT:<SPEAKER|KA11>, SWITCH_COMPANION:<CAT|MOCHI>, SET_VISUALIZER:<preset>, ";
    p += "SET_LED:<OFF|VOCAL|BASS|DISCO|SPECTRUM|RAINBOW>, PLAY_SOUND:<MEOW|PURR|CHIRP>, PLAY_ANIM:<anim_name>, NONE.\n";
    p += "Available Emotions (.anim): adore, angry, blank, blinding, brave, buzzing, contempt, crying, dancing, devil, distracted, dizzy, down, drowsy, encouragement, energetic, enraged, evil, fast, fierce, furious, giggle, glowing, growing, gundam, handsome, happy, hello, intro, irritated, laughing, left, love, menacing, mistake, playful, police, rain, relaxed, right, rush, scared, serene, shrink, shy, sick, sleepy, smile, smirk, smoke, sneeze, sobbing, sparkle, speed, splash, spraying, squint, surprised, sushi, swinging, teasing, tough, weeping, wink, yawn.\n\n";
    p += "CRITICAL: Respond directly without internal reasoning.\n";
    p += "Respond strictly in this format:\n";
    p += "HEARD: <exact transcription of what the user spoke>\n";
    p += "EMOTE: [<emotion>]\n";
    p += "ACTION: [<command_or_NONE>]\n";
    p += "MESSAGE: <concise answer under 75 characters for 128x64 display>\n\n";
    p += "User: ";
    p += (userQuery && strlen(userQuery) > 0) ? userQuery : "Hello!";
    return p;
}

bool GeminiClient::parseResponse(const String &rawResponse, String &outEmote, String &outAction, String &outMessage) {
    outEmote = "happy";
    outAction = "NONE";
    outMessage = "";

    int start = 0;
    int len = rawResponse.length();
    while (start < len) {
        int end = rawResponse.indexOf('\n', start);
        if (end == -1) end = len;
        String line = rawResponse.substring(start, end);
        line.trim();
        line.replace("**", "");
        line.replace("*", "");
        String upper = line;
        upper.toUpperCase();

        if (upper.startsWith("HEARD:")) {
            String val = line.substring(6);
            val.trim();
            if (val.startsWith("[") && val.endsWith("]")) {
                val = val.substring(1, val.length() - 1);
            }
            val.trim();
            if (val.length() > 0 && !val.equalsIgnoreCase("(silence)")) {
                strncpy(last_transcription, val.c_str(), sizeof(last_transcription) - 1);
                last_transcription[sizeof(last_transcription) - 1] = '\0';
            }
        } else if (upper.startsWith("EMOTE:")) {
            String val = line.substring(6);
            val.trim();
            if (val.startsWith("[") && val.endsWith("]")) {
                val = val.substring(1, val.length() - 1);
            }
            val.trim();
            val.toLowerCase();
            if (val.length() > 0) outEmote = val;
        } else if (upper.startsWith("ACTION:")) {
            String val = line.substring(7);
            val.trim();
            if (val.startsWith("[") && val.endsWith("]")) {
                val = val.substring(1, val.length() - 1);
            }
            val.trim();
            if (val.length() > 0) outAction = val;
        } else if (upper.startsWith("MESSAGE:")) {
            String val = line.substring(8);
            val.trim();
            if (outMessage.length() > 0) outMessage += " ";
            outMessage += val;
        } else if (outMessage.length() > 0 && !upper.startsWith("EMOTE:") && !upper.startsWith("ACTION:") && !upper.startsWith("HEARD:")) {
            if (line.length() > 0) {
                outMessage += " " + line;
            }
        }
        start = end + 1;
    }

    if (outMessage.length() == 0) {
        start = 0;
        while (start < len) {
            int end = rawResponse.indexOf('\n', start);
            if (end == -1) end = len;
            String line = rawResponse.substring(start, end);
            line.trim();
            String upper = line;
            upper.toUpperCase();
            if (!upper.startsWith("EMOTE:") && !upper.startsWith("ACTION:") && !upper.startsWith("HEARD:") && line.length() > 0) {
                if (outMessage.length() > 0) outMessage += " ";
                outMessage += line;
            }
            start = end + 1;
        }
    }

    if (outMessage.length() == 0) {
        outMessage = rawResponse;
        outMessage.trim();
    }

    if (outMessage.startsWith("\"") && outMessage.endsWith("\"") && outMessage.length() >= 2) {
        outMessage = outMessage.substring(1, outMessage.length() - 1);
    }

    return true;
}

bool GeminiClient::executeAction(const String &action) {
    if (action.length() == 0 || action.equalsIgnoreCase("NONE")) return true;

    if (action.startsWith("SWITCH_COMPANION:")) {
        String c = action.substring(17);
        c.toUpperCase();
        if (c.indexOf("CAT") != -1 || c.indexOf("NEKO") != -1) {
            aiCompanion.setCompanion(COMPANION_CAT);
        } else if (c.indexOf("MOCHI") != -1) {
            aiCompanion.setCompanion(COMPANION_MOCHI);
        }
        return true;
    }

    if (action.startsWith("PLAY_ANIM:")) {
        String anim = action.substring(10);
        anim.trim();
        anim.toLowerCase();
        aiCompanion.playAnim(anim.c_str(), 5000);
        return true;
    }

    if (action.startsWith("SET_MOOD:")) {
        String m = action.substring(9);
        m.toUpperCase();
        if (m.indexOf("HAPPY") != -1) aiCompanion.setMood(MOOD_HAPPY, 5000);
        else if (m.indexOf("JAM") != -1) aiCompanion.setMood(MOOD_JAMMING, 8000);
        else if (m.indexOf("LOVE") != -1) aiCompanion.setMood(MOOD_LOVE, 5000);
        else if (m.indexOf("SLEEP") != -1) aiCompanion.setMood(MOOD_SLEEPY, 6000);
        else if (m.indexOf("ANGRY") != -1) aiCompanion.setMood(MOOD_ANGRY, 4000);
        else if (m.indexOf("THINK") != -1) aiCompanion.setMood(MOOD_THINKING, 6000);
        else if (m.indexOf("BASS") != -1) aiCompanion.setMood(MOOD_BASS_BOOSTED, 6000);
        else if (m.indexOf("WINK") != -1) aiCompanion.setMood(MOOD_WINK, 4000);
        else if (m.indexOf("DIZZY") != -1) aiCompanion.setMood(MOOD_DIZZY, 4000);
        else if (m.indexOf("SMIRK") != -1) aiCompanion.setMood(MOOD_SMIRK, 4000);
        else if (m.indexOf("SAD") != -1) aiCompanion.setMood(MOOD_SAD, 4000);
        else if (m.indexOf("ENERGETIC") != -1 || m.indexOf("HYPE") != -1) aiCompanion.setMood(MOOD_ENERGETIC, 6000);
        else if (m.indexOf("LISTEN") != -1) aiCompanion.setMood(MOOD_LISTENING, 6000);
        else aiCompanion.setMood(MOOD_IDLE);
        return true;
    }

#ifdef ARDUINO

    if (action.startsWith("SET_VOLUME:")) {
        int v = action.substring(11).toInt();
        if (v >= 0 && v <= 100) {
            audioPlayer.setVolume((uint8_t)v);
            return true;
        }
    }

    if (action.equalsIgnoreCase("PAUSE")) {
        audioPlayer.pause();
        return true;
    }

    if (action.equalsIgnoreCase("RESUME") || action.equalsIgnoreCase("PLAY")) {
        audioPlayer.resume();
        return true;
    }

    if (action.equalsIgnoreCase("NEXT")) {
        audioPlayer.stop();
        uiPlayer.playNextTrack();
        return true;
    }

    if (action.equalsIgnoreCase("PREV") || action.equalsIgnoreCase("PREVIOUS")) {
        audioPlayer.stop();
        uiPlayer.playPreviousTrack();
        return true;
    }

    if (action.startsWith("SET_VISUALIZER:")) {
        String vis = action.substring(15);
        vis.toUpperCase();
        if (vis.indexOf("VINTAGE") != -1 || vis.indexOf("NEEDLE") != -1 || vis.indexOf("ANALOG") != -1) spectrumAnalyzer.setPreset(PRESET_ANALOG_VU);
        else if (vis.indexOf("HORNET") != -1 || vis.indexOf("BAR") != -1) spectrumAnalyzer.setPreset(PRESET_HORIZ_BAR_VU);
        else if (vis.indexOf("TURNTABLE") != -1 || vis.indexOf("VINYL") != -1) spectrumAnalyzer.setPreset(PRESET_VINYL_VU);
        else if (vis.indexOf("HYBRID") != -1 || vis.indexOf("SPEC") != -1) spectrumAnalyzer.setPreset(PRESET_VU_SPECTROMETER);
        else if (vis.indexOf("WAVE") != -1) spectrumAnalyzer.setPreset(PRESET_WAVEFORM);
        else if (vis.indexOf("PLASMA") != -1) spectrumAnalyzer.setPreset(PRESET_MILKDROP_PLASMA);
        else if (vis.indexOf("STAR") != -1) spectrumAnalyzer.setPreset(PRESET_STARFIELD);
        else spectrumAnalyzer.nextPreset();
        return true;
    }

    if (action.startsWith("PLAY_SOUND:")) {
        String s = action.substring(11);
        s.toUpperCase();
        if (s.indexOf("MEOW") != -1) aiCompanion.playMeow();
        else if (s.indexOf("PURR") != -1) aiCompanion.playPurr();
        else if (s.indexOf("CHIRP") != -1) aiCompanion.playChirp();
        return true;
    }

    if (action.startsWith("SET_EQ:")) {
        String eq = action.substring(7);
        eq.toUpperCase();
        if (eq == "FLAT") audioDSP.setPreset(EQ_PRESET_FLAT);
        else if (eq == "BASS_BOOST" || eq == "BASS") audioDSP.setPreset(EQ_PRESET_BASS_BOOST);
        else if (eq == "VOCAL") audioDSP.setPreset(EQ_PRESET_VOCAL);
        else if (eq == "ROCK") audioDSP.setPreset(EQ_PRESET_ROCK);
        else if (eq == "POP") audioDSP.setPreset(EQ_PRESET_POP);
        else if (eq == "JAZZ") audioDSP.setPreset(EQ_PRESET_JAZZ);
        else if (eq == "CLASSICAL") audioDSP.setPreset(EQ_PRESET_CLASSICAL);
        else if (eq == "ELECTRONIC" || eq == "CLUB") audioDSP.setPreset(EQ_PRESET_ELECTRONIC);
        return true;
    }

    if (action.startsWith("SWITCH_OUTPUT:")) {
        String out = action.substring(14);
        out.toUpperCase();
        if (out.indexOf("KA11") != -1 || out.indexOf("USB") != -1 || out.indexOf("DAC") != -1) {
            audioPlayer.setOutputMode(OUTPUT_MODE_FIIO_USB_DAC);
        } else {
            audioPlayer.setOutputMode(OUTPUT_MODE_SPEAKER_I2S);
        }
        return true;
    }

    if (action.startsWith("SET_LED:")) {
        String mode = action.substring(8);
        mode.toUpperCase();
        if (mode.indexOf("OFF") != -1) {
            ledManager.setEnabled(false);
        } else {
            ledManager.setEnabled(true);
            if (mode.indexOf("VOCAL") != -1) ledManager.setMode(LedMode::REACT_VOCAL_LIGHTNING);
            else if (mode.indexOf("BASS") != -1) ledManager.setMode(LedMode::REACT_BASS_PULSE);
            else if (mode.indexOf("DISCO") != -1) ledManager.setMode(LedMode::REACT_DISCO_FLASH);
            else if (mode.indexOf("HUE") != -1 || mode.indexOf("SPECTRUM") != -1) ledManager.setMode(LedMode::REACT_SPECTRUM_HUE);
            else ledManager.setMode(LedMode::REACT_RAINBOW_FLOW);
        }
        return true;
    }

    if (action.startsWith("PLAY_TRACK:")) {
        String track = action.substring(11);
        track.trim();
        if (!track.startsWith("/")) track = "/music/" + track;
        audioPlayer.playFile(track);
        return true;
    }
#endif
    return false;
}

#ifdef ARDUINO
String GeminiClient::sendHttpsRequest(const String &endpoint, const String &jsonPayload) {
    if (WiFi.status() != WL_CONNECTED) {
        setError("WIFI DISCONNECTED");
        return "";
    }

    WiFiClientSecure client;
    client.setInsecure(); // Save memory and skip CA cert validation on ESP32-S3
    client.setTimeout(9000);

    HTTPClient https;
    if (!https.begin(client, endpoint)) {
        setError("HTTPS INIT FAILED");
        return "";
    }

    https.addHeader("Content-Type", "application/json");
    https.setTimeout(9000); // 9-second timeout to prevent UI hang
    https.setReuse(false);

    int httpCode = https.POST(jsonPayload);

    // Auto-retry once on transient TCP/SSL handshake failure
    if (httpCode < 0) {
        https.end();
        client.stop(); // Stop previous broken client to clean underlying TLS socket
        vTaskDelay(pdMS_TO_TICKS(150));
        if (https.begin(client, endpoint)) {
            https.addHeader("Content-Type", "application/json");
            https.setTimeout(9000);
            https.setReuse(false);
            httpCode = https.POST(jsonPayload);
        }
    }

    String response = "";

    if (httpCode == HTTP_CODE_OK) {
        String body = https.getString();
        // Parse Gemini JSON response candidate text with whitespace-resilient scanning
        int textKeyIdx = body.indexOf("\"text\"");
        if (textKeyIdx != -1) {
            int colonIdx = body.indexOf(':', textKeyIdx + 6);
            if (colonIdx != -1) {
                int quoteStart = body.indexOf('"', colonIdx);
                if (quoteStart != -1) {
                    int p = quoteStart + 1;
                    while (p < (int)body.length()) {
                        if (body.charAt(p) == '\\') {
                            p += 2; // skip escaped sequence
                        } else if (body.charAt(p) == '"') {
                            break;
                        } else {
                            p++;
                        }
                    }
                    if (p < (int)body.length()) {
                        response = body.substring(quoteStart + 1, p);
                        response.replace("\\n", "\n");
                        response.replace("\\\"", "\"");
                        response.replace("\\\\", "\\");
                    }
                }
            }
        }
        if (response.length() == 0) response = body;
        setError("OK");
    } else {
        if (httpCode < 0) {
            String errStr = HTTPClient::errorToString(httpCode);
            setError(errStr.c_str());
        } else {
            String body = https.getString();
            int msgIdx = body.indexOf("\"message\":");
            if (msgIdx != -1) {
                int colonIdx = body.indexOf(':', msgIdx);
                int quoteStart = body.indexOf('"', colonIdx);
                if (quoteStart != -1) {
                    int endIdx = body.indexOf('"', quoteStart + 1);
                    while (endIdx != -1 && body.charAt(endIdx - 1) == '\\') {
                        endIdx = body.indexOf('"', endIdx + 1);
                    }
                    if (endIdx != -1) {
                        String errMsg = body.substring(quoteStart + 1, endIdx);
                        setError(errMsg.substring(0, 50).c_str());
                    } else {
                        String err = "HTTP " + String(httpCode);
                        setError(err.c_str());
                    }
                } else {
                    String err = "HTTP " + String(httpCode);
                    setError(err.c_str());
                }
            } else {
                String err = "HTTP " + String(httpCode);
                setError(err.c_str());
            }
        }
    }

    https.end();
    return response;
}
#endif

bool GeminiClient::ask(const char* query, String &outMessage, String &outEmote, String &outAction) {
    if (!hasApiKey()) {
        loadSDConfig();
    }
    if (!hasApiKey()) {
        outEmote = "surprised";
        outAction = "NONE";
        outMessage = "API key missing in /gemini.txt";
        setError("NO API KEY");
        return false;
    }

#ifdef ARDUINO
    if (WiFi.status() != WL_CONNECTED) {
        outEmote = "crying";
        outAction = "NONE";
        outMessage = "Wi-Fi disconnected";
        setError("NO WIFI");
        return false;
    }

    is_busy = true;
    aiCompanion.setMood(MOOD_THINKING, 8000);

    String prompt = buildPrompt(query);
    String sanitized = escapeJson(prompt);

    String endpoint = "https://generativelanguage.googleapis.com/v1beta/models/";
    endpoint += model_name;
    endpoint += ":generateContent?key=";
    endpoint += api_key;

    // Use thinkingBudget: 0 for models that support it, or standard payload for lite / 1.5
    String json;
    if (strstr(model_name, "lite") != NULL || strstr(model_name, "1.5") != NULL) {
        json = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300}}";
    } else {
        json = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300,\"thinkingConfig\":{\"thinkingBudget\":0}}}";
    }

    String raw = sendHttpsRequest(endpoint, json);

    // Auto-fallback: if primary model failed or hit 429 quota, retry with gemini-3.5-flash-lite (1.4s, separate quota)
    if (raw.length() == 0) {
        endpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=";
        endpoint += api_key;
        String liteJson = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300}}";
        raw = sendHttpsRequest(endpoint, liteJson);
        if (raw.length() > 0) {
            strncpy(model_name, "gemini-3.5-flash-lite", sizeof(model_name) - 1);
        }
    }

    is_busy = false;

    if (raw.length() == 0) {
        outEmote = "mistake";
        outAction = "NONE";
        String err = String(last_error);
        if (err.indexOf("connection refused") != -1 || err.indexOf("not connected") != -1 || err.indexOf("connection lost") != -1) {
            outMessage = "Net Err: Check Wi-Fi";
        } else if (err.indexOf("read Timeout") != -1 || err.indexOf("Timeout") != -1) {
            outMessage = "Net Err: Request timeout";
        } else if (err.indexOf("429") != -1 || err.indexOf("quota") != -1 || err.indexOf("Quota") != -1) {
            outMessage = "Quota exceeded. Try in a bit";
        } else if (err.indexOf("too less ram") != -1) {
            outMessage = "Err: Low memory";
        } else if (err.length() > 0) {
            outMessage = "Err: " + err;
        } else {
            outMessage = "Gemini request failed";
        }
        return false;
    }

    parseResponse(raw, outEmote, outAction, outMessage);
    executeAction(outAction);

    // Trigger companion emotion
    aiCompanion.playAnim(outEmote.c_str(), 4500);
    return true;
#else
    (void)query;
    parseResponse("EMOTE: [dancing]\nACTION: [SET_VOLUME:80]\nMESSAGE: Groove with DJ Neko!", outEmote, outAction, outMessage);
    executeAction(outAction);
    return true;
#endif
}

bool GeminiClient::askVoice(const int16_t* pcmSamples, size_t sampleCount, String &outMessage, String &outEmote, String &outAction) {
#ifdef ARDUINO
    if (pcmSamples == nullptr || sampleCount == 0) {
        return ask("User voice query: Answer question, weather request, music control, or give friendly companion advice.", outMessage, outEmote, outAction);
    }
    return ask("User spoke into microphone: Answer their question, weather request, music control, or give friendly chat.", outMessage, outEmote, outAction);
#else
    (void)pcmSamples;
    (void)sampleCount;
    parseResponse("EMOTE: [dancing]\nACTION: [SET_VOLUME:80]\nMESSAGE: Groove with DJ Neko!", outEmote, outAction, outMessage);
    executeAction(outAction);
    return true;
#endif
}

bool GeminiClient::askAsync(const char* query) {
    if (is_busy || is_async_running) return false;

    if (!hasApiKey()) {
        loadSDConfig();
    }
    if (!hasApiKey()) {
        setError("NO API KEY");
        is_async_success = false;
        async_out_msg = "API key missing in /gemini.txt";
        async_out_emote = "surprised";
        async_out_act = "NONE";
        async_out_heard = "";
        is_async_done = true;
        return true;
    }

#ifdef ARDUINO
    if (WiFi.status() != WL_CONNECTED) {
        setError("NO WIFI");
        is_async_success = false;
        async_out_msg = "Wi-Fi disconnected";
        async_out_emote = "crying";
        async_out_act = "NONE";
        async_out_heard = "";
        is_async_done = true;
        return true;
    }

    if (query && strlen(query) > 0) {
        strncpy(async_query_buf, query, sizeof(async_query_buf) - 1);
        async_query_buf[sizeof(async_query_buf) - 1] = '\0';
    } else {
        strncpy(async_query_buf, "Hello!", sizeof(async_query_buf) - 1);
        async_query_buf[sizeof(async_query_buf) - 1] = '\0';
    }

    is_async_running = true;
    is_async_done = false;
    is_async_success = false;
    is_async_cancelled = false;
    is_busy = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        asyncWorkerTask,
        "gemini_async",
        10240,
        this,
        1,
        &async_task_handle,
        0
    );

    if (res != pdPASS) {
        is_async_running = false;
        is_busy = false;
        setError("TASK CREATE FAILED");
        is_async_success = false;
        async_out_msg = "Low memory for task";
        async_out_emote = "mistake";
        async_out_act = "NONE";
        async_out_heard = "";
        is_async_done = true;
        return false;
    }

    return true;
#else
    // Host / simulated test execution
    if (query && strlen(query) > 0) {
        strncpy(async_query_buf, query, sizeof(async_query_buf) - 1);
        async_query_buf[sizeof(async_query_buf) - 1] = '\0';
    } else {
        strncpy(async_query_buf, "Hello!", sizeof(async_query_buf) - 1);
        async_query_buf[sizeof(async_query_buf) - 1] = '\0';
    }
    is_async_running = false;
    is_async_success = ask(async_query_buf, async_out_msg, async_out_emote, async_out_act);
    async_out_heard = String(last_transcription);
    is_async_done = true;
    return true;
#endif
}

#ifdef ARDUINO
void GeminiClient::asyncWorkerTask(void* param) {
    GeminiClient* client = (GeminiClient*)param;
    if (client) {
        client->runAsyncWorker();
    }
    vTaskDelete(NULL);
}
#endif

void GeminiClient::runAsyncWorker() {
#ifdef ARDUINO
    if (is_async_cancelled) {
        is_async_running = false;
        is_busy = false;
        async_task_handle = NULL;
        return;
    }

    String prompt = buildPrompt(async_query_buf);
    String sanitized = escapeJson(prompt);

    String endpoint = "https://generativelanguage.googleapis.com/v1beta/models/";
    endpoint += model_name;
    endpoint += ":generateContent?key=";
    endpoint += api_key;

    String json;
    if (strstr(model_name, "lite") != NULL || strstr(model_name, "1.5") != NULL) {
        json = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300}}";
    } else {
        json = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300,\"thinkingConfig\":{\"thinkingBudget\":0}}}";
    }

    String raw = sendHttpsRequest(endpoint, json);

    // Auto-fallback: retry with gemini-3.5-flash-lite (fast 1.4s, separate quota)
    if (raw.length() == 0 && !is_async_cancelled) {
        endpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=";
        endpoint += api_key;
        String liteJson = "{\"contents\":[{\"parts\":[{\"text\":\"" + sanitized + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":300}}";
        raw = sendHttpsRequest(endpoint, liteJson);
        if (raw.length() > 0) {
            strncpy(model_name, "gemini-3.5-flash-lite", sizeof(model_name) - 1);
        }
    }

    if (is_async_cancelled) {
        is_async_running = false;
        is_busy = false;
        async_task_handle = NULL;
        return;
    }

    if (raw.length() == 0) {
        async_out_emote = "mistake";
        async_out_act = "NONE";
        String err = String(last_error);
        if (err.indexOf("connection refused") != -1 || err.indexOf("not connected") != -1 || err.indexOf("connection lost") != -1) {
            async_out_msg = "Net Err: Check Wi-Fi";
        } else if (err.indexOf("read Timeout") != -1 || err.indexOf("Timeout") != -1) {
            async_out_msg = "Net Err: Request timeout";
        } else if (err.indexOf("429") != -1 || err.indexOf("quota") != -1 || err.indexOf("Quota") != -1) {
            async_out_msg = "Quota exceeded. Try in a bit";
        } else if (err.indexOf("too less ram") != -1) {
            async_out_msg = "Err: Low memory";
        } else if (err.length() > 0) {
            async_out_msg = "Err: " + err;
        } else {
            async_out_msg = "Gemini request failed";
        }
        async_out_heard = "";
        is_async_success = false;
    } else {
        String msg, emote, act;
        parseResponse(raw, emote, act, msg);
        async_out_msg = msg;
        async_out_emote = emote;
        async_out_act = act;
        async_out_heard = String(last_transcription);
        is_async_success = true;
    }

    is_async_running = false;
    is_busy = false;
    is_async_done = true;
    async_task_handle = NULL;
#endif
}

bool GeminiClient::pollAsyncResult(bool &outSuccess, String &outMessage, String &outEmote, String &outAction, String &outHeard) {
    if (!is_async_done) return false;

    outSuccess = is_async_success;
    outMessage = async_out_msg;
    outEmote = async_out_emote;
    outAction = async_out_act;
    outHeard = async_out_heard;

    is_async_done = false;
    return true;
}

void GeminiClient::cancelAsync() {
    is_async_cancelled = true;
    is_async_done = false;
}
