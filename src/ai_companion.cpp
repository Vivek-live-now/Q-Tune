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

const char* const AICompanion::ANIM_CATALOG[AICompanion::ANIM_CATALOG_COUNT] = {
    "adore", "angry", "blank", "blinding", "brave", "buzzing", "contempt", "crying", "dancing", "devil",
    "distracted", "dizzy", "down", "drowsy", "encouragement", "energetic", "enraged", "evil", "fast", "fierce",
    "furious", "giggle", "glowing", "growing", "gundam", "handsome", "happy", "hello", "intro", "irritated",
    "laughing", "left", "love", "menacing", "mistake", "playful", "police", "rain", "relaxed", "right",
    "rush", "scared", "serene", "shrink", "shy", "sick", "sleepy", "smile", "smirk", "smoke",
    "sneeze", "sobbing", "sparkle", "speed", "splash", "spraying", "squint", "surprised", "sushi", "swinging",
    "teasing", "tough", "weeping", "wink", "yawn"
};

static const float SPEED_PRESETS[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
static const char* const SPEED_LABELS[] = { "0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x" };
static const size_t SPEED_PRESET_COUNT = sizeof(SPEED_PRESETS) / sizeof(SPEED_PRESETS[0]);

AICompanion::AICompanion() :
    current_type(COMPANION_CAT),
    visual_mode(VISUAL_HYBRID),
    current_mood(MOOD_IDLE),
    sound_enabled(true),
    neko_overlay_on_anim(true),
    speed_index(2),
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
    pupil_dy(0.0f),
    current_anim_index(0),
    autonomous_enabled(true),
    autonomous_timer(0.0f),
    next_autonomous_interval(12.0f),
    face_toast_end_time(0)
{
    memset(current_anim_name, 0, sizeof(current_anim_name));
    memset(last_face_name, 0, sizeof(last_face_name));
}

bool AICompanion::begin() {
    loadSettings();
    animEngine.setSpeedMultiplier(getSpeedMultiplier());
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
        speed_index = prefs.getUChar("spd", 2);
        if (speed_index >= SPEED_PRESET_COUNT) speed_index = 2;
        animEngine.setSpeedMultiplier(SPEED_PRESETS[speed_index]);
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
        prefs.putUChar("spd", speed_index);
        prefs.end();
    }
#endif
}

void AICompanion::cycleSpeed() {
    speed_index = (speed_index + 1) % SPEED_PRESET_COUNT;
    animEngine.setSpeedMultiplier(SPEED_PRESETS[speed_index]);
    saveSettings();
}

void AICompanion::setSpeedIndex(uint8_t idx) {
    if (idx < SPEED_PRESET_COUNT) {
        speed_index = idx;
        animEngine.setSpeedMultiplier(SPEED_PRESETS[speed_index]);
        saveSettings();
    }
}

float AICompanion::getSpeedMultiplier() const {
    if (speed_index >= SPEED_PRESET_COUNT) return 1.0f;
    return SPEED_PRESETS[speed_index];
}

const char* AICompanion::getSpeedString() const {
    if (speed_index >= SPEED_PRESET_COUNT) return "1.0x";
    return SPEED_LABELS[speed_index];
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
        case MOOD_WINK: return "WINK";
        case MOOD_DIZZY: return "DIZZY";
        case MOOD_SMIRK: return "SMIRK";
        case MOOD_SAD: return "SAD";
        case MOOD_ENERGETIC: return "ENERGETIC";
        default: return "NORMAL";
    }
}

void AICompanion::cycleNextAnim() {
    current_anim_index = (current_anim_index + 1) % ANIM_CATALOG_COUNT;
    const char* animName = ANIM_CATALOG[current_anim_index];
    playAnim(animName, 8000);
    strncpy(last_face_name, animName, sizeof(last_face_name) - 1);
    last_face_name[sizeof(last_face_name) - 1] = '\0';
    face_toast_end_time = millis() + 2000;
}

void AICompanion::cyclePrevAnim() {
    current_anim_index = (current_anim_index == 0) ? (ANIM_CATALOG_COUNT - 1) : (current_anim_index - 1);
    const char* animName = ANIM_CATALOG[current_anim_index];
    playAnim(animName, 8000);
    strncpy(last_face_name, animName, sizeof(last_face_name) - 1);
    last_face_name[sizeof(last_face_name) - 1] = '\0';
    face_toast_end_time = millis() + 2000;
}

