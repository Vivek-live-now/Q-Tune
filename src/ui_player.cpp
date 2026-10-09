#include "ui_player.h"
#include "led_manager.h"
#include "battery.h"
#include "spectrum_analyzer.h"

UIPlayer::UIPlayer() :
    currentTrackIndex(0),
    trackScrollOffset(0),
    inListMode(true),
    playbackMode(PLAY_MODE_ALL),
    volumeOverlayExpiry(0) {}

void UIPlayer::begin() {
    refreshTrackList();
    trackScrollOffset = 0;
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
        ledManager.onPlaybackStop();
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
            Display::navigateMenu(currentTrackIndex, trackScrollOffset, (int)trackList.size(), -1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            Display::navigateMenu(currentTrackIndex, trackScrollOffset, (int)trackList.size(), +1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            if (!sdManager.isMounted()) {
                sdManager.remount();
                refreshTrackList();
                ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
            } else if (!trackList.empty()) {
                inListMode = false;
                audioPlayer.playFile(trackList[currentTrackIndex]);
                ledManager.onPlaybackStart();
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
                ledManager.onPlaybackPause();
            } else if (audioPlayer.isPaused()) {
                audioPlayer.resume();
                ledManager.onPlaybackResume();
            } else {
                audioPlayer.playFile(trackList[currentTrackIndex]);
                ledManager.onPlaybackStart();
            }
        } else if (evt == BTN_EVENT_OK_HOLD) {
            cyclePlaybackMode();
            ledManager.triggerButtonPulse(CRGB::Magenta, 1, 100);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            playNextTrack();
            ledManager.triggerButtonPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_PRESS) {
            playPreviousTrack();
            ledManager.triggerButtonPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_HOLD) {
            audioPlayer.volumeUp(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_DN_HOLD) {
            audioPlayer.volumeDown(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            inListMode = true;
            ledManager.onPlaybackStop();
        }

        renderPlayer();
    }
    return true;
}

void UIPlayer::renderTrackList() {
    display.clear();
    if (!sdManager.isMounted()) {
        display.drawTopStatusBar("Q-TUNES MUSIC", battery.getPercentage());
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 26, "SD Not Mounted");
        u8g2.drawStr(10, 40, "SEL: Mount SD Card");
        u8g2.drawStr(10, 54, "CANCEL: Main Menu");
    } else if (trackList.empty()) {
        display.drawTopStatusBar("Q-TUNES MUSIC", battery.getPercentage());
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 26, "No Tracks in /music");
        u8g2.drawStr(10, 40, "SEL: Refresh");
        u8g2.drawStr(10, 54, "CANCEL: Main Menu");
    } else {
        std::vector<const char*> itemPtrs(trackList.size());
        std::vector<String> displayNames(trackList.size());
        for (size_t i = 0; i < trackList.size(); i++) {
            String name = trackList[i];
            if (name.startsWith("/music/")) name = name.substring(7);
            if (name.length() > 18) name = name.substring(0, 16) + "..";
            displayNames[i] = name;
            itemPtrs[i] = displayNames[i].c_str();
        }
        char headerBuf[24];
        snprintf(headerBuf, sizeof(headerBuf), "%d/%d", currentTrackIndex + 1, (int)trackList.size());
        display.drawStandardMenu("Q-TUNES MUSIC", itemPtrs.data(), (int)trackList.size(), currentTrackIndex, trackScrollOffset, (const String*)nullptr, headerBuf);
    }
    display.sendBuffer();
}

void UIPlayer::renderPlayer() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Top status header (Battery, Output Mode, Playback Mode)
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "B:%d%% [%s] %s",
             battery.getPercentage(), audioPlayer.getOutputModeShortName(), getPlaybackModeString());
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
    if (audioPlayer.isPlaying()) {
        stateStr = audioPlayer.isFLAC() ? "▶ PLAYING (FLAC)" : "▶ PLAYING (WAV)";
    } else if (audioPlayer.isPaused()) {
        stateStr = "❚❚ PAUSED";
    }
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
