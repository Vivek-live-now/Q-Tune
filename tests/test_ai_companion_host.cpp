#include "../include/anim_engine.h"
#include "../include/ai_companion.h"
#include "../include/gemini_client.h"

#include <cassert>
#include <iostream>
#include <cstring>

int main() {
    std::cout << "=== Q-Tune Native Unit Tests (AnimEngine, AICompanion & GeminiClient) ===\n";

    // -------------------------------------------------------------------------
    // 1. AnimEngine Tests
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 1. AnimEngine binary specification & file playback...\n";
    assert(animEngine.begin() == true);
    assert(animEngine.isOpen() == false);

    // Open dancing.anim from consolidated sdcard folder
    const char* testAnim = "Q-Tune/sdcard/anim/dancing.anim";
    bool opened = animEngine.open(testAnim);
    if (!opened) {
        // Fallback if run from Q-Tune root
        opened = animEngine.open("sdcard/anim/dancing.anim");
    }
    assert(opened == true);
    assert(animEngine.isOpen() == true);
    assert(animEngine.isPlaying() == true);
    assert(animEngine.getTotalFrames() > 0);
    assert(animEngine.getHeader().width == 128);
    assert(animEngine.getHeader().height == 64);
    assert(animEngine.getHeader().magic == ANIM_MAGIC);

    // Test stepping & seeking
    uint16_t initialFrame = animEngine.getCurrentFrame();
    assert(initialFrame == 0);
    animEngine.stepForward();
    assert(animEngine.getCurrentFrame() == 1);
    animEngine.stepBackward();
    assert(animEngine.getCurrentFrame() == 0);
    assert(animEngine.seekFrame(2) == true);
    assert(animEngine.getCurrentFrame() == 2);

    // Test frame buffer data integrity
    const uint8_t* fb = animEngine.getFrameBuffer();
    assert(fb != nullptr);

    animEngine.close();
    assert(animEngine.isOpen() == false);
    std::cout << "  [PASS] AnimEngine QANM header, frame seek, and PSRAM memory buffer verified.\n";

    // -------------------------------------------------------------------------
    // 2. AICompanion State Machine & Switcher Tests
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 2. AICompanion DJ Neko & Mochi Switching...\n";
    aiCompanion.begin();
    aiCompanion.setCompanion(COMPANION_CAT);
    assert(aiCompanion.getCompanion() == COMPANION_CAT);
    assert(strcmp(aiCompanion.getCompanionName(), "DJ NEKO (CAT)") == 0);

    // Switch to Mochi
    aiCompanion.toggleCompanion();
    assert(aiCompanion.getCompanion() == COMPANION_MOCHI);
    assert(strcmp(aiCompanion.getCompanionName(), "MOCHI (CLASSIC)") == 0);

    // Switch back to DJ Neko
    aiCompanion.toggleCompanion();
    assert(aiCompanion.getCompanion() == COMPANION_CAT);

    // Moods & Visual modes
    aiCompanion.setMood(MOOD_JAMMING);
    assert(aiCompanion.getMood() == MOOD_JAMMING);
    assert(strcmp(aiCompanion.getMoodName(), "JAMMING") == 0);

    aiCompanion.setMood(MOOD_BASS_BOOSTED);
    assert(strcmp(aiCompanion.getMoodName(), "BASS BOOST") == 0);
    aiCompanion.setMood(MOOD_LOVE);
    assert(strcmp(aiCompanion.getMoodName(), "LOVE") == 0);
    aiCompanion.setMood(MOOD_WINK);
    assert(strcmp(aiCompanion.getMoodName(), "WINK") == 0);
    aiCompanion.setMood(MOOD_DIZZY);
    assert(strcmp(aiCompanion.getMoodName(), "DIZZY") == 0);
    aiCompanion.setMood(MOOD_SMIRK);
    assert(strcmp(aiCompanion.getMoodName(), "SMIRK") == 0);
    aiCompanion.setMood(MOOD_SAD);
    assert(strcmp(aiCompanion.getMoodName(), "SAD") == 0);
    aiCompanion.setMood(MOOD_ENERGETIC);
    assert(strcmp(aiCompanion.getMoodName(), "ENERGETIC") == 0);

    // 65 anims catalog verification
    assert(AICompanion::ANIM_CATALOG_COUNT == 65);
    assert(strcmp(AICompanion::ANIM_CATALOG[0], "adore") == 0);
    assert(strcmp(AICompanion::ANIM_CATALOG[64], "yawn") == 0);

    aiCompanion.cycleNextFace();
    assert(strlen(aiCompanion.getCurrentFaceDisplayName()) > 0);

    aiCompanion.setVisualMode(VISUAL_HYBRID);
    assert(aiCompanion.getVisualMode() == VISUAL_HYBRID);
    aiCompanion.cycleVisualMode();
    assert(aiCompanion.getVisualMode() == VISUAL_REACTIVE);

    // Playback Speed Controls
    assert(aiCompanion.getSpeedMultiplier() == 1.0f);
    assert(strcmp(aiCompanion.getSpeedString(), "1.0x") == 0);
    aiCompanion.cycleSpeed();
    assert(aiCompanion.getSpeedMultiplier() == 1.25f);
    assert(strcmp(aiCompanion.getSpeedString(), "1.25x") == 0);
    aiCompanion.setSpeedIndex(0);
    assert(aiCompanion.getSpeedMultiplier() == 0.5f);
    assert(strcmp(aiCompanion.getSpeedString(), "0.5x") == 0);
    aiCompanion.setSpeedIndex(2); // restore 1.0x
    assert(aiCompanion.getSpeedMultiplier() == 1.0f);

    // Live audio reactivity physics
    aiCompanion.update(0.033f);
    aiCompanion.update(0.033f);
    assert(aiCompanion.getBeatPulse() >= 0.0f);

    // Voice Engine Autonomy Controls (SFX, On-Device Retro TTS, Cloud Streaming, Mute)
    aiCompanion.setVoiceMode(VOICE_MODE_SFX);
    assert(aiCompanion.getVoiceMode() == VOICE_MODE_SFX);
    assert(strcmp(aiCompanion.getVoiceModeShortName(), "SFX") == 0);
    assert(strcmp(aiCompanion.getVoiceModeName(), "SFX (CHIPTUNE)") == 0);

    aiCompanion.cycleVoiceMode();
    assert(aiCompanion.getVoiceMode() == VOICE_MODE_ON_DEVICE_TTS);
    assert(strcmp(aiCompanion.getVoiceModeShortName(), "DEVICE") == 0);
    assert(strcmp(aiCompanion.getVoiceModeName(), "ON-DEVICE RETRO") == 0);

    aiCompanion.cycleVoiceMode();
    assert(aiCompanion.getVoiceMode() == VOICE_MODE_STREAMING_TTS);
    assert(strcmp(aiCompanion.getVoiceModeShortName(), "STREAM") == 0);
    assert(strcmp(aiCompanion.getVoiceModeName(), "CLOUD STREAMING") == 0);

    aiCompanion.cycleVoiceMode();
    assert(aiCompanion.getVoiceMode() == VOICE_MODE_MUTE);
    assert(strcmp(aiCompanion.getVoiceModeShortName(), "MUTE") == 0);
    assert(strcmp(aiCompanion.getVoiceModeName(), "MUTE (TEXT ONLY)") == 0);

    aiCompanion.cycleVoiceMode();
    assert(aiCompanion.getVoiceMode() == VOICE_MODE_SFX);
    aiCompanion.speak("Test speech engine");

    // Trigger Word ("Hey Neko" / "Hey Mochi" Acoustic Cadence Matcher)
    assert(aiCompanion.isTriggerWordEnabled() == false);
    assert(strcmp(aiCompanion.getTriggerWordStatus(), "OFF") == 0);
    aiCompanion.setTriggerWordEnabled(true);
    assert(aiCompanion.isTriggerWordEnabled() == true);
    assert(strcmp(aiCompanion.getTriggerWordStatus(), "ON") == 0);
    aiCompanion.toggleTriggerWord();
    assert(aiCompanion.isTriggerWordEnabled() == false);
    aiCompanion.toggleTriggerWord();
    assert(aiCompanion.isTriggerWordEnabled() == true);

    // 1. Silence test
    aiCompanion.resetTriggerDetector();
    aiCompanion.setTriggerSimTime(1000);
    assert(aiCompanion.processTriggerAudio(0.0f, 0.0f, 0.0f) == false);

    // 2. Transients / single spike rejection
    aiCompanion.resetTriggerDetector();
    aiCompanion.setTriggerSimTime(2000);
    aiCompanion.processTriggerAudio(30.0f, 0.5f, 220.0f); // 35ms spike
    assert(aiCompanion.processTriggerAudio(0.0f, 0.0f, 0.0f) == false); // dropped

    // 3. Continuous tone rejection (no syllable dip)
    aiCompanion.resetTriggerDetector();
    aiCompanion.setTriggerSimTime(3000);
    for (int f = 0; f < 20; f++) {
        assert(aiCompanion.processTriggerAudio(30.0f, 0.5f, 220.0f) == false);
    }

    // 4. Valid "Hey Neko" cadence detection
    aiCompanion.resetTriggerDetector();
    aiCompanion.setTriggerSimTime(4000);
    // Syllable 1 ("Hey" - ~175ms)
    for (int f = 0; f < 5; f++) {
        assert(aiCompanion.processTriggerAudio(25.0f, 0.40f, 220.0f) == false);
    }
    // Gap (~105ms)
    for (int f = 0; f < 3; f++) {
        assert(aiCompanion.processTriggerAudio(2.0f, 0.05f, 0.0f) == false);
    }
    // Syllable 2 ("Neko" - ~210ms)
    bool matched = false;
    for (int f = 0; f < 6; f++) {
        if (aiCompanion.processTriggerAudio(28.0f, 0.45f, 240.0f)) {
            matched = true;
            break;
        }
    }
    assert(matched == true);

    std::cout << "  [PASS] AICompanion dual personality (DJ Neko ⇄ Mochi), speed controls, voice modes, trigger word cadence, reactivity, and visual modes verified.\n";

    // -------------------------------------------------------------------------
    // 3. GeminiClient Parsing & Action Execution Tests
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 3. GeminiClient Context, Prompts & Action Parsing...\n";
    geminiClient.begin();
    geminiClient.setApiKey("AIzaSyTestDummyKeyForVerification987");
    assert(geminiClient.hasApiKey() == true);
    assert(strcmp(geminiClient.getModelName(), "gemini-3.5-flash") == 0);
    assert(strcmp(geminiClient.getModelShortName(), "3.5-FL") == 0);
    geminiClient.cycleModel();
    assert(strcmp(geminiClient.getModelName(), "gemini-3.5-flash-lite") == 0);
    assert(strcmp(geminiClient.getModelShortName(), "3.5-LT") == 0);
    geminiClient.cycleModel();
    assert(strcmp(geminiClient.getModelName(), "gemini-2.5-flash") == 0);
    assert(strcmp(geminiClient.getModelShortName(), "2.5-FL") == 0);
    geminiClient.cycleModel();
    assert(strcmp(geminiClient.getModelName(), "gemini-2.5-flash-lite") == 0);
    assert(strcmp(geminiClient.getModelShortName(), "2.5-LT") == 0);
    geminiClient.cycleModel();
    assert(strcmp(geminiClient.getModelName(), "gemini-3.5-flash") == 0);
    assert(strcmp(geminiClient.getModelShortName(), "3.5-FL") == 0);

    // Prompt building
    String prompt = geminiClient.buildPrompt("Play some jazz!");
    assert(prompt.indexOf("DJ Neko") != -1);
    assert(prompt.indexOf("PLAY_TRACK") != -1);
    assert(prompt.indexOf("SET_EQ") != -1);
    assert(prompt.indexOf("Play some jazz!") != -1);
    assert(prompt.indexOf("gundam") != -1);
    assert(prompt.indexOf("PLAY_ANIM") != -1);

    // Response parsing
    String emote, action, message;
    String sampleResponse = "EMOTE: [dancing]\nACTION: [SET_EQ:BASS_BOOST]\nMESSAGE: Bass boosted! Let's groove, meow!";
    bool parsed = geminiClient.parseResponse(sampleResponse, emote, action, message);
    assert(parsed == true);
    assert(emote == "dancing");
    assert(action == "SET_EQ:BASS_BOOST");
    assert(message.indexOf("Bass boosted!") != -1);

    String sampleWithHeard = "HEARD: How is the weather today?\nEMOTE: curious\nACTION: NONE\nMESSAGE: Sunny and warm!";
    assert(geminiClient.parseResponse(sampleWithHeard, emote, action, message) == true);
    assert(strcmp(geminiClient.getLastTranscription(), "How is the weather today?") == 0);
    assert(message.indexOf("Sunny and warm!") != -1);

    // Action Execution
    assert(geminiClient.executeAction("SWITCH_COMPANION:MOCHI") == true);
    assert(aiCompanion.getCompanion() == COMPANION_MOCHI);
    assert(geminiClient.executeAction("SWITCH_COMPANION:CAT") == true);
    assert(aiCompanion.getCompanion() == COMPANION_CAT);
    assert(geminiClient.executeAction("SET_MOOD:JAMMING") == true);
    assert(aiCompanion.getMood() == MOOD_JAMMING);
    assert(geminiClient.executeAction("SET_MOOD:HAPPY") == true);
    assert(aiCompanion.getMood() == MOOD_HAPPY);
    assert(geminiClient.executeAction("SET_MOOD:LOVE") == true);
    assert(aiCompanion.getMood() == MOOD_LOVE);
    assert(geminiClient.executeAction("SET_MOOD:BASS_BOOSTED") == true);
    assert(aiCompanion.getMood() == MOOD_BASS_BOOSTED);
    assert(geminiClient.executeAction("PLAY_ANIM:gundam") == true);

    // Voice query execution
    String vMsg, vEmote, vAct;
    assert(geminiClient.askVoice(nullptr, 0, vMsg, vEmote, vAct) == true);
    assert(vAct == "SET_VOLUME:80");

    // Non-blocking asynchronous query execution
    assert(geminiClient.askAsync("What's the weather today?") == true);
    assert(geminiClient.isAsyncDone() == true);
    bool asyncSuccess = false;
    String aMsg, aEmote, aAct, aHeard;
    assert(geminiClient.pollAsyncResult(asyncSuccess, aMsg, aEmote, aAct, aHeard) == true);
    assert(asyncSuccess == true);
    assert(geminiClient.isAsyncDone() == false);

    std::cout << "  [PASS] GeminiClient prompt serialization, response parsing, and action execution verified.\n";
    std::cout << "\nAll AI Companion & AnimEngine Native Tests PASSED!\n";
    return 0;
}