void AICompanion::cycleNextFace() {
    static int global_face_idx = 0;
    global_face_idx = (global_face_idx + 1) % (MOOD_COUNT + ANIM_CATALOG_COUNT);
    if (global_face_idx < MOOD_COUNT) {
        stopAnim();
        setMood((CompanionMood)global_face_idx, 8000);
        strncpy(last_face_name, getMoodName(), sizeof(last_face_name) - 1);
    } else {
        size_t a_idx = global_face_idx - MOOD_COUNT;
        current_anim_index = a_idx;
        const char* animName = ANIM_CATALOG[a_idx];
        playAnim(animName, 8000);
        strncpy(last_face_name, animName, sizeof(last_face_name) - 1);
    }
    last_face_name[sizeof(last_face_name) - 1] = '\0';
    face_toast_end_time = millis() + 2000;
}

void AICompanion::cyclePrevFace() {
    static int global_face_idx = 0;
    global_face_idx = (global_face_idx == 0) ? (MOOD_COUNT + ANIM_CATALOG_COUNT - 1) : (global_face_idx - 1);
    if (global_face_idx < MOOD_COUNT) {
        stopAnim();
        setMood((CompanionMood)global_face_idx, 8000);
        strncpy(last_face_name, getMoodName(), sizeof(last_face_name) - 1);
    } else {
        size_t a_idx = global_face_idx - MOOD_COUNT;
        current_anim_index = a_idx;
        const char* animName = ANIM_CATALOG[a_idx];
        playAnim(animName, 8000);
        strncpy(last_face_name, animName, sizeof(last_face_name) - 1);
    }
    last_face_name[sizeof(last_face_name) - 1] = '\0';
    face_toast_end_time = millis() + 2000;
}

const char* AICompanion::getCurrentFaceDisplayName() const {
    if (isPlayingAnim() && strlen(current_anim_name) > 0) {
        return current_anim_name;
    }
    return getMoodName();
}

bool AICompanion::isToastActive() const {
    return (face_toast_end_time > 0 && millis() < face_toast_end_time);
}

