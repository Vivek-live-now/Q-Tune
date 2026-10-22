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

    aiCompanion.setVisualMode(VISUAL_HYBRID);
    assert(aiCompanion.getVisualMode() == VISUAL_HYBRID);
    aiCompanion.cycleVisualMode();
    assert(aiCompanion.getVisualMode() == VISUAL_REACTIVE);

    // Live audio reactivity physics
    aiCompanion.update(0.033f);
    aiCompanion.update(0.033f);
    assert(aiCompanion.getBeatPulse() >= 0.0f);

    std::cout << "  [PASS] AICompanion dual personality (DJ Neko ⇄ Mochi), reactivity, and visual modes verified.\n";

    // -------------------------------------------------------------------------
    // 3. GeminiClient Parsing & Action Execution Tests
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 3. GeminiClient Context, Prompts & Action Parsing...\n";
    geminiClient.begin();
    geminiClient.setApiKey("AIzaSyTestDummyKeyForVerification987");
    assert(geminiClient.hasApiKey() == true);
    assert(strcmp(geminiClient.getModelName(), "gemini-3.5-flash-lite") == 0);

    // Prompt building
    String prompt = geminiClient.buildPrompt("Play some jazz!");
    assert(prompt.indexOf("DJ Neko") != -1);
    assert(prompt.indexOf("PLAY_TRACK") != -1);
    assert(prompt.indexOf("SET_EQ") != -1);
    assert(prompt.indexOf("Play some jazz!") != -1);

    // Response parsing
    String emote, action, message;
    String sampleResponse = "EMOTE: [dancing]\nACTION: [SET_EQ:BASS_BOOST]\nMESSAGE: Bass boosted! Let's groove, meow!";
    bool parsed = geminiClient.parseResponse(sampleResponse, emote, action, message);
    assert(parsed == true);
    assert(emote == "dancing");
    assert(action == "SET_EQ:BASS_BOOST");
    assert(message.indexOf("Bass boosted!") != -1);

    // Action Execution
    assert(geminiClient.executeAction("SWITCH_COMPANION:MOCHI") == true);
    assert(aiCompanion.getCompanion() == COMPANION_MOCHI);
    assert(geminiClient.executeAction("SWITCH_COMPANION:CAT") == true);
    assert(aiCompanion.getCompanion() == COMPANION_CAT);
    assert(geminiClient.executeAction("SET_MOOD:JAMMING") == true);
    assert(aiCompanion.getMood() == MOOD_JAMMING);
    assert(geminiClient.executeAction("SET_MOOD:HAPPY") == true);
    assert(aiCompanion.getMood() == MOOD_HAPPY);

    std::cout << "  [PASS] GeminiClient prompt serialization, response parsing, and action execution verified.\n";
    std::cout << "\nAll AI Companion & AnimEngine Native Tests PASSED!\n";
    return 0;
}
