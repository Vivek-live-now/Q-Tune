#include "ai_companion.h"
#include <math.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include "hw_config.h"
#include "display.h"
#include "anim_engine.h"
#include "spectrum_analyzer.h"
#include "audio_player.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline uint32_t millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

AICompanion aiCompanion;

AICompanion::AICompanion() :
    current_type(COMPANION_CAT),
    visual_mode(VISUAL_HYBRID),
    current_mood(MOOD_IDLE),
    sound_enabled(true),
    neko_overlay_on_anim(true),
    emote_end_time(0),
    anim_timer(0.0f),
    beat_pulse(0.0f),
    head_bob_y(0.0f),
    ear_twitch(0.0f),
    whisker_vibe(0.0f),
    tail_swish(0.0f),
    breath_phase(0.0f),
    heart_pulse(1.0f),
    is_blinking(false),
    blink_timer(0.0f),
    blink_phase(0.0f),
    pupil_dx(0.0f),
    pupil_dy(0.0f)
{
    memset(current_anim_name, 0, sizeof(current_anim_name));
}

bool AICompanion::begin() {
    loadSettings();
    anim_timer = 0.0f;
    return true;
}

void AICompanion::loadSettings() {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("ai_cfg", true)) {
        current_type = (CompanionType)prefs.getUChar("type", (uint8_t)COMPANION_CAT);
        visual_mode = (CompanionVisualMode)prefs.getUChar("vmode", (uint8_t)VISUAL_HYBRID);
        sound_enabled = prefs.getBool("snd", true);
        neko_overlay_on_anim = prefs.getBool("overlay", true);
        prefs.end();
    }
#endif
}

void AICompanion::saveSettings() {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("ai_cfg", false)) {
        prefs.putUChar("type", (uint8_t)current_type);
        prefs.putUChar("vmode", (uint8_t)visual_mode);
        prefs.putBool("snd", sound_enabled);
        prefs.putBool("overlay", neko_overlay_on_anim);
        prefs.end();
    }
#endif
}

void AICompanion::setCompanion(CompanionType type) {
    if (type < COMPANION_COUNT) {
        current_type = type;
        saveSettings();
        if (current_type == COMPANION_CAT) {
            playMeow();
        } else {
            playChirp();
        }
    }
}

void AICompanion::toggleCompanion() {
    setCompanion((current_type == COMPANION_CAT) ? COMPANION_MOCHI : COMPANION_CAT);
}

const char* AICompanion::getCompanionName() const {
    switch (current_type) {
        case COMPANION_CAT: return "DJ NEKO (CAT)";
        case COMPANION_MOCHI: return "MOCHI (CLASSIC)";
        default: return "COMPANION";
    }
}

void AICompanion::setVisualMode(CompanionVisualMode mode) {
    visual_mode = mode;
    saveSettings();
}

void AICompanion::cycleVisualMode() {
    uint8_t next = ((uint8_t)visual_mode + 1) % 3;
    setVisualMode((CompanionVisualMode)next);
}

const char* AICompanion::getVisualModeName() const {
    switch (visual_mode) {
        case VISUAL_HYBRID: return "HYBRID (AUTO)";
        case VISUAL_REACTIVE: return "LIVE REACTIVE";
        case VISUAL_ANIM_ONLY: return "ANIM ONLY";
        default: return "AUTO";
    }
}

void AICompanion::setMood(CompanionMood mood, uint32_t duration_ms) {
    current_mood = mood;
    if (duration_ms > 0) {
        emote_end_time = millis() + duration_ms;
    } else {
        emote_end_time = 0;
    }
}

const char* AICompanion::getMoodName() const {
    switch (current_mood) {
        case MOOD_IDLE: return "IDLE";
        case MOOD_LISTENING: return "LISTENING";
        case MOOD_JAMMING: return "JAMMING";
        case MOOD_BASS_BOOSTED: return "BASS BOOST";
        case MOOD_SLEEPY: return "SLEEPY";
        case MOOD_HAPPY: return "HAPPY";
        case MOOD_LOVE: return "LOVE";
        case MOOD_SURPRISED: return "SURPRISED";
        case MOOD_ANGRY: return "ANGRY";
        case MOOD_THINKING: return "THINKING";
        default: return "NORMAL";
    }
}

