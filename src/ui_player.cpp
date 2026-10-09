#include "ui_player.h"
#include "led_manager.h"
#include "battery.h"
#include "spectrum_analyzer.h"

UIPlayer::UIPlayer() :
    currentTrackIndex(0),
    inListMode(true),
    playbackMode(PLAY_MODE_ALL),
    volumeOverlayExpiry(0) {}

void UIPlayer::begin() {
    refreshTrackList();
    playbackMode = PLAY_MODE_ALL;
}

void UIPlayer::refreshTrackList() {
    if (!sdManager.isMounted()) sdManager.begin();
    trackList = sdManager.listMusicFiles();
    if (currentTrackIndex >= (int)trackList.size()) {
        currentTrackIndex = 0;
    }
}

void UIPlayer::playNextTrack() {
    if (trackList.empty()) return;
    if (playbackMode == PLAY_MODE_REPEAT_ONE) {
        audioPlayer.playFile(trackList[currentTrackIndex]);
    } else if (playbackMode == PLAY_MODE_SHUFFLE) {
        if (trackList.size() > 1) {
            int next = rand() % trackList.size();
            if (next == currentTrackIndex) next = (next + 1) % trackList.size();
            currentTrackIndex = next;
        }
        audioPlayer.playFile(trackList[currentTrackIndex]);
    } else if (playbackMode == PLAY_MODE_ALL) {
        currentTrackIndex = (currentTrackIndex + 1) % trackList.size();
        audioPlayer.playFile(trackList[currentTrackIndex]);
    } else { // PLAY_MODE_SINGLE
        audioPlayer.stop();
        inListMode = true;
    }
}

void UIPlayer::playPreviousTrack() {
    if (trackList.empty()) return;
    currentTrackIndex = (currentTrackIndex - 1 + trackList.size()) % trackList.size();
    audioPlayer.playFile(trackList[currentTrackIndex]);
}

void UIPlayer::setPlaybackMode(PlaybackMode mode) {
    playbackMode = mode;
}

PlaybackMode UIPlayer::getPlaybackMode() const {
    return playbackMode;
}

void UIPlayer::cyclePlaybackMode() {
    playbackMode = (PlaybackMode)((playbackMode + 1) % 4);
}

const char* UIPlayer::getPlaybackModeString() const {
    switch (playbackMode) {
        case PLAY_MODE_ALL:        return "[ALL]";
        case PLAY_MODE_REPEAT_ONE: return "[R-1]";
        case PLAY_MODE_SHUFFLE:    return "[SHF]";
        case PLAY_MODE_SINGLE:     return "[SGL]";
        default:                   return "[ALL]";
    }
}

void UIPlayer::showVolumeOverlay() {
    volumeOverlayExpiry = millis() + 1500;
}

