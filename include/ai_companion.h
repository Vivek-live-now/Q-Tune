#ifndef AI_COMPANION_H
#define AI_COMPANION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "anim_engine.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include "hw_config.h"
#include "display.h"
#include "spectrum_analyzer.h"
#include "audio_player.h"
#else
#include <string>
#include <stdint.h>
#endif

// ==============================================================================
// Companion Types & Visual Modes
// ==============================================================================
enum CompanionType : uint8_t {
    COMPANION_CAT = 0,    // DJ Neko (Cute Music-Lover Cat with Studio Headphones)
    COMPANION_MOCHI = 1,  // Classic Mochi (Dasai Iconic Pet)
    COMPANION_COUNT = 2
};

enum CompanionVisualMode : uint8_t {
    VISUAL_HYBRID = 0,    // Live audio-reactive by default; plays .anim on emotions/events
    VISUAL_REACTIVE = 1,  // 100% Live 60 FPS procedural audio-reactive
    VISUAL_ANIM_ONLY = 2  // 100% .anim file playback from SD card
};

enum CompanionMood : uint8_t {
    MOOD_IDLE = 0,
    MOOD_LISTENING,
    MOOD_JAMMING,
    MOOD_BASS_BOOSTED,
    MOOD_SLEEPY,
    MOOD_HAPPY,
    MOOD_LOVE,
    MOOD_SURPRISED,
    MOOD_ANGRY,
    MOOD_THINKING,
    MOOD_WINK,
    MOOD_DIZZY,
    MOOD_SMIRK,
    MOOD_SAD,
    MOOD_ENERGETIC,
    MOOD_COUNT
};

class AICompanion {
public:
    AICompanion();
    bool begin();
    void update(float dt = 0.033f);

    // Catalog of all 65 SD card .anim faces
    static const size_t ANIM_CATALOG_COUNT = 65;
    static const char* const ANIM_CATALOG[ANIM_CATALOG_COUNT];

    // Face / Animation Browsing & Cycling (Supports all 65 .anim + 15 procedural faces)
    void cycleNextAnim();
    void cyclePrevAnim();
    void cycleNextFace();
    void cyclePrevFace();
    const char* getCurrentFaceDisplayName() const;
    const char* getCurrentAnimName() const { return current_anim_name; }
    size_t getCurrentAnimIndex() const { return current_anim_index; }

    // Toast overlay (for showing face name during quick full-screen cycling)
    bool isToastActive() const;
    const char* getToastName() const { return last_face_name; }

    // Playback Speed Controls
    void cycleSpeed();
    void setSpeedIndex(uint8_t idx);
    uint8_t getSpeedIndex() const { return speed_index; }
    float getSpeedMultiplier() const;
    const char* getSpeedString() const;

    // Autonomous mood and animation rotation
    bool isAutonomous() const { return autonomous_enabled; }
    void setAutonomous(bool enable) { autonomous_enabled = enable; }

    // Companion Selection & Switcher
    CompanionType getCompanion() const { return current_type; }
    void setCompanion(CompanionType type);
    void toggleCompanion();
    const char* getCompanionName() const;

    // Visual Mode Controls
    CompanionVisualMode getVisualMode() const { return visual_mode; }
    void setVisualMode(CompanionVisualMode mode);
    void cycleVisualMode();
    const char* getVisualModeName() const;

    // Mood & Animation Triggering
    void setMood(CompanionMood mood, uint32_t duration_ms = 0);
    CompanionMood getMood() const { return current_mood; }
    const char* getMoodName() const;

    // Play specific .anim face from SD Card
    bool playAnim(const char* name, uint32_t duration_ms = 4000);
    bool isPlayingAnim() const;
    void stopAnim();

    // Sound FX & Cat Purrs / Meows
    void playMeow();
    void playPurr();
    void playChirp();
    void toggleSound() { sound_enabled = !sound_enabled; }
    bool isSoundEnabled() const { return sound_enabled; }

    // Rendering
    void render(int16_t x = 0, int16_t y = 0, int16_t w = 128, int16_t h = 64);
    void renderLiveCat(int16_t cx, int16_t cy);
    void renderLiveMochi(int16_t cx, int16_t cy);
    void renderNekoAccessories(int16_t cx, int16_t cy);

    // Live Audio Reactivity Telemetry
    float getBeatPulse() const { return beat_pulse; }
    float getHeadBob() const { return head_bob_y; }
    float getEarTwitch() const { return ear_twitch; }
    float getWhiskerVibe() const { return whisker_vibe; }
    float getTailSwish() const { return tail_swish; }

    // Settings Persistence
    void loadSettings();
    void saveSettings();

private:
    CompanionType current_type;
    CompanionVisualMode visual_mode;
    CompanionMood current_mood;

    bool sound_enabled;
    bool neko_overlay_on_anim;
    uint8_t speed_index;

    // Animation / Emotion timer
    uint32_t emote_end_time;
    char current_anim_name[32];

    // Live Audio Reactivity State
    float anim_timer;
    float beat_pulse;
    float head_bob_y;
    float ear_twitch;
    float whisker_vibe;
    float tail_swish;
    float breath_phase;
    float heart_pulse;

    // Blinking State
    bool is_blinking;
    float blink_timer;
    float blink_phase;

    // Gaze tracking
    float pupil_dx;
    float pupil_dy;

    // Face / Anim Catalog State & Autonomous Engine
    size_t current_anim_index;
    bool autonomous_enabled;
    float autonomous_timer;
    float next_autonomous_interval;
    uint32_t face_toast_end_time;
    char last_face_name[32];

    void updateReactivity(float dt);
};

extern AICompanion aiCompanion;

#endif // AI_COMPANION_H
