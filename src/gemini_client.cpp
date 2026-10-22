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

GeminiClient::GeminiClient() :
    is_busy(false)
{
    memset(api_key, 0, sizeof(api_key));
    strncpy(model_name, "gemini-1.5-flash", sizeof(model_name) - 1);
    memset(last_error, 0, sizeof(last_error));
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
        }
        String mdl = prefs.getString("model", "gemini-1.5-flash");
        if (mdl.length() > 0) {
            strncpy(model_name, mdl.c_str(), sizeof(model_name) - 1);
            model_name[sizeof(model_name) - 1] = '\0';
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
            if (val.startsWith("AIza") || (val.length() >= 15 && foundKey.length() == 0)) {
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
        const char* cur = audioPlayer.getCurrentTrack();
        if (cur && strlen(cur) > 0) {
            String cTrack(cur);
            cTrack.replace("\"", "\\\"");
            json += ",\"track\":\"" + cTrack + "\"";
            json += ",\"sample_rate\":" + String(audioPlayer.getSampleRate());
            json += ",\"format\":\"" + String(audioPlayer.getFormatBadge()) + "\"";
        }
    }
    if (sdManager.isMounted()) {
        json += ",\"sd_tracks\":" + String(sdManager.getTotalTracks());
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

    p += "You are fully aware of all Q-Tune controls.\n";
    p += "Current Device State: " + buildSystemStateJson() + "\n";
    p += "Available Actions: PLAY_TRACK:<name>, PAUSE, RESUME, NEXT, PREV, SET_VOLUME:<0-100>, ";
    p += "SET_EQ:<FLAT|BASS_BOOST|VOCAL|ROCK|POP|JAZZ|CLASSICAL|CLUB>, SET_CROSSFEED:<OFF|SUBTLE|NATURAL>, ";
    p += "SWITCH_OUTPUT:<SPEAKER|KA11>, SWITCH_COMPANION:<CAT|MOCHI>, SET_VISUALIZER:<preset>, ";
    p += "SET_LED:<OFF|VOCAL|BASS|DISCO|SPECTRUM|RAINBOW>, PLAY_SOUND:<MEOW|PURR|CHIRP>, NONE.\n";
    p += "Available Emotions (.anim): happy, dancing, love, giggle, wink, angry, surprised, sleepy, relaxed, energetic, furious, sparkle, speed, dizzy, yawn, hello, serene, shy, brave, adore, smirk, sick.\n\n";
    p += "Respond strictly in this format:\n";
    p += "EMOTE: [<emotion>]\n";
    p += "ACTION: [<command_or_NONE>]\n";
    p += "MESSAGE: <concise message for 128x64 screen, max 2 lines>\n\n";
    p += "User: ";
    p += (userQuery && strlen(userQuery) > 0) ? userQuery : "Hello!";
    return p;
}

bool GeminiClient::parseResponse(const String &rawResponse, String &outEmote, String &outAction, String &outMessage) {
    outEmote = "happy";
    outAction = "NONE";
    outMessage = "";

    // Extract EMOTE
    int emoteStart = rawResponse.indexOf("EMOTE: [");
    if (emoteStart != -1) {
        int emoteEnd = rawResponse.indexOf(']', emoteStart);
        if (emoteEnd != -1) {
            outEmote = rawResponse.substring(emoteStart + 8, emoteEnd);
            outEmote.trim();
            outEmote.toLowerCase();
        }
    }

    // Extract ACTION
    int actionStart = rawResponse.indexOf("ACTION: [");
    if (actionStart != -1) {
        int actionEnd = rawResponse.indexOf(']', actionStart);
        if (actionEnd != -1) {
            outAction = rawResponse.substring(actionStart + 9, actionEnd);
            outAction.trim();
        }
    }

    // Extract MESSAGE
    int msgStart = rawResponse.indexOf("MESSAGE:");
    if (msgStart != -1) {
        outMessage = rawResponse.substring(msgStart + 8);
        outMessage.trim();
    } else {
        outMessage = rawResponse;
        outMessage.trim();
    }

    // Clean quotes/newlines
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

    if (action.startsWith("SET_MOOD:")) {
        String m = action.substring(9);
        m.toUpperCase();
        if (m.indexOf("HAPPY") != -1) aiCompanion.setMood(MOOD_HAPPY, 5000);
        else if (m.indexOf("JAM") != -1) aiCompanion.setMood(MOOD_JAMMING, 8000);
        else if (m.indexOf("LOVE") != -1) aiCompanion.setMood(MOOD_LOVE, 5000);
        else if (m.indexOf("SLEEP") != -1) aiCompanion.setMood(MOOD_SLEEPY, 6000);
        else if (m.indexOf("ANGRY") != -1) aiCompanion.setMood(MOOD_ANGRY, 4000);
        else if (m.indexOf("THINK") != -1) aiCompanion.setMood(MOOD_THINKING, 6000);
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
    client.setInsecure(); // Save memory on ESP32-S3

    HTTPClient https;
    if (!https.begin(client, endpoint)) {
        setError("HTTPS INIT FAILED");
        return "";
    }

    https.addHeader("Content-Type", "application/json");
    https.setTimeout(12000); // 12-second timeout

    int httpCode = https.POST(jsonPayload);
    String response = "";

    if (httpCode == HTTP_CODE_OK) {
        String body = https.getString();
        // Parse Gemini JSON response candidate text
        int textIdx = body.indexOf("\"text\": \"");
        if (textIdx != -1) {
            int endIdx = body.indexOf("\"", textIdx + 9);
            while (endIdx != -1 && body.charAt(endIdx - 1) == '\\') {
                endIdx = body.indexOf("\"", endIdx + 1);
            }
            if (endIdx != -1) {
                response = body.substring(textIdx + 9, endIdx);
                response.replace("\\n", "\n");
                response.replace("\\\"", "\"");
            }
        }
        if (response.length() == 0) response = body;
        setError("OK");
    } else {
        String body = https.getString();
        int msgIdx = body.indexOf("\"message\": \"");
        if (msgIdx != -1) {
            int endIdx = body.indexOf("\"", msgIdx + 12);
            if (endIdx != -1) {
                String errMsg = body.substring(msgIdx + 12, endIdx);
                setError(errMsg.substring(0, 50).c_str());
            } else {
                String err = "HTTP " + String(httpCode);
                setError(err.c_str());
            }
        } else {
            String err = "HTTP " + String(httpCode);
            setError(err.c_str());
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
        outMessage = "Gemini API key missing! Add to /gemini.txt on SD card or web.";
        setError("NO API KEY");
        return false;
    }

#ifdef ARDUINO
    if (WiFi.status() != WL_CONNECTED) {
        outEmote = "crying";
        outAction = "NONE";
        outMessage = "Wi-Fi disconnected! Please connect in Wi-Fi menu.";
        setError("NO WIFI");
        return false;
    }

    is_busy = true;
    aiCompanion.setMood(MOOD_THINKING, 8000);

    String prompt = buildPrompt(query);
    // Sanitize prompt for JSON string
    prompt.replace("\\", "\\\\");
    prompt.replace("\"", "\\\"");
    prompt.replace("\n", "\\n");

    String endpoint = "https://generativelanguage.googleapis.com/v1beta/models/";
    endpoint += model_name;
    endpoint += ":generateContent?key=";
    endpoint += api_key;

    String json = "{\"contents\":[{\"parts\":[{\"text\":\"" + prompt + "\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":150}}";

    String raw = sendHttpsRequest(endpoint, json);
    is_busy = false;

    if (raw.length() == 0) {
        outEmote = "mistake";
        outAction = "NONE";
        outMessage = "Gemini request failed: " + String(last_error);
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