bool AICompanion::playAnim(const char* name, uint32_t duration_ms) {
    if (!name || strlen(name) == 0) return false;
    strncpy(current_anim_name, name, sizeof(current_anim_name) - 1);
    current_anim_name[sizeof(current_anim_name) - 1] = '\0';

    bool ok = animEngine.open(name);
    if (ok) {
        animEngine.setLoop(true);
        animEngine.play();
        if (duration_ms > 0) {
            emote_end_time = millis() + duration_ms;
        } else {
            emote_end_time = 0;
        }
    }
    return ok;
}

bool AICompanion::isPlayingAnim() const {
    return animEngine.isOpen() && animEngine.isPlaying();
}

void AICompanion::stopAnim() {
    if (animEngine.isOpen()) {
        animEngine.stop();
        animEngine.close();
    }
    emote_end_time = 0;
    current_anim_name[0] = '\0';
}

void AICompanion::playMeow() {
#ifdef ARDUINO
    if (!sound_enabled) return;
    // Pleasant rising/falling frequency sweep simulating a cute meow
    audioPlayer.playTestTone(880, 70);
    audioPlayer.playTestTone(1250, 90);
    audioPlayer.playTestTone(980, 80);
#endif
}

void AICompanion::playPurr() {
#ifdef ARDUINO
    if (!sound_enabled) return;
    // Low frequency gentle rhythmic pulses
    for (int i = 0; i < 3; i++) {
        audioPlayer.playTestTone(130, 45);
        delay(30);
    }
#endif
}

void AICompanion::playChirp() {
#ifdef ARDUINO
    if (!sound_enabled) return;
    // Cheerful arpeggio chirp
    audioPlayer.playTestTone(1046, 50); // C6
    audioPlayer.playTestTone(1318, 50); // E6
    audioPlayer.playTestTone(1567, 80); // G6
#endif
}

void AICompanion::updateReactivity(float dt) {
    anim_timer += dt;

    // Organic squashing blink timer
    blink_timer += dt;
    if (!is_blinking) {
        if (blink_timer > 3.8f) {
            is_blinking = true;
            blink_timer = 0.0f;
            blink_phase = 0.0f;
        }
    } else {
        blink_phase += dt * 5.0f;
        if (blink_phase >= 1.0f) {
            is_blinking = false;
            blink_timer = 0.0f;
        }
    }

    // Audio reactivity inputs
#ifdef ARDUINO
    bool isPlaying = audioPlayer.isPlaying();
    float bass = spectrumAnalyzer.getBassLevel();
    float rms = spectrumAnalyzer.getRMSLevel();
    const uint8_t* bands = spectrumAnalyzer.getBands();
    float highEnergy = 0.0f;
    if (bands) {
        highEnergy = ((float)bands[10] + (float)bands[11] + (float)bands[12]) / (3.0f * 255.0f);
    }
#else
    bool isPlaying = false;
    float bass = 0.0f;
    float rms = 0.0f;
    float highEnergy = 0.0f;
    (void)rms;
    (void)highEnergy;
#endif

    // Lively gaze tracking
    pupil_dx = sinf(anim_timer * 1.5f) * 2.0f;
    pupil_dy = cosf(anim_timer * 1.5f) * 1.0f;

    if (isPlaying) {
        // High-energy groove dynamics
        beat_pulse = bass * 1.6f;
        head_bob_y = sinf(anim_timer * 7.5f) * (2.0f + bass * 3.5f);
        ear_twitch = sinf(anim_timer * 16.0f) * (highEnergy * 4.0f);
        whisker_vibe = sinf(anim_timer * 28.0f) * (0.8f + bass * 3.2f);
        tail_swish = sinf(anim_timer * 4.5f) * 10.0f;
        breath_phase = 0.0f;
        heart_pulse = 1.0f + bass * 0.4f;
    } else {
        // Gentle resting breathing dynamics
        beat_pulse = 0.0f;
        breath_phase = sinf(anim_timer * 2.2f) * 1.5f;
        head_bob_y = breath_phase;
        ear_twitch = sinf(anim_timer * 1.2f) * 0.5f;
        whisker_vibe = 0.0f;
        tail_swish = sinf(anim_timer * 1.8f) * 4.0f;
        heart_pulse = 1.0f + 0.1f * sinf(anim_timer * 3.0f);
    }
}