bool AICompanion::playAnim(const char* name, uint32_t duration_ms) {
    if (!name || strlen(name) == 0) return false;
    strncpy(current_anim_name, name, sizeof(current_anim_name) - 1);
    current_anim_name[sizeof(current_anim_name) - 1] = '\0';

    bool ok = animEngine.open(name);
    if (ok) {
        animEngine.setSpeedMultiplier(getSpeedMultiplier());
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
    if (audioPlayer.isPlaying()) return; // Avoid audio stutter during music playback
    // Pleasant rising/falling frequency sweep simulating a cute meow
    audioPlayer.playTestTone(880, 70);
    audioPlayer.playTestTone(1250, 90);
    audioPlayer.playTestTone(980, 80);
#endif
}

void AICompanion::playPurr() {
#ifdef ARDUINO
    if (!sound_enabled) return;
    if (audioPlayer.isPlaying()) return; // Avoid audio stutter during music playback
    // Low frequency gentle rhythmic pulses
    for (int i = 0; i < 3; i++) {
        audioPlayer.playTestTone(130, 40);
    }
#endif
}

void AICompanion::playChirp() {
#ifdef ARDUINO
    if (!sound_enabled) return;
    if (audioPlayer.isPlaying()) return; // Avoid audio stutter during music playback
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
        } else if (visual_mode == VISUAL_ANIM_ONLY) {
            cycleNextAnim();
        }
    }

    // In VISUAL_ANIM_ONLY mode, ensure continuous playback through all 65 anims
    if (visual_mode == VISUAL_ANIM_ONLY && !isPlayingAnim()) {
        cycleNextAnim();
    }

    // Autonomous organic emotion & anim rotation (living companion)
    if (autonomous_enabled) {
        autonomous_timer += dt;
        if (autonomous_timer >= next_autonomous_interval) {
            autonomous_timer = 0.0f;
            next_autonomous_interval = 11.0f + (float)(rand() % 8); // 11-18 seconds

            if (visual_mode == VISUAL_ANIM_ONLY) {
                cycleNextAnim();
            } else if (visual_mode == VISUAL_HYBRID && !isPlayingAnim()) {
#ifdef ARDUINO
                bool isPlaying = audioPlayer.isPlaying();
                float bass = spectrumAnalyzer.getBassLevel();
#else
                bool isPlaying = false;
                float bass = 0.0f;
#endif
                int choice = rand() % 100;
                if (isPlaying) {
                    if (bass > 0.35f || choice < 45) {
                        // High energy beat dropped!
                        const char* energeticAnims[] = {
                            "dancing", "energetic", "sparkle", "fast", "speed",
                            "buzzing", "furious", "gundam", "glowing", "rush", "swinging"
                        };
                        int n = sizeof(energeticAnims) / sizeof(energeticAnims[0]);
                        if (choice % 3 == 0) {
                            setMood(MOOD_BASS_BOOSTED, 5000);
                        } else if (choice % 3 == 1) {
                            setMood(MOOD_JAMMING, 5000);
                        } else {
                            playAnim(energeticAnims[rand() % n], 4500);
                        }
                    } else {
                        // Chill melodic groove
                        const char* chillAnims[] = {
                            "relaxed", "serene", "happy", "smile", "giggle", "wink", "adore", "love", "handsome"
                        };
                        int n = sizeof(chillAnims) / sizeof(chillAnims[0]);
                        if (choice % 2 == 0) {
                            setMood((choice % 4 == 0) ? MOOD_LOVE : MOOD_HAPPY, 5000);
                        } else {
                            playAnim(chillAnims[rand() % n], 4500);
                        }
                    }
                } else {
                    // Resting idle companion
                    const char* idleAnims[] = {
                        "yawn", "sleepy", "drowsy", "distracted", "left", "right",
                        "hello", "shy", "smirk", "sneeze", "teasing", "shrink", "growing"
                    };
                    int n = sizeof(idleAnims) / sizeof(idleAnims[0]);
                    int rMood = choice % 6;
                    if (rMood == 0) setMood(MOOD_SLEEPY, 6000);
                    else if (rMood == 1) setMood(MOOD_THINKING, 5000);
                    else if (rMood == 2) setMood(MOOD_WINK, 4000);
                    else if (rMood == 3) setMood(MOOD_SMIRK, 4000);
                    else if (rMood == 4) setMood(MOOD_HAPPY, 4000);
                    else playAnim(idleAnims[rand() % n], 4000);
                }
            }
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
        if (!showAnim) {
            cycleNextAnim();
            showAnim = isPlayingAnim();
        }
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

    // 1. Pointed Cat Ears with Mood-Specific Ear Physics
    int16_t eTwitch = (int16_t)ear_twitch;
    int16_t earH = 26;
    if (current_mood == MOOD_SLEEPY) {
        earH = 20; // Drooping relaxed ears
    } else if (current_mood == MOOD_SURPRISED) {
        earH = 28; // Alert perked ears
    }

    if (current_mood == MOOD_ANGRY) {
        // Flattened airplane ears
        u8g2.drawTriangle(cx - 24, hy - 10, cx - 18, hy - 18, cx - 10, hy - 14);
        u8g2.drawTriangle(cx + 10, hy - 14, cx + 18, hy - 18, cx + 24, hy - 10);
    } else {
        // Left Ear
        u8g2.drawTriangle(cx - 20, hy - 14, cx - 14 + eTwitch, hy - earH, cx - 6, hy - 14);
        u8g2.drawTriangle(cx - 18, hy - 14, cx - 14 + eTwitch, hy - (earH - 4), cx - 9, hy - 14); // inner
        // Right Ear
        u8g2.drawTriangle(cx + 6, hy - 14, cx + 14 - eTwitch, hy - earH, cx + 20, hy - 14);
        u8g2.drawTriangle(cx + 9, hy - 14, cx + 14 - eTwitch, hy - (earH - 4), cx + 18, hy - 14); // inner
    }

    // 2. Soft Rounded Cat Face
    u8g2.drawRFrame(cx - 24, hy - 14, 48, 28, 7);

    // 3. Studio DJ Headphones
    // Over-ear Headband Curved Arch (connects top of ear cups over head smoothly)
    u8g2.drawCircle(cx, hy - 10, 26, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(cx, hy - 11, 26, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);

    // Left Cushioned Ear Cup
    u8g2.drawRBox(cx - 28, hy - 10, 5, 20, 2);
    // Right Cushioned Ear Cup
    u8g2.drawRBox(cx + 23, hy - 10, 5, 20, 2);

    // Mini Live Spectrum Equalizer on Left & Right Ear Cups
    float bass = spectrumAnalyzer.getBassLevel();
    float rms = spectrumAnalyzer.getRMSLevel();
    int bHeight1 = (int)(bass * 12.0f);
    int bHeight2 = (int)(rms * 14.0f);
    if (bHeight1 > 14) bHeight1 = 14;
    if (bHeight2 > 14) bHeight2 = 14;

    u8g2.setDrawColor(0); // Cutout inside ear cups
    if (bHeight1 > 0) u8g2.drawVLine(cx - 26, hy + 7 - bHeight1, bHeight1);
    if (bHeight2 > 0) u8g2.drawVLine(cx + 25, hy + 7 - bHeight2, bHeight2);
    u8g2.setDrawColor(1);

    // 4. Kawaii Anime Eyes with Rich Facial Mood Expressions
    int16_t ey = hy - 3;
    int16_t pdx = (int16_t)pupil_dx;
    int16_t pdy = (int16_t)pupil_dy;
    if (pdx > 2) pdx = 2;
    if (pdx < -2) pdx = -2;
    if (pdy > 1) pdy = 1;
    if (pdy < -1) pdy = -1;

    if (is_blinking) {
        // Closed slit blink
        u8g2.drawHLine(cx - 13, ey + 2, 7);
        u8g2.drawHLine(cx + 7, ey + 2, 7);
    } else if (current_mood == MOOD_HAPPY) {
        // Happy curved smile eyes ^ ^ with blushing cheeks ///
        u8g2.drawLine(cx - 14, ey + 3, cx - 10, ey - 2);
        u8g2.drawLine(cx - 10, ey - 2, cx - 6, ey + 3);
        u8g2.drawLine(cx - 14, ey + 4, cx - 10, ey - 1);
        u8g2.drawLine(cx - 10, ey - 1, cx - 6, ey + 4);
        u8g2.drawLine(cx + 6, ey + 3, cx + 10, ey - 2);
        u8g2.drawLine(cx + 10, ey - 2, cx + 14, ey + 3);
        u8g2.drawLine(cx + 6, ey + 4, cx + 10, ey - 1);
        u8g2.drawLine(cx + 10, ey - 1, cx + 14, ey + 4);
        // Blushing cheeks ///
        u8g2.drawPixel(cx - 17, ey + 6); u8g2.drawPixel(cx - 15, ey + 6);
        u8g2.drawPixel(cx + 15, ey + 6); u8g2.drawPixel(cx + 17, ey + 6);
    } else if (current_mood == MOOD_JAMMING) {
        // Intense groove rhythm squint eyes > <
        u8g2.drawLine(cx - 14, ey - 2, cx - 7, ey + 1);
        u8g2.drawLine(cx - 7, ey + 1, cx - 14, ey + 4);
        u8g2.drawLine(cx + 14, ey - 2, cx + 7, ey + 1);
        u8g2.drawLine(cx + 7, ey + 1, cx + 14, ey + 4);
    } else if (current_mood == MOOD_BASS_BOOSTED) {
        // Ultra-cool DJ shades with white diagonal reflection slashes
        u8g2.drawRBox(cx - 15, ey - 4, 11, 8, 2);
        u8g2.drawRBox(cx + 4, ey - 4, 11, 8, 2);
        u8g2.drawHLine(cx - 4, ey - 2, 8);
        u8g2.drawHLine(cx - 4, ey - 1, 8);
        u8g2.setDrawColor(0);
        u8g2.drawLine(cx - 13, ey - 2, cx - 11, ey + 2);
        u8g2.drawLine(cx - 10, ey - 2, cx - 8, ey + 2);
        u8g2.drawLine(cx + 6, ey - 2, cx + 8, ey + 2);
        u8g2.drawLine(cx + 9, ey - 2, cx + 11, ey + 2);
        u8g2.setDrawColor(1);
    } else if (current_mood == MOOD_LOVE) {
        // Sparkling heart-shaped eyes ♥ ♥ with soft blush
        u8g2.drawDisc(cx - 12, ey - 1, 2);
        u8g2.drawDisc(cx - 8, ey - 1, 2);
        u8g2.drawTriangle(cx - 14, ey, cx - 6, ey, cx - 10, ey + 4);
        u8g2.drawDisc(cx + 8, ey - 1, 2);
        u8g2.drawDisc(cx + 12, ey - 1, 2);
        u8g2.drawTriangle(cx + 6, ey, cx + 14, ey, cx + 10, ey + 4);
        u8g2.drawPixel(cx - 16, ey + 6); u8g2.drawPixel(cx - 14, ey + 6);
        u8g2.drawPixel(cx + 14, ey + 6); u8g2.drawPixel(cx + 16, ey + 6);
    } else if (current_mood == MOOD_SLEEPY) {
        // Sleepy downward curved eyelids u u and floating zZ
        u8g2.drawCircle(cx - 10, ey, 4, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawCircle(cx + 10, ey, 4, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        int16_t zOff = ((int16_t)(anim_timer * 2.5f)) % 4;
        u8g2.drawPixel(cx + 15, hy - 16 - zOff);
        u8g2.drawPixel(cx + 16, hy - 16 - zOff);
        u8g2.drawPixel(cx + 17, hy - 15 - zOff);
        u8g2.drawPixel(cx + 15, hy - 14 - zOff);
        u8g2.drawPixel(cx + 16, hy - 14 - zOff);
    } else if (current_mood == MOOD_SURPRISED) {
        // Wide anime shocked frames with tiny pinpoint pupils (O O)
        u8g2.drawCircle(cx - 10, ey, 5);
        u8g2.drawCircle(cx + 10, ey, 5);
        u8g2.drawPixel(cx - 10, ey);
        u8g2.drawPixel(cx + 10, ey);
    } else if (current_mood == MOOD_ANGRY) {
        // Slanted furrowed intense eyes \ / with angry vein mark 💢
        u8g2.drawLine(cx - 15, ey - 3, cx - 6, ey + 2);
        u8g2.drawLine(cx - 15, ey - 2, cx - 6, ey + 3);
        u8g2.drawLine(cx - 15, ey + 3, cx - 6, ey + 3);
        u8g2.drawLine(cx + 6, ey + 2, cx + 15, ey - 3);
        u8g2.drawLine(cx + 6, ey + 3, cx + 15, ey - 2);
        u8g2.drawLine(cx + 6, ey + 3, cx + 15, ey + 3);
        // Angry vein mark 💢
        u8g2.drawPixel(cx + 16, hy - 10);
        u8g2.drawPixel(cx + 17, hy - 11);
        u8g2.drawPixel(cx + 18, hy - 10);
        u8g2.drawPixel(cx + 17, hy - 9);
    } else if (current_mood == MOOD_THINKING) {
        // Curious asymmetrical eyes: wide up-gazing left eye, squinted right eye, floating ?
        u8g2.drawCircle(cx - 10, ey, 4);
        u8g2.drawPixel(cx - 12, ey - 2);
        u8g2.drawHLine(cx + 6, ey + 1, 7);
        u8g2.drawLine(cx + 6, ey - 2, cx + 13, ey);
        // Question mark ?
        u8g2.drawPixel(cx + 17, hy - 13);
        u8g2.drawPixel(cx + 18, hy - 14);
        u8g2.drawPixel(cx + 17, hy - 11);
        u8g2.drawPixel(cx + 17, hy - 9);
    } else if (current_mood == MOOD_WINK) {
        // Playful wink: winking left arc ^, sparkling wide right anime eye
        u8g2.drawLine(cx - 14, ey + 2, cx - 10, ey - 1);
        u8g2.drawLine(cx - 10, ey - 1, cx - 6, ey + 2);
        u8g2.drawLine(cx - 14, ey + 3, cx - 10, ey);
        u8g2.drawLine(cx - 10, ey, cx - 6, ey + 3);
        u8g2.drawDisc(cx + 10, ey, 4);
        u8g2.setDrawColor(0);
        u8g2.drawPixel(cx + 9, ey - 1);
        u8g2.setDrawColor(1);
    } else if (current_mood == MOOD_DIZZY) {
        // Dizzy spinning cross eyes X X
        u8g2.drawLine(cx - 13, ey - 3, cx - 7, ey + 3);
        u8g2.drawLine(cx - 13, ey + 3, cx - 7, ey - 3);
        u8g2.drawLine(cx + 7, ey - 3, cx + 13, ey + 3);
        u8g2.drawLine(cx + 7, ey + 3, cx + 13, ey - 3);
    } else if (current_mood == MOOD_SMIRK) {
        // Sassy half-closed lids ¬ ¬
        u8g2.drawHLine(cx - 14, ey - 1, 8);
        u8g2.drawCircle(cx - 10, ey + 1, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawHLine(cx + 6, ey - 1, 8);
        u8g2.drawCircle(cx + 10, ey + 1, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
    } else if (current_mood == MOOD_SAD) {
        // Teary drooping eyes with streaming tears
        u8g2.drawLine(cx - 14, ey + 1, cx - 6, ey - 2);
        u8g2.drawLine(cx + 6, ey - 2, cx + 14, ey + 1);
        u8g2.drawCircle(cx - 10, ey + 1, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawCircle(cx + 10, ey + 1, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawVLine(cx - 10, ey + 5, 4);
        u8g2.drawVLine(cx + 10, ey + 5, 4);
    } else if (current_mood == MOOD_ENERGETIC) {
        // Star diamond anime sparkle pupils * *
        u8g2.drawVLine(cx - 10, ey - 4, 9);
        u8g2.drawHLine(cx - 14, ey, 9);
        u8g2.drawPixel(cx - 12, ey - 2); u8g2.drawPixel(cx - 8, ey - 2);
        u8g2.drawPixel(cx - 12, ey + 2); u8g2.drawPixel(cx - 8, ey + 2);
        u8g2.drawVLine(cx + 10, ey - 4, 9);
        u8g2.drawHLine(cx + 6, ey, 9);
        u8g2.drawPixel(cx + 8, ey - 2); u8g2.drawPixel(cx + 12, ey - 2);
        u8g2.drawPixel(cx + 8, ey + 2); u8g2.drawPixel(cx + 12, ey + 2);
    } else {
        // Big round shiny anime eyes with lively gaze tracking & dual sparkle highlights
        u8g2.drawDisc(cx - 10 + pdx, ey + pdy, 4);
        u8g2.drawDisc(cx + 10 + pdx, ey + pdy, 4);
        u8g2.setDrawColor(0); // White pupil sparkle
        u8g2.drawPixel(cx - 11 + pdx, ey - 1 + pdy);
        u8g2.drawPixel(cx + 9 + pdx, ey - 1 + pdy);
        if (current_mood == MOOD_LISTENING) {
            u8g2.drawPixel(cx - 9 + pdx, ey + 1 + pdy);
            u8g2.drawPixel(cx + 11 + pdx, ey + 1 + pdy);
        }
        u8g2.setDrawColor(1);
    }

    // 5. Bass-Reactive Cheek Whiskers (neatly nestled under cheeks, clear of headphones)
    int16_t wVibe = (int16_t)whisker_vibe;
    if (wVibe > 3) wVibe = 3;
    if (wVibe < -3) wVibe = -3;
    // Left Whiskers
    u8g2.drawLine(cx - 15, hy + 3, cx - 22, hy + 2 + wVibe);
    u8g2.drawLine(cx - 16, hy + 6, cx - 23, hy + 6);
    u8g2.drawLine(cx - 15, hy + 9, cx - 22, hy + 10 - wVibe);
    // Right Whiskers
    u8g2.drawLine(cx + 15, hy + 3, cx + 22, hy + 2 + wVibe);
    u8g2.drawLine(cx + 16, hy + 6, cx + 23, hy + 6);
    u8g2.drawLine(cx + 15, hy + 9, cx + 22, hy + 10 - wVibe);

    // 6. Cute :3 Cat Mouth with Mood Expressions
    u8g2.drawPixel(cx, hy + 3); // tiny nose
    if (current_mood == MOOD_ANGRY) {
        // Cute vampire kitty fangs ^w^
        u8g2.drawLine(cx - 4, hy + 5, cx - 2, hy + 7);
        u8g2.drawLine(cx - 2, hy + 7, cx, hy + 5);
        u8g2.drawLine(cx, hy + 5, cx + 2, hy + 7);
        u8g2.drawLine(cx + 2, hy + 7, cx + 4, hy + 5);
        u8g2.drawPixel(cx - 2, hy + 8);
        u8g2.drawPixel(cx + 2, hy + 8);
    } else if (current_mood == MOOD_BASS_BOOSTED) {
        // Cool smirk with side fang :>
        u8g2.drawLine(cx - 3, hy + 6, cx + 3, hy + 5);
        u8g2.drawPixel(cx + 2, hy + 7);
    } else if (current_mood == MOOD_SURPRISED) {
        // Shocked circle mouth o
        u8g2.drawCircle(cx, hy + 6, 2);
    } else if (current_mood == MOOD_DIZZY) {
        // Dizzy wavy squiggly mouth ~
        u8g2.drawPixel(cx - 3, hy + 6);
        u8g2.drawPixel(cx - 2, hy + 5);
        u8g2.drawPixel(cx - 1, hy + 6);
        u8g2.drawPixel(cx, hy + 7);
        u8g2.drawPixel(cx + 1, hy + 6);
        u8g2.drawPixel(cx + 2, hy + 5);
        u8g2.drawPixel(cx + 3, hy + 6);
    } else if (current_mood == MOOD_SMIRK) {
        // Sly raised corner smirk :>
        u8g2.drawLine(cx - 2, hy + 6, cx + 1, hy + 6);
        u8g2.drawLine(cx + 1, hy + 6, cx + 4, hy + 4);
    } else if (current_mood == MOOD_SAD) {
        // Sad downturned mouth :(
        u8g2.drawCircle(cx, hy + 8, 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    } else if (current_mood == MOOD_HAPPY || current_mood == MOOD_ENERGETIC || (audioPlayer.isPlaying() && (rms > 0.4f))) {
        // Singing open happy mouth :3 or :D
        u8g2.drawCircle(cx, hy + 6, 2, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawHLine(cx - 2, hy + 5, 5);
    } else {
        // Kawaii :3 classic w-mouth
        u8g2.drawPixel(cx - 3, hy + 5);
        u8g2.drawPixel(cx - 2, hy + 6);
        u8g2.drawPixel(cx - 1, hy + 5);
        u8g2.drawPixel(cx + 1, hy + 5);
        u8g2.drawPixel(cx + 2, hy + 6);
        u8g2.drawPixel(cx + 3, hy + 5);
    }

    // 7. Swishing Tail (bounds-safe)
    int16_t tSwish = (int16_t)tail_swish;
    if (tSwish > 5) tSwish = 5;
    if (tSwish < -5) tSwish = -5;
    u8g2.drawLine(cx + 18, hy + 12, cx + 22 + tSwish, hy + 18);
    u8g2.drawCircle(cx + 22 + tSwish, hy + 18, 2);
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

    // 3. Iconic Mochi Curved Eyes with Rich Expressions
    int16_t ey = hy - 2;
    int16_t pdx = (int16_t)pupil_dx;
    if (pdx > 1) pdx = 1;
    if (pdx < -1) pdx = -1;

    if (is_blinking) {
        u8g2.drawHLine(cx - 12, ey + 2, 8);
        u8g2.drawHLine(cx + 4, ey + 2, 8);
    } else if (current_mood == MOOD_HAPPY) {
        // Enthusiastic curved eyes ^ ^
        u8g2.drawCircle(cx - 8, ey, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
        u8g2.drawCircle(cx + 8, ey, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    } else if (current_mood == MOOD_JAMMING) {
        // Grooving rhythm squint > <
        u8g2.drawLine(cx - 12, ey - 2, cx - 6, ey + 1);
        u8g2.drawLine(cx - 6, ey + 1, cx - 12, ey + 4);
        u8g2.drawLine(cx + 12, ey - 2, cx + 6, ey + 1);
        u8g2.drawLine(cx + 6, ey + 1, cx + 12, ey + 4);
    } else if (current_mood == MOOD_BASS_BOOSTED) {
        // Cool visor sunglasses
        u8g2.drawBox(cx - 14, ey - 3, 28, 6);
        u8g2.setDrawColor(0);
        u8g2.drawLine(cx - 12, ey - 2, cx - 9, ey + 2);
        u8g2.drawLine(cx + 7, ey - 2, cx + 10, ey + 2);
        u8g2.setDrawColor(1);
    } else if (current_mood == MOOD_LOVE) {
        // Visor heart eyes ♥ ♥
        u8g2.drawDisc(cx - 10, ey - 1, 2);
        u8g2.drawDisc(cx - 6, ey - 1, 2);
        u8g2.drawTriangle(cx - 12, ey, cx - 4, ey, cx - 8, ey + 4);
        u8g2.drawDisc(cx + 6, ey - 1, 2);
        u8g2.drawDisc(cx + 10, ey - 1, 2);
        u8g2.drawTriangle(cx + 4, ey, cx + 12, ey, cx + 8, ey + 4);
    } else if (current_mood == MOOD_SLEEPY) {
        // Sleepy visor lines u u
        u8g2.drawHLine(cx - 11, ey + 1, 7);
        u8g2.drawHLine(cx + 5, ey + 1, 7);
        u8g2.drawPixel(cx + 16, hy - 16);
        u8g2.drawPixel(cx + 17, hy - 16);
    } else if (current_mood == MOOD_SURPRISED) {
        // Visor wide shock eyes O O
        u8g2.drawCircle(cx - 8, ey, 4);
        u8g2.drawCircle(cx + 8, ey, 4);
        u8g2.drawPixel(cx - 8, ey);
        u8g2.drawPixel(cx + 8, ey);
    } else if (current_mood == MOOD_ANGRY) {
        // Angled visor lines \ /
        u8g2.drawLine(cx - 13, ey - 2, cx - 5, ey + 2);
        u8g2.drawLine(cx + 5, ey + 2, cx + 13, ey - 2);
    } else if (current_mood == MOOD_THINKING) {
        // Asymmetric visor
        u8g2.drawCircle(cx - 8, ey, 4);
        u8g2.drawPixel(cx - 9, ey - 1);
        u8g2.drawHLine(cx + 5, ey + 1, 7);
    } else if (current_mood == MOOD_WINK) {
        // Wink ^ -
        u8g2.drawLine(cx - 11, ey + 1, cx - 8, ey - 1);
        u8g2.drawLine(cx - 8, ey - 1, cx - 5, ey + 1);
        u8g2.drawRBox(cx + 5, ey - 3, 7, 6, 2);
    } else if (current_mood == MOOD_DIZZY) {
        // Cross eyes X X
        u8g2.drawLine(cx - 11, ey - 2, cx - 5, ey + 2);
        u8g2.drawLine(cx - 11, ey + 2, cx - 5, ey - 2);
        u8g2.drawLine(cx + 5, ey - 2, cx + 11, ey + 2);
        u8g2.drawLine(cx + 5, ey + 2, cx + 11, ey - 2);
    } else {
        // Standard friendly eyes with gaze tracking
        u8g2.drawRBox(cx - 12 + pdx, ey - 3, 7, 6, 2);
        u8g2.drawRBox(cx + 5 + pdx, ey - 3, 7, 6, 2);
        u8g2.setDrawColor(0);
        u8g2.drawPixel(cx - 10 + pdx, ey - 2);
        u8g2.drawPixel(cx + 7 + pdx, ey - 2);
        u8g2.setDrawColor(1);
    }

    // 4. Mood-Adaptive Mouth
    if (current_mood == MOOD_HAPPY || current_mood == MOOD_JAMMING || current_mood == MOOD_ENERGETIC) {
        u8g2.drawCircle(cx, hy + 5, 2, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
    } else if (current_mood == MOOD_SURPRISED) {
        u8g2.drawCircle(cx, hy + 5, 2);
    } else {
        u8g2.drawHLine(cx - 3, hy + 6, 6);
    }
#else
    (void)cx; (void)cy;
#endif
}

void AICompanion::renderNekoAccessories(int16_t cx, int16_t cy) {
#ifdef ARDUINO
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setDrawColor(1);

    if (cx >= 50) {
        // Full screen 128x64 mode (cx ~ 64, cy ~ 32)
        // Cat ears atop .anim frame
        u8g2.drawTriangle(cx - 28, cy - 24, cx - 20, cy - 31, cx - 12, cy - 24);
        u8g2.drawTriangle(cx + 12, cy - 24, cx + 20, cy - 31, cx + 28, cy - 24);

        // Headphone side cups
        u8g2.drawRBox(cx - 40, cy - 10, 4, 18, 2);
        u8g2.drawRBox(cx + 36, cy - 10, 4, 18, 2);
    } else {
        // Split-screen companion preview mode (cx ~ 30, cy ~ 37)
        // Scaled accessories fitting within 60px preview
        u8g2.drawTriangle(cx - 18, cy - 22, cx - 13, cy - 28, cx - 8, cy - 22);
        u8g2.drawTriangle(cx + 8, cy - 22, cx + 13, cy - 28, cx + 18, cy - 22);

        // Compact headphone side cups within bounds
        u8g2.drawRBox(cx - 27, cy - 9, 3, 16, 1);
        u8g2.drawRBox(cx + 24, cy - 9, 3, 16, 1);
    }
#else
    (void)cx; (void)cy;
#endif
}