bool UIPlayer::update() {
    // Check auto-advance on track completion
    if (!inListMode && audioPlayer.hasFinished()) {
        audioPlayer.clearFinished();
        playNextTrack();
    }

    ButtonEvent evt = buttonManager.update();

    // Global Long-Press CANCEL returns to Main Menu from anywhere in Player
    if (evt == BTN_EVENT_CANCEL_HOLD) {
        return false;
    }

    if (inListMode) {
        if (evt == BTN_EVENT_UP_PRESS) {
            if (!trackList.empty()) {
                currentTrackIndex = (currentTrackIndex - 1 + trackList.size()) % trackList.size();
            }
        } else if (evt == BTN_EVENT_DN_PRESS) {
            if (!trackList.empty()) {
                currentTrackIndex = (currentTrackIndex + 1) % trackList.size();
            }
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            if (!trackList.empty()) {
                inListMode = false;
                audioPlayer.playFile(trackList[currentTrackIndex]);
                ledManager.setMode(LedMode::BREATHING);
            } else {
                refreshTrackList();
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            // Short press CANCEL from track list returns to Main Menu
            return false;
        }
        renderTrackList();
    } else {
        if (evt == BTN_EVENT_SEL_PRESS) {
            if (audioPlayer.isPlaying()) {
                audioPlayer.pause();
                ledManager.setColor(CRGB::Orange);
            } else if (audioPlayer.isPaused()) {
                audioPlayer.resume();
                ledManager.setMode(LedMode::BREATHING);
            } else {
                audioPlayer.playFile(trackList[currentTrackIndex]);
                ledManager.setMode(LedMode::BREATHING);
            }
        } else if (evt == BTN_EVENT_OK_HOLD) {
            cyclePlaybackMode();
            ledManager.triggerPulse(CRGB::Magenta, 1, 100);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            playNextTrack();
            ledManager.triggerPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_PRESS) {
            playPreviousTrack();
            ledManager.triggerPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_HOLD) {
            audioPlayer.volumeUp(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_DN_HOLD) {
            audioPlayer.volumeDown(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            inListMode = true;
            ledManager.off();
        }

        renderPlayer();
    }
    return true;
}

void UIPlayer::renderTrackList() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "--- Q-TUNE MUSIC ---");

    if (trackList.empty()) {
        u8g2.drawStr(0, 30, "No WAV files in /music");
        u8g2.drawStr(0, 45, "SEL: Refresh");
        u8g2.drawStr(0, 58, "CANCEL: Main Menu");
    } else {
        int visibleItems = 4;
        int topIndex = currentTrackIndex;
        if (topIndex > (int)trackList.size() - visibleItems) {
            topIndex = trackList.size() - visibleItems;
        }
        if (topIndex < 0) topIndex = 0;

        for (int i = 0; i < visibleItems && (topIndex + i) < (int)trackList.size(); i++) {
            int idx = topIndex + i;
            int y = 24 + (i * 10);
            String displayName = trackList[idx];
            if (displayName.startsWith("/music/")) {
                displayName = displayName.substring(7);
            }
            if (idx == currentTrackIndex) {
                u8g2.drawStr(0, y, ">");
                u8g2.drawStr(10, y, displayName.c_str());
            } else {
                u8g2.drawStr(10, y, displayName.c_str());
            }
        }
    }
    display.sendBuffer();
}

void UIPlayer::renderPlayer() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Top status header
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "B:%d%% V:%d%% %s",
             battery.getPercentage(), audioPlayer.getVolume(), getPlaybackModeString());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    // Track Title
    if (!trackList.empty() && currentTrackIndex < (int)trackList.size()) {
        String displayName = trackList[currentTrackIndex];
        if (displayName.startsWith("/music/")) displayName = displayName.substring(7);
        if (displayName.length() > 20) {
            displayName = displayName.substring(0, 18) + "..";
        }
        u8g2.drawStr(0, 23, displayName.c_str());
    }

    // Playback state
    String stateStr = "■ STOPPED";
    if (audioPlayer.isPlaying()) stateStr = "▶ PLAYING (44.1k)";
    else if (audioPlayer.isPaused()) stateStr = "❚❚ PAUSED";
    u8g2.drawStr(0, 35, stateStr.c_str());

    // Time elapsed / total
    uint32_t posSec = audioPlayer.getPositionMs() / 1000;
    uint32_t durSec = audioPlayer.getDurationMs() / 1000;
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u / %02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
    u8g2.drawStr(0, 47, timeBuf);

    // Mini Spectrum Visualizer (Live WAV decoding tap)
    spectrumAnalyzer.sampleAudioStream();
    spectrumAnalyzer.renderMiniBars(u8g2, 92, 28, 34, 18);

    // Progress bar
    u8g2.drawFrame(0, 53, 128, 7);
    if (durSec > 0) {
        int progressWidth = (posSec * 126) / durSec;
        if (progressWidth > 126) progressWidth = 126;
        u8g2.drawBox(1, 54, progressWidth, 5);
    }

    // Volume popup overlay (active for 1.5s after volume adjustment)
    if (millis() < volumeOverlayExpiry) {
        u8g2.setDrawColor(0);
        u8g2.drawBox(18, 16, 92, 34);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(18, 16, 92, 34);

        char volStr[20];
        snprintf(volStr, sizeof(volStr), "VOLUME: %d%%", audioPlayer.getVolume());
        u8g2.drawStr(24, 28, volStr);

        u8g2.drawFrame(24, 34, 80, 8);
        int volBar = (audioPlayer.getVolume() * 76) / 100;
        if (volBar > 76) volBar = 76;
        u8g2.drawBox(26, 36, volBar, 4);
    }

    display.sendBuffer();
}

UIPlayer uiPlayer;