void AICompanion::update(float dt) {
    updateReactivity(dt);

    // Check if timed emote expired
    if (emote_end_time > 0 && millis() >= emote_end_time) {
        emote_end_time = 0;
        current_mood = MOOD_IDLE;
        if (visual_mode == VISUAL_HYBRID && isPlayingAnim()) {
            stopAnim();
        }
    }

    // Update .anim playback if open
    if (isPlayingAnim()) {
        animEngine.update();
    }
}

void AICompanion::render(int16_t x, int16_t y, int16_t w, int16_t h) {
    int16_t cx = x + (w / 2);
    int16_t cy = y + (h / 2);

    bool showAnim = false;
    if (visual_mode == VISUAL_ANIM_ONLY) {
        showAnim = isPlayingAnim();
    } else if (visual_mode == VISUAL_HYBRID) {
        showAnim = isPlayingAnim();
    }

    if (showAnim) {
        animEngine.drawFrame(x, y);
        if (current_type == COMPANION_CAT && neko_overlay_on_anim) {
            renderNekoAccessories(cx, cy);
        }
    } else {
        if (current_type == COMPANION_CAT) {
            renderLiveCat(cx, cy);
        } else {
            renderLiveMochi(cx, cy);
        }
    }
}

void AICompanion::renderLiveCat(int16_t cx, int16_t cy) {
#ifdef ARDUINO
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setDrawColor(1);

    int16_t hy = cy + (int16_t)head_bob_y;

    // 1. Pointed Cat Ears with Inner Notch Detail
    int16_t eTwitch = (int16_t)ear_twitch;
    // Left Ear
    u8g2.drawTriangle(cx - 20, hy - 14, cx - 14 + eTwitch, hy - 26, cx - 6, hy - 14);
    u8g2.drawTriangle(cx - 18, hy - 14, cx - 14 + eTwitch, hy - 22, cx - 9, hy - 14); // inner
    // Right Ear
    u8g2.drawTriangle(cx + 6, hy - 14, cx + 14 - eTwitch, hy - 26, cx + 20, hy - 14);
    u8g2.drawTriangle(cx + 9, hy - 14, cx + 14 - eTwitch, hy - 22, cx + 18, hy - 14); // inner

    // 2. Soft Rounded Cat Face
    u8g2.drawRFrame(cx - 24, hy - 14, 48, 28, 7);

    // 3. Studio DJ Headphones
    // Over-ear Headband
    u8g2.drawRFrame(cx - 26, hy - 19, 52, 12, 5);

    // Left Cushioned Ear Cup
    u8g2.drawRBox(cx - 28, hy - 10, 5, 20, 2);
    // Right Cushioned Ear Cup
    u8g2.drawRBox(cx + 23, hy - 10, 5, 20, 2);

    // Mini Live Spectrum Equalizer on Left & Right Ear Cups!
    float bass = spectrumAnalyzer.getBassLevel();
    float rms = spectrumAnalyzer.getRMSLevel();
    int bHeight1 = (int)(bass * 10.0f);
    int bHeight2 = (int)(rms * 12.0f);
    if (bHeight1 > 12) bHeight1 = 12;
    if (bHeight2 > 12) bHeight2 = 12;

    u8g2.setDrawColor(0); // Cutout inside ear cups
    u8g2.drawVLine(cx - 26, hy + 6 - bHeight1, bHeight1);
    u8g2.drawVLine(cx + 25, hy + 6 - bHeight2, bHeight2);
    u8g2.setDrawColor(1);

    // 4. Kawaii Anime Eyes
    int16_t ey = hy - 3;
    if (is_blinking) {
        // Closed slit blink
        u8g2.drawHLine(cx - 13, ey + 2, 7);
        u8g2.drawHLine(cx + 7, ey + 2, 7);
    } else if (current_mood == MOOD_JAMMING || current_mood == MOOD_HAPPY) {
        // Happy curved smile eyes ^ ^
        u8g2.drawLine(cx - 14, ey + 3, cx - 10, ey - 2);
        u8g2.drawLine(cx - 10, ey - 2, cx - 6, ey + 3);
        u8g2.drawLine(cx + 6, ey + 3, cx + 10, ey - 2);
        u8g2.drawLine(cx + 10, ey - 2, cx + 14, ey + 3);
    } else {
        // Big round shiny anime eyes with white reflection sparkles
        u8g2.drawDisc(cx - 10, ey, 4);
        u8g2.drawDisc(cx + 10, ey, 4);
        u8g2.setDrawColor(0); // White pupil sparkle
        u8g2.drawPixel(cx - 11, ey - 1);
        u8g2.drawPixel(cx + 9, ey - 1);
        u8g2.setDrawColor(1);
    }

    // 5. Bass-Reactive Whiskers
    int16_t wVibe = (int16_t)whisker_vibe;
    // Left Whiskers
    u8g2.drawLine(cx - 22, hy - 1, cx - 34, hy - 4 + wVibe);
    u8g2.drawLine(cx - 23, hy + 2, cx - 36, hy + 2);
    u8g2.drawLine(cx - 22, hy + 5, cx - 34, hy + 7 - wVibe);
    // Right Whiskers
    u8g2.drawLine(cx + 22, hy - 1, cx + 34, hy - 4 + wVibe);
    u8g2.drawLine(cx + 23, hy + 2, cx + 36, hy + 2);
    u8g2.drawLine(cx + 22, hy + 5, cx + 34, hy + 7 - wVibe);

    // 6. Cute :3 Cat Mouth
    u8g2.drawPixel(cx, hy + 3); // tiny nose
    if (audioPlayer.isPlaying() && (rms > 0.4f)) {
        // Singing open mouth :o
        u8g2.drawCircle(cx, hy + 7, 2);
    } else {
        // Kawaii :3 w-mouth
        u8g2.drawPixel(cx - 3, hy + 5);
        u8g2.drawPixel(cx - 2, hy + 6);
        u8g2.drawPixel(cx - 1, hy + 5);
        u8g2.drawPixel(cx + 1, hy + 5);
        u8g2.drawPixel(cx + 2, hy + 6);
        u8g2.drawPixel(cx + 3, hy + 5);
    }

    // 7. Swishing Tail
    int16_t tSwish = (int16_t)tail_swish;
    u8g2.drawLine(cx + 18, hy + 14, cx + 24 + tSwish, hy + 20);
    u8g2.drawCircle(cx + 24 + tSwish, hy + 20, 2);
#else
    (void)cx; (void)cy;
#endif
}

