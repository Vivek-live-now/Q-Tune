#include "ui_player.h"

UIPlayer::UIPlayer() : currentTrackIndex(0), inListMode(true) {}

void UIPlayer::begin() {
    refreshTrackList();
}

void UIPlayer::refreshTrackList() {
    if (!sdManager.isMounted()) sdManager.begin();
    trackList = sdManager.listMusicFiles();
    if (currentTrackIndex >= (int)trackList.size()) {
        currentTrackIndex = 0;
    }
}

void UIPlayer::update() {
    ButtonEvent evt = buttonManager.update();

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
            } else {
                refreshTrackList();
            }
        }
        renderTrackList();
    } else {
        if (evt == BTN_EVENT_SEL_PRESS) {
            if (audioPlayer.isPlaying()) {
                audioPlayer.pause();
            } else if (audioPlayer.isPaused()) {
                audioPlayer.resume();
            } else {
                audioPlayer.playFile(trackList[currentTrackIndex]);
            }
        } else if (evt == BTN_EVENT_DN_PRESS) {
            if (!trackList.empty()) {
                currentTrackIndex = (currentTrackIndex + 1) % trackList.size();
                audioPlayer.playFile(trackList[currentTrackIndex]);
            }
        } else if (evt == BTN_EVENT_UP_PRESS) {
            inListMode = true;
        }

        audioPlayer.update();
        renderPlayer();
    }
}

void UIPlayer::renderTrackList() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "--- Q-TUNE MUSIC ---");

    if (trackList.empty()) {
        u8g2.drawStr(0, 30, "No WAV files in /music");
        u8g2.drawStr(0, 50, "SEL: Refresh");
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

    u8g2.drawStr(0, 10, "NOW PLAYING:");
    if (!trackList.empty() && currentTrackIndex < (int)trackList.size()) {
        String displayName = trackList[currentTrackIndex];
        if (displayName.startsWith("/music/")) displayName = displayName.substring(7);
        u8g2.drawStr(0, 24, displayName.c_str());
    }

    String stateStr = "STOPPED";
    if (audioPlayer.isPlaying()) stateStr = "PLAYING";
    else if (audioPlayer.isPaused()) stateStr = "PAUSED";
    u8g2.drawStr(0, 38, stateStr.c_str());

    uint32_t posSec = audioPlayer.getPositionMs() / 1000;
    uint32_t durSec = audioPlayer.getDurationMs() / 1000;
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u / %02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
    u8g2.drawStr(0, 50, timeBuf);

    if (durSec > 0) {
        int progressWidth = (posSec * 128) / durSec;
        u8g2.drawBox(0, 56, progressWidth, 6);
    }
    u8g2.drawFrame(0, 56, 128, 6);

    display.sendBuffer();
}

UIPlayer uiPlayer;