void AICompanion::renderLiveMochi(int16_t cx, int16_t cy) {
#ifdef ARDUINO
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setDrawColor(1);

    int16_t hy = cy + (int16_t)head_bob_y;

    // 1. Classic Dasai Mochi Rounded Helmet Visor
    u8g2.drawRFrame(cx - 24, hy - 14, 48, 28, 8);

    // 2. Corner Visor Notch Accents
    u8g2.drawHLine(cx - 20, hy - 11, 4);
    u8g2.drawHLine(cx + 16, hy - 11, 4);

    // 3. Iconic Mochi Curved Eyes
    int16_t ey = hy - 2;
    if (is_blinking) {
        u8g2.drawHLine(cx - 12, ey + 2, 8);
        u8g2.drawHLine(cx + 4, ey + 2, 8);
    } else if (current_mood == MOOD_JAMMING || current_mood == MOOD_HAPPY) {
        // Enthusiastic curved eyes
        u8g2.drawCircle(cx - 8, ey, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
        u8g2.drawCircle(cx + 8, ey, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    } else {
        // Standard friendly eyes
        u8g2.drawRBox(cx - 12, ey - 3, 7, 6, 2);
        u8g2.drawRBox(cx + 5, ey - 3, 7, 6, 2);
        u8g2.setDrawColor(0);
        u8g2.drawPixel(cx - 10, ey - 2);
        u8g2.drawPixel(cx + 7, ey - 2);
        u8g2.setDrawColor(1);
    }

    // 4. Subtle Smile
    u8g2.drawHLine(cx - 3, hy + 6, 6);
#else
    (void)cx; (void)cy;
#endif
}

void AICompanion::renderNekoAccessories(int16_t cx, int16_t cy) {
#ifdef ARDUINO
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setDrawColor(1);

    // Cat ears atop .anim frame
    u8g2.drawTriangle(cx - 28, cy - 24, cx - 20, cy - 31, cx - 12, cy - 24);
    u8g2.drawTriangle(cx + 12, cy - 24, cx + 20, cy - 31, cx + 28, cy - 24);

    // Headphone side cups
    u8g2.drawRBox(cx - 40, cy - 10, 4, 18, 2);
    u8g2.drawRBox(cx + 36, cy - 10, 4, 18, 2);
#else
    (void)cx; (void)cy;
#endif
}
