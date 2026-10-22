#include "ui_player.h"
#include "led_manager.h"
#include "battery.h"
#include "spectrum_analyzer.h"
#include "wifi_streamer.h"

UIPlayer::UIPlayer() :
    currentTrackIndex(0),
    trackScrollOffset(0),
    inListMode(true),
    playbackMode(PLAY_MODE_ALL),
    volumeOverlayExpiry(0),
    currentView(VIEW_CATEGORIES),
    previousView(VIEW_CATEGORIES),
    currentPage(PAGE_NOW_PLAYING),
    manualLyricIndex(0),
    categorySelection(0),
    categoryScrollOffset(0),
    currentCategoryTitle("Q-TUNES MUSIC"),
    currentFolderPath("/music"),
    folderSelection(0),
    folderScrollOffset(0),
    settingsSelection(0),
    settingsScrollOffset(0),
    eqBandSelection(0),
    artSettingsRow(0),
    playerTrackStartTime(0),
    lastTrackPath(""),
    topBarExpiryMs(0) {}

void UIPlayer::begin() {
    if (!sdManager.isMounted() && !sdManager.isExFAT() && !sdManager.isGPT()) sdManager.begin();
    playerConfig.begin();
    wakeTopBar(5000);

    // If already playing or paused, jump straight to Now Playing view without restarting!
    if (audioPlayer.isPlaying() || audioPlayer.isPaused()) {
        openNowPlaying();
        return;
    }

    currentView = VIEW_CATEGORIES;
    inListMode = true;
    currentPage = PAGE_NOW_PLAYING;
    categorySelection = 0;
    categoryScrollOffset = 0;
    playbackMode = (PlaybackMode)playerConfig.getPlaybackMode();
}

void UIPlayer::openNowPlaying() {
    currentView = VIEW_PLAYER;
    inListMode = false;
    currentPage = PAGE_NOW_PLAYING;
    playerTrackStartTime = millis();
    manualLyricIndex = 0;
    lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    albumArtManager.loadForTrack(audioPlayer.getCurrentTrackPath());
    metadataParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    wakeTopBar(5000);
}

PlayerPage UIPlayer::getCurrentPage() const {
    return currentPage;
}

void UIPlayer::setPage(PlayerPage page) {
    currentPage = page;
    playerTrackStartTime = millis();
    wakeTopBar(5000);
}

void UIPlayer::cyclePage() {
    currentPage = (PlayerPage)((currentPage + 1) % 4);
    playerTrackStartTime = millis();
    wakeTopBar(5000);
    if (currentPage == PAGE_LYRICS) {
        manualLyricIndex = 0;
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    } else if (currentPage == PAGE_NOW_PLAYING) {
        albumArtManager.loadForTrack(audioPlayer.getCurrentTrackPath());
    } else if (currentPage == PAGE_METATAGS) {
        metadataParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }
}

void UIPlayer::refreshTrackList() {
    if (!sdManager.isMounted() && !sdManager.isExFAT() && !sdManager.isGPT()) sdManager.begin();
    trackList = sdManager.listMusicFiles();
    if (currentTrackIndex >= (int)trackList.size()) {
        currentTrackIndex = 0;
    }
}

void UIPlayer::refreshCurrentFolder() {
    currentFolderDirs.clear();
    currentFolderFiles.clear();
    sdManager.listFolder(currentFolderPath, currentFolderDirs, currentFolderFiles);
    folderSelection = 0;
    folderScrollOffset = 0;
}

void UIPlayer::enterFolder(const String &folderPath) {
    currentFolderPath = folderPath;
    refreshCurrentFolder();
}

void UIPlayer::playNextTrack() {
    if (trackList.empty()) {
        refreshTrackList();
        if (trackList.empty()) return;
    }
    manualLyricIndex = 0;
    bool started = false;
    size_t attempts = 0;
    size_t total = trackList.size();

    while (!started && attempts < total) {
        attempts++;
        if (playbackMode == PLAY_MODE_REPEAT_ONE) {
            started = audioPlayer.playFile(trackList[currentTrackIndex]);
            if (!started) {
                // If current file fails, advance to avoid getting stuck indefinitely
                currentTrackIndex = (currentTrackIndex + 1) % total;
            }
        } else if (playbackMode == PLAY_MODE_SHUFFLE) {
            if (total > 1) {
                int next = rand() % total;
                if (next == currentTrackIndex) next = (next + 1) % total;
                currentTrackIndex = next;
            }
            started = audioPlayer.playFile(trackList[currentTrackIndex]);
        } else if (playbackMode == PLAY_MODE_ALL) {
            currentTrackIndex = (currentTrackIndex + 1) % total;
            started = audioPlayer.playFile(trackList[currentTrackIndex]);
        } else { // PLAY_MODE_SINGLE
            audioPlayer.stop();
            inListMode = true;
            currentView = VIEW_CATEGORIES;
            ledManager.onPlaybackStop();
            break;
        }
    }

    if (started) {
        ledManager.onPlaybackStart();
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
        albumArtManager.loadForTrack(audioPlayer.getCurrentTrackPath());
        metadataParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }
}

void UIPlayer::playPreviousTrack() {
    if (trackList.empty()) {
        refreshTrackList();
        if (trackList.empty()) return;
    }
    manualLyricIndex = 0;
    bool started = false;
    size_t attempts = 0;
    size_t total = trackList.size();

    while (!started && attempts < total) {
        attempts++;
        currentTrackIndex = (currentTrackIndex - 1 + total) % total;
        started = audioPlayer.playFile(trackList[currentTrackIndex]);
    }

    if (started) {
        ledManager.onPlaybackStart();
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
        albumArtManager.loadForTrack(audioPlayer.getCurrentTrackPath());
        metadataParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }
}

void UIPlayer::setPlaybackMode(PlaybackMode mode) {
    playbackMode = mode;
}

PlaybackMode UIPlayer::getPlaybackMode() const {
    return playbackMode;
}

void UIPlayer::cyclePlaybackMode() {
    playbackMode = (PlaybackMode)((playbackMode + 1) % 4);
    playerConfig.setPlaybackMode((uint8_t)playbackMode);
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
    wakeTopBar(5000);
}

void UIPlayer::wakeTopBar(unsigned long durationMs) {
    topBarExpiryMs = millis() + durationMs;
}

bool UIPlayer::isTopBarVisible() const {
    return millis() < topBarExpiryMs;
}

void UIPlayer::resetTopBar() {
    topBarExpiryMs = 0;
}

bool UIPlayer::update() {
    // Check auto-advance on track completion across all player views
    if (audioPlayer.hasFinished()) {
        audioPlayer.clearFinished();
        playNextTrack();
    }

    ButtonEvent evt = buttonManager.update();

    String curTrack = audioPlayer.getCurrentTrackPath();
    if (curTrack != lastTrackPath) {
        lastTrackPath = curTrack;
        playerTrackStartTime = millis();
    }

    // Global Long-Press CANCEL returns to Main Menu from anywhere
    if (evt == BTN_EVENT_CANCEL_HOLD) {
        return false;
    }

    if (currentView == VIEW_CATEGORIES) {
        // --- 1. Categories Menu View ---
        std::vector<String> catLabels;
        bool hasActiveTrack = (audioPlayer.isPlaying() || audioPlayer.isPaused());
        if (hasActiveTrack) catLabels.push_back("▶ Now Playing");
        catLabels.push_back("All Songs");
        catLabels.push_back("Folders");
        catLabels.push_back("Artists");
        catLabels.push_back("Albums");
        catLabels.push_back("Genres");
        catLabels.push_back("Playlists");
        catLabels.push_back("Recently Played");
        catLabels.push_back(String("Playback: ") + getPlaybackModeString());
        catLabels.push_back("Player Settings");
        catLabels.push_back("Rescan Library");

        int totalCats = (int)catLabels.size();
        if (categorySelection >= totalCats) categorySelection = 0;

        if (evt == BTN_EVENT_UP_PRESS) {
            Display::navigateMenu(categorySelection, categoryScrollOffset, totalCats, -1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            Display::navigateMenu(categorySelection, categoryScrollOffset, totalCats, +1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
            int idx = categorySelection;
            if (hasActiveTrack && idx == 0) {
                // "▶ Now Playing" selected: return straight to active player!
                openNowPlaying();
                return true;
            }
            if (hasActiveTrack) idx--; // adjust offset

            switch (idx) {
                case 0: // All Songs
                    currentCategoryTitle = "ALL SONGS";
                    refreshTrackList();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    for (size_t i = 0; i < trackList.size(); i++) {
                        if (trackList[i] == audioPlayer.getCurrentTrackPath()) {
                            currentTrackIndex = (int)i;
                            break;
                        }
                    }
                    break;
                case 1: // Folders
                    currentView = VIEW_FOLDER_BROWSER;
                    currentFolderPath = "/music";
                    refreshCurrentFolder();
                    break;
                case 2: // Artists
                    currentCategoryTitle = "ARTISTS";
                    trackList = sdManager.listArtists();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 3: // Albums
                    currentCategoryTitle = "ALBUMS";
                    trackList = sdManager.listAlbums();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 4: // Genres
                    currentCategoryTitle = "GENRES";
                    trackList = sdManager.listGenres();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 5: // Playlists
                    currentCategoryTitle = "PLAYLISTS";
                    trackList = sdManager.listPlaylists();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 6: // Recently Played
                    currentCategoryTitle = "RECENT TRACKS";
                    trackList = sdManager.getRecentTracks();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 7: // Playback Mode toggle
                    cyclePlaybackMode();
                    break;
                case 8: // Player Settings
                    currentView = VIEW_PLAYER_SETTINGS;
                    settingsSelection = 0;
                    settingsScrollOffset = 0;
                    break;
                case 9: // Rescan Library
                    display.clear();
                    display.drawTopStatusBar("INDEXING...", battery.getPercentage());
                    display.getU8g2().setFont(u8g2_font_6x10_tr);
                    display.getU8g2().drawStr(10, 32, "Scanning SD Card...");
                    display.sendBuffer();
                    sdManager.rescanLibrary();
                    refreshTrackList();
                    break;
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            return false; // Exit to Main Menu
        }
        static unsigned long lastCatRender = 0;
        unsigned long now = millis();
        unsigned long catInterval = Display::isMenuScrolling() ? 60 : 250;
        if (evt != BTN_EVENT_NONE || (now - lastCatRender >= catInterval)) {
            lastCatRender = now;
            renderCategories();
        }

    } else if (currentView == VIEW_PLAYER_SETTINGS) {
        // --- Customization Settings Menu ---
        const int SETTINGS_COUNT = 9;
        if (evt == BTN_EVENT_UP_PRESS) {
            Display::navigateMenu(settingsSelection, settingsScrollOffset, SETTINGS_COUNT, -1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            Display::navigateMenu(settingsSelection, settingsScrollOffset, SETTINGS_COUNT, +1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
            switch (settingsSelection) {
                case 0: playerConfig.cycleLayout(); break;
                case 1: playerConfig.cycleArtSource(); break;
                case 2: {
                    currentView = VIEW_ART_SETTINGS;
                    artSettingsRow = 0;
                    break;
                }
                case 3: playerConfig.toggleLyricsAutoScroll(); break;
                case 4: playerConfig.cycleLyricsFont(); break;
                case 5: playerConfig.cycleLyricsSize(); break;
                case 6: {
                    currentView = VIEW_EQ_SETTINGS;
                    eqBandSelection = 0;
                    break;
                }
                case 7: {
                    wifiStreamer.toggleWiFi();
                    break;
                }
                case 8: currentView = VIEW_CATEGORIES; break;
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            currentView = VIEW_CATEGORIES;
        }
        static unsigned long lastSetRender = 0;
        unsigned long nowSettings = millis();
        unsigned long setInterval = Display::isMenuScrolling() ? 60 : 250;
        if (evt != BTN_EVENT_NONE || (nowSettings - lastSetRender >= setInterval)) {
            lastSetRender = nowSettings;
            renderPlayerSettings();
        }

    } else if (currentView == VIEW_EQ_SETTINGS) {
        // --- Audiophile 5-Band Equalizer & Crossfeed ---
        if (evt == BTN_EVENT_UP_PRESS || evt == BTN_EVENT_UP_HOLD) {
            if (eqBandSelection < 5) {
                int8_t g = playerConfig.getEqBand(eqBandSelection);
                if (g < 12) {
                    playerConfig.setEqBand(eqBandSelection, g + 1);
                }
            } else {
                playerConfig.cycleCrossfeedMode();
            }
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS || evt == BTN_EVENT_DN_HOLD) {
            if (eqBandSelection < 5) {
                int8_t g = playerConfig.getEqBand(eqBandSelection);
                if (g > -12) {
                    playerConfig.setEqBand(eqBandSelection, g - 1);
                }
            } else {
                playerConfig.cycleCrossfeedMode();
            }
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            eqBandSelection = (eqBandSelection + 1) % 6;
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
        } else if (evt == BTN_EVENT_OK_HOLD) {
            playerConfig.cycleEqPreset();
            ledManager.triggerButtonPulse(CRGB::Magenta, 1, 100);
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            currentView = VIEW_PLAYER_SETTINGS;
        }
        static unsigned long lastEqRender = 0;
        unsigned long nowEq = millis();
        if (evt != BTN_EVENT_NONE || (nowEq - lastEqRender >= 50)) {
            lastEqRender = nowEq;
            renderEqSettings();
        }

    } else if (currentView == VIEW_ART_SETTINGS) {
        // --- Album Art Settings: Dithering + Brightness/Contrast/Threshold ---
        // Row 0: dither algorithm (SEL cycles), Row 1: brightness, Row 2: contrast, Row 3: threshold
        const int ART_ROW_COUNT = 4;
        if (evt == BTN_EVENT_SEL_PRESS) {
            if (artSettingsRow == 0) {
                playerConfig.cycleDitherMode();
            } else {
                artSettingsRow = (artSettingsRow + 1) % ART_ROW_COUNT;
            }
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
        } else if (evt == BTN_EVENT_UP_PRESS || evt == BTN_EVENT_UP_HOLD) {
            switch (artSettingsRow) {
                case 0: playerConfig.cycleDitherMode(); break;
                case 1: { int8_t v = playerConfig.getArtBrightness(); if (v < 100) playerConfig.setArtBrightness(v + 1); break; }
                case 2: { int8_t v = playerConfig.getArtContrast();   if (v < 100) playerConfig.setArtContrast(v + 1);   break; }
                case 3: { uint8_t v = playerConfig.getArtThreshold(); if (v < 255) playerConfig.setArtThreshold(v + 1); break; }
            }
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS || evt == BTN_EVENT_DN_HOLD) {
            switch (artSettingsRow) {
                case 0: playerConfig.cycleDitherMode(); break;
                case 1: { int8_t v = playerConfig.getArtBrightness(); if (v > -100) playerConfig.setArtBrightness(v - 1); break; }
                case 2: { int8_t v = playerConfig.getArtContrast();   if (v > -100) playerConfig.setArtContrast(v - 1);   break; }
                case 3: { uint8_t v = playerConfig.getArtThreshold(); if (v > 0)   playerConfig.setArtThreshold(v - 1);   break; }
            }
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_OK_HOLD) {
            // Cycle to next row on long OK press
            artSettingsRow = (artSettingsRow + 1) % ART_ROW_COUNT;
            ledManager.triggerButtonPulse(CRGB::Magenta, 1, 100);
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            // Reload album art with new settings and return to Settings
            albumArtManager.clear();
            albumArtManager.loadForTrack(audioPlayer.getCurrentTrackPath());
            currentView = VIEW_PLAYER_SETTINGS;
        }
        static unsigned long lastArtRender = 0;
        unsigned long nowArt = millis();
        if (evt != BTN_EVENT_NONE || (nowArt - lastArtRender >= 100)) {
            lastArtRender = nowArt;
            renderArtSettings();
        }

    } else if (currentView == VIEW_TRACK_LIST) {
        // --- Track List View ---
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
                String selected = trackList[currentTrackIndex];

                if (selected.endsWith(".m3u") || selected.endsWith(".m3u8")) {
                    currentCategoryTitle = "PLAYLIST";
                    trackList = sdManager.loadPlaylist(selected);
                    currentTrackIndex = 0;
                    trackScrollOffset = 0;
                    return true;
                }

                if (currentCategoryTitle == "ARTISTS") {
                    std::vector<String> filtered = sdManager.listTracksByArtist(selected);
                    if (!filtered.empty()) {
                        currentCategoryTitle = selected;
                        trackList = filtered;
                        currentTrackIndex = 0;
                        trackScrollOffset = 0;
                        return true;
                    }
                } else if (currentCategoryTitle == "ALBUMS") {
                    std::vector<String> filtered = sdManager.listTracksByAlbum(selected);
                    if (!filtered.empty()) {
                        currentCategoryTitle = selected;
                        trackList = filtered;
                        currentTrackIndex = 0;
                        trackScrollOffset = 0;
                        return true;
                    }
                } else if (currentCategoryTitle == "GENRES") {
                    std::vector<String> filtered = sdManager.listTracksByGenre(selected);
                    if (!filtered.empty()) {
                        currentCategoryTitle = selected;
                        trackList = filtered;
                        currentTrackIndex = 0;
                        trackScrollOffset = 0;
                        return true;
                    }
                }

                // If user selected the track that is ALREADY PLAYING:
                // DO NOT RESTART FROM BEGINNING! Return straight to Now Playing!
                if ((audioPlayer.isPlaying() || audioPlayer.isPaused()) &&
                    audioPlayer.getCurrentTrackPath() == selected) {
                    openNowPlaying();
                    ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
                    return true;
                }

                if (audioPlayer.playFile(selected)) {
                    openNowPlaying();
                    ledManager.onPlaybackStart();
                } else {
                    playNextTrack();
                    openNowPlaying();
                }
            } else {
                refreshTrackList();
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            currentView = VIEW_CATEGORIES;
        }
        static unsigned long lastTrkRender = 0;
        unsigned long nowTrk = millis();
        unsigned long trkInterval = Display::isMenuScrolling() ? 60 : 250;
        if (evt != BTN_EVENT_NONE || (nowTrk - lastTrkRender >= trkInterval)) {
            lastTrkRender = nowTrk;
            renderTrackList();
        }

    } else if (currentView == VIEW_FOLDER_BROWSER) {
        // --- Hierarchical Folder Browser View ---
        if (!sdManager.isMounted()) {
            if (evt == BTN_EVENT_SEL_PRESS) {
                sdManager.remount();
                refreshCurrentFolder();
                ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
            } else if (evt == BTN_EVENT_CANCEL_PRESS) {
                currentView = VIEW_CATEGORIES;
            }
            static unsigned long lastFldRender = 0;
            unsigned long nowFld = millis();
            if (evt != BTN_EVENT_NONE || (nowFld - lastFldRender >= 250)) {
                lastFldRender = nowFld;
                renderFolderBrowser();
            }
            return true;
        }

        bool hasParent = (currentFolderPath != "/music" && currentFolderPath != "/music/");
        int totalItems = (hasParent ? 1 : 0) + currentFolderDirs.size() + currentFolderFiles.size();

        if (evt == BTN_EVENT_UP_PRESS) {
            Display::navigateMenu(folderSelection, folderScrollOffset, totalItems, -1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            Display::navigateMenu(folderSelection, folderScrollOffset, totalItems, +1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
            int idx = folderSelection;
            if (hasParent) {
                if (idx == 0) {
                    int lastSlash = currentFolderPath.lastIndexOf('/');
                    if (lastSlash > 0) {
                        enterFolder(currentFolderPath.substring(0, lastSlash));
                    } else {
                        enterFolder("/music");
                    }
                    return true;
                }
                idx--;
            }

            if (idx < (int)currentFolderDirs.size()) {
                String newPath = currentFolderPath;
                if (!newPath.endsWith("/")) newPath += "/";
                newPath += currentFolderDirs[idx];
                enterFolder(newPath);
            } else {
                int fileIdx = idx - currentFolderDirs.size();
                if (fileIdx >= 0 && fileIdx < (int)currentFolderFiles.size()) {
                    String selected = currentFolderFiles[fileIdx];
                    if ((audioPlayer.isPlaying() || audioPlayer.isPaused()) &&
                        audioPlayer.getCurrentTrackPath() == selected) {
                        openNowPlaying();
                        return true;
                    }
                    trackList = currentFolderFiles;
                    currentTrackIndex = fileIdx;
                    audioPlayer.playFile(selected);
                    openNowPlaying();
                    ledManager.onPlaybackStart();
                }
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            if (hasParent) {
                int lastSlash = currentFolderPath.lastIndexOf('/');
                if (lastSlash > 0) {
                    enterFolder(currentFolderPath.substring(0, lastSlash));
                } else {
                    enterFolder("/music");
                }
            } else {
                currentView = VIEW_CATEGORIES;
            }
        }
        static unsigned long lastFldRender = 0;
        unsigned long nowFld = millis();
        unsigned long fldInterval = Display::isMenuScrolling() ? 60 : 250;
        if (evt != BTN_EVENT_NONE || (nowFld - lastFldRender >= fldInterval)) {
            lastFldRender = nowFld;
            renderFolderBrowser();
        }

    } else {
        // --- Now Playing Multi-Page View (VIEW_PLAYER) ---
        if (evt != BTN_EVENT_NONE) {
            wakeTopBar(5000);
        }
        if (evt == BTN_EVENT_SEL_PRESS) {
            if (currentPage == PAGE_TRACK_INFO) {
                cyclePlaybackMode();
                ledManager.triggerButtonPulse(CRGB::Magenta, 1, 60);
            } else {
                if (audioPlayer.isPlaying()) {
                    audioPlayer.pause();
                    ledManager.onPlaybackPause();
                } else if (audioPlayer.isPaused()) {
                    audioPlayer.resume();
                    ledManager.onPlaybackResume();
                } else if (!trackList.empty()) {
                    audioPlayer.playFile(trackList[currentTrackIndex]);
                    ledManager.onPlaybackStart();
                }
            }
        } else if (evt == BTN_EVENT_OK_HOLD) {
            // Long Press OK: Switch to next page! (Page 1 -> Page 2 -> Page 3 -> Page 1)
            cyclePage();
            ledManager.triggerButtonPulse(CRGB::Magenta, 1, 100);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            if (currentPage == PAGE_LYRICS && lyricsParser.hasLyrics() && !lyricsParser.isSynchronized()) {
                if (manualLyricIndex + 1 < (int)lyricsParser.getLineCount()) {
                    manualLyricIndex++;
                }
            } else {
                playNextTrack();
            }
            ledManager.triggerButtonPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_PRESS) {
            if (currentPage == PAGE_LYRICS && lyricsParser.hasLyrics() && !lyricsParser.isSynchronized()) {
                if (manualLyricIndex > 0) {
                    manualLyricIndex--;
                }
            } else {
                playPreviousTrack();
            }
            ledManager.triggerButtonPulse(CRGB::Cyan, 1, 100);
        } else if (evt == BTN_EVENT_UP_HOLD) {
            audioPlayer.volumeUp(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_DN_HOLD) {
            audioPlayer.volumeDown(5);
            showVolumeOverlay();
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            if (currentPage != PAGE_NOW_PLAYING) {
                currentPage = PAGE_NOW_PLAYING;
            } else {
                currentView = VIEW_CATEGORIES;
                inListMode = true;
            }
        }

        static unsigned long lastPlyRender = 0;
        unsigned long nowPly = millis();
        if (evt != BTN_EVENT_NONE || (nowPly - lastPlyRender >= 33)) {
            lastPlyRender = nowPly;
            renderPlayer();
        }
    }
    return true;
}

void UIPlayer::renderCategories() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    bool hasActiveTrack = (audioPlayer.isPlaying() || audioPlayer.isPaused());
    std::vector<const char*> itemPtrs;
    std::vector<String> items;

    if (hasActiveTrack) items.push_back("▶ Now Playing");
    items.push_back("All Songs");
    items.push_back("Folders");
    items.push_back("Artists");
    items.push_back("Albums");
    items.push_back("Genres");
    items.push_back("Playlists");
    items.push_back("Recently Played");
    items.push_back(String("Playback: ") + getPlaybackModeString());
    items.push_back("Player Settings");
    items.push_back("Rescan Library");

    for (size_t i = 0; i < items.size(); i++) {
        itemPtrs.push_back(items[i].c_str());
    }

    display.drawStandardMenu("Q-TUNES MUSIC", itemPtrs.data(), (int)items.size(), categorySelection, categoryScrollOffset, (const String*)nullptr, batBuf);
    display.sendBuffer();
}

void UIPlayer::renderPlayerSettings() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    const int SETTINGS_COUNT = 9;
    const char* labels[SETTINGS_COUNT] = {
        "Layout",
        "Art Src",
        "Art Style",
        "LRC Sync",
        "LRC Font",
        "LRC Size",
        "Equalizer (DSP)",
        "Wi-Fi",
        "Back"
    };

    String vals[SETTINGS_COUNT];
    vals[0] = String("[") + playerConfig.getLayoutName() + "]";
    vals[1] = String("[") + playerConfig.getArtSourceName() + "]";
    vals[2] = String("[") + playerConfig.getDitherName() + "] >";
    vals[3] = String("[") + playerConfig.getLyricsAutoScrollName() + "]";
    vals[4] = String("[") + playerConfig.getLyricsFontName() + "]";
    vals[5] = String("[") + playerConfig.getLyricsSizeName() + "]";
    vals[6] = String("[") + playerConfig.getEqPresetName() + "]";
    vals[7] = wifiStreamer.isWiFiEnabled() ? "[ON]" : "[OFF]";
    vals[8] = "[EXIT]";

    display.drawStandardMenu("SETTINGS", labels, SETTINGS_COUNT, settingsSelection, settingsScrollOffset, vals, batBuf);
    display.sendBuffer();
}

void UIPlayer::renderEqSettings() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    // Top status line
    u8g2.setFont(u8g2_font_5x7_tr);
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "EQ: %s", playerConfig.getEqPresetName());
    while (headerBuf[0] != '\0' && u8g2.getStrWidth(headerBuf) > 92) {
        headerBuf[strlen(headerBuf) - 1] = '\0';
    }
    u8g2.drawStr(0, 8, headerBuf);

    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "B:%d%%", battery.getPercentage());
    u8g2.drawStr(98, 8, batBuf);
    u8g2.drawHLine(0, 10, 128);

    // 5 Frequency Bars: 60Hz, 250Hz, 1kHz, 4kHz, 12kHz
    // Gain range: -12dB .. +12dB
    // Zero baseline at y = 29
    u8g2.drawHLine(4, 29, 120);

    const char* bandLabels[5] = { "60", "250", "1K", "4K", "12K" };
    const int barX[5] = { 6, 31, 56, 81, 106 };
    const int barW = 16;

    for (int b = 0; b < 5; b++) {
        int8_t gain = playerConfig.getEqBand(b);
        int x = barX[b];

        if (gain > 0) {
            int h = gain;
            u8g2.drawBox(x + 2, 29 - h, barW - 4, h);
        } else if (gain < 0) {
            int h = -gain;
            u8g2.drawBox(x + 2, 30, barW - 4, h);
        } else {
            u8g2.drawHLine(x + 2, 29, barW - 4);
        }

        if (eqBandSelection == b) {
            u8g2.drawFrame(x, 15, barW, 31);
            u8g2.setFont(u8g2_font_4x6_tr);
            char gStr[8];
            snprintf(gStr, sizeof(gStr), "%+d", gain);
            int gw = u8g2.getStrWidth(gStr);
            u8g2.drawStr(x + (barW - gw) / 2, 14, gStr);
        }

        u8g2.setFont(u8g2_font_4x6_tr);
        int textW = u8g2.getStrWidth(bandLabels[b]);
        u8g2.drawStr(x + (barW - textW) / 2, 45, bandLabels[b]);
    }

    // Crossfeed status row
    u8g2.setFont(u8g2_font_5x7_tr);
    char xfeedBuf[32];
    snprintf(xfeedBuf, sizeof(xfeedBuf), "X-Feed: %s", playerConfig.getCrossfeedModeName());

    if (eqBandSelection == 5) {
        u8g2.drawBox(0, 48, 128, 9);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, 55, xfeedBuf);
        u8g2.setDrawColor(1);
    } else {
        u8g2.drawStr(2, 55, xfeedBuf);
    }

    // Footer hint line
    u8g2.setFont(u8g2_font_4x6_tr);
    if (eqBandSelection == 5) {
        u8g2.drawStr(2, 63, "UP/DN:Adjust  OK:Band  CANCEL:Back");
    } else {
        u8g2.drawStr(2, 63, "OK:Band  HOLD:Pre  CANCEL:Back");
    }

    display.sendBuffer();
}

void UIPlayer::renderArtSettings() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    // Header
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 8, "ART SETTINGS");
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "B:%d%%", battery.getPercentage());
    u8g2.drawStr(98, 8, batBuf);
    u8g2.drawHLine(0, 10, 128);

    // Row 0: Dithering algorithm (text row with selection highlight)
    {
        const char *dname = playerConfig.getDitherName();
        char buf[32];
        snprintf(buf, sizeof(buf), "Dither: %s", dname);
        if (artSettingsRow == 0) {
            u8g2.drawBox(0, 11, 128, 9);
            u8g2.setDrawColor(0);
            u8g2.drawStr(2, 18, buf);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(2, 18, buf);
        }
    }

    // Helper: draw a horizontal bar for a value in range [minV..maxV]
    // Bar occupies full row width, value mapped to bar fill
    // y is baseline (text at y, bar just below)
    auto drawBar = [&](int row, const char *label, int value, int minV, int maxV, int yTop) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s:%+d", label, value);
        bool selected = (artSettingsRow == row);

        // label + value text
        u8g2.setFont(u8g2_font_5x7_tr);
        if (selected) {
            u8g2.drawBox(0, yTop, 128, 7);
            u8g2.setDrawColor(0);
            u8g2.drawStr(2, yTop + 6, buf);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(2, yTop + 6, buf);
        }

        // Bar below text
        int barY = yTop + 8;
        int barW = 120;
        int barX = 4;
        u8g2.drawFrame(barX, barY, barW, 3);
        int range = maxV - minV;
        int filled = (range > 0) ? ((value - minV) * (barW - 2) / range) : 0;
        if (filled > 0) u8g2.drawBox(barX + 1, barY + 1, filled, 1);
    };

    // Row 1: Brightness (-100..+100)
    drawBar(1, "Bri", (int)playerConfig.getArtBrightness(), -100, 100, 21);

    // Row 2: Contrast (-100..+100)
    drawBar(2, "Con", (int)playerConfig.getArtContrast(), -100, 100, 32);

    // Row 3: Threshold (0..255) — shown as 0..255
    {
        int thr = (int)playerConfig.getArtThreshold();
        bool selected = (artSettingsRow == 3);
        char buf[32];
        snprintf(buf, sizeof(buf), "Thr:%d", thr);
        u8g2.setFont(u8g2_font_5x7_tr);
        if (selected) {
            u8g2.drawBox(0, 43, 128, 7);
            u8g2.setDrawColor(0);
            u8g2.drawStr(2, 49, buf);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(2, 49, buf);
        }
        // bar
        int barY = 51;
        int barX = 4;
        int barW = 120;
        u8g2.drawFrame(barX, barY, barW, 3);
        int filled = thr * (barW - 2) / 255;
        if (filled > 0) u8g2.drawBox(barX + 1, barY + 1, filled, 1);
    }

    // Footer hint
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 63, "SEL/UP/DN:Adj  HOLD:Row  CANCEL:Back");

    display.sendBuffer();
}

void UIPlayer::renderTrackList() {
    display.clear();
    if (!sdManager.isMounted()) {
        display.drawTopStatusBar("Q-TUNES MUSIC", battery.getPercentage());
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        if (sdManager.isGPT()) {
            u8g2.drawStr(10, 24, "SD: GPT Partition");
            u8g2.drawStr(10, 36, "Convert to MBR");
            u8g2.drawStr(10, 48, "in PartitionWizard");
            u8g2.drawStr(10, 60, "CANCEL: Main Menu");
        } else if (sdManager.isExFAT()) {
            u8g2.drawStr(10, 24, "SD: 64GB+ (exFAT)");
            u8g2.drawStr(10, 36, "Format as FAT32");
            u8g2.drawStr(10, 48, "for Q-Tune playback");
            u8g2.drawStr(10, 60, "CANCEL: Main Menu");
        } else {
            u8g2.drawStr(10, 26, "SD Not Mounted");
            u8g2.drawStr(10, 40, "SEL: Mount SD Card");
            u8g2.drawStr(10, 54, "CANCEL: Main Menu");
        }
    } else if (trackList.empty()) {
        display.drawTopStatusBar(currentCategoryTitle.c_str(), battery.getPercentage());
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 26, "No Tracks Found");
        u8g2.drawStr(10, 40, "SEL: Refresh");
        u8g2.drawStr(10, 54, "CANCEL: Categories");
    } else {
        std::vector<const char*> itemPtrs(trackList.size());
        std::vector<String> displayNames(trackList.size());
        String currentPlaying = audioPlayer.getCurrentTrackPath();

        for (size_t i = 0; i < trackList.size(); i++) {
            String name = trackList[i];
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);

            if (trackList[i] == currentPlaying && (audioPlayer.isPlaying() || audioPlayer.isPaused())) {
                name = "▶ " + name;
            }

            displayNames[i] = name;
            itemPtrs[i] = displayNames[i].c_str();
        }
        char headerBuf[24];
        snprintf(headerBuf, sizeof(headerBuf), "%d/%d", currentTrackIndex + 1, (int)trackList.size());
        display.drawStandardMenu(currentCategoryTitle.c_str(), itemPtrs.data(), (int)trackList.size(), currentTrackIndex, trackScrollOffset, (const String*)nullptr, headerBuf);
    }
    display.sendBuffer();
}

void UIPlayer::renderFolderBrowser() {
    display.clear();
    if (!sdManager.isMounted()) {
        display.drawTopStatusBar("FOLDER BROWSER", battery.getPercentage());
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        if (sdManager.isGPT()) {
            u8g2.drawStr(10, 24, "SD: GPT Partition");
            u8g2.drawStr(10, 36, "Convert to MBR");
            u8g2.drawStr(10, 48, "in PartitionWizard");
            u8g2.drawStr(10, 60, "CANCEL: Categories");
        } else if (sdManager.isExFAT()) {
            u8g2.drawStr(10, 24, "SD: 64GB+ (exFAT)");
            u8g2.drawStr(10, 36, "Format as FAT32");
            u8g2.drawStr(10, 48, "for Q-Tune playback");
            u8g2.drawStr(10, 60, "CANCEL: Categories");
        } else {
            u8g2.drawStr(10, 26, "SD Not Mounted");
            u8g2.drawStr(10, 40, "SEL: Mount SD Card");
            u8g2.drawStr(10, 54, "CANCEL: Categories");
        }
        display.sendBuffer();
        return;
    }

    bool hasParent = (currentFolderPath != "/music" && currentFolderPath != "/music/");
    int totalItems = (hasParent ? 1 : 0) + currentFolderDirs.size() + currentFolderFiles.size();

    std::vector<const char*> itemPtrs;
    std::vector<String> displayNames;
    String currentPlaying = audioPlayer.getCurrentTrackPath();

    if (hasParent) {
        displayNames.push_back(".. [Back]");
    }
    for (size_t i = 0; i < currentFolderDirs.size(); i++) {
        String name = "[" + currentFolderDirs[i] + "]";
        displayNames.push_back(name);
    }
    for (size_t i = 0; i < currentFolderFiles.size(); i++) {
        String fullPath = currentFolderFiles[i];
        String name = fullPath;
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (fullPath == currentPlaying && (audioPlayer.isPlaying() || audioPlayer.isPaused())) {
            name = "▶ " + name;
        }
        displayNames.push_back(name);
    }

    for (size_t i = 0; i < displayNames.size(); i++) {
        itemPtrs.push_back(displayNames[i].c_str());
    }

    char headerBuf[24];
    snprintf(headerBuf, sizeof(headerBuf), "%d/%d", folderSelection + 1, totalItems);
    display.drawStandardMenu("FOLDERS", itemPtrs.data(), totalItems, folderSelection, folderScrollOffset, (const String*)nullptr, headerBuf);
    display.sendBuffer();
}

void UIPlayer::renderPlayer() {
    switch (currentPage) {
        case PAGE_METATAGS:
            renderPlayerPage2();
            break;
        case PAGE_LYRICS:
            renderPlayerPage3();
            break;
        case PAGE_TRACK_INFO:
            renderPlayerPage4();
            break;
        case PAGE_NOW_PLAYING:
        default:
            renderPlayerPage1();
            break;
    }
}

// ============================================================================
// Page 1: Now Playing Dispatcher (Auto / Split Art / Cover Hero / Spectrum HUD)
// ============================================================================
void UIPlayer::renderPlayerPage1() {
    PlayerLayout layout = playerConfig.getLayout();
    if (layout == LAYOUT_AUTO) {
        if (albumArtManager.hasArt()) {
            renderPlayerSplitArt();
        } else {
            // Default Spectrum HUD when no art is available
            display.clear();
            U8G2 &u8g2 = display.getU8g2();
            u8g2.setFont(u8g2_font_6x10_tr);
            bool showBar = isTopBarVisible();

            if (showBar) {
                char headerBuf[32];
                snprintf(headerBuf, sizeof(headerBuf), "B:%d%% [%s] [1/4]",
                         battery.getPercentage(), audioPlayer.getOutputModeShortName());
                u8g2.drawStr(0, 10, headerBuf);
                u8g2.drawHLine(0, 12, 128);

                String displayName = audioPlayer.getCurrentTrackName();
                if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
                    displayName = trackList[currentTrackIndex];
                    int slash = displayName.lastIndexOf('/');
                    if (slash >= 0) displayName = displayName.substring(slash + 1);
                }
                Display::drawScrollingText(u8g2, displayName.c_str(), 0, 23, 126, playerTrackStartTime, true);

                String stateStr = "■ STOP";
                if (audioPlayer.isPlaying()) {
                    stateStr = "▶ " + String(audioPlayer.getFormatName());
                } else if (audioPlayer.isPaused()) {
                    stateStr = "❚❚ PAUS";
                }
                u8g2.drawStr(0, 35, stateStr.c_str());

                uint32_t posSec = audioPlayer.getPositionMs() / 1000;
                uint32_t durSec = audioPlayer.getDurationMs() / 1000;
                char timeBuf[32];
                snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u/%02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
                u8g2.drawStr(0, 47, timeBuf);

                spectrumAnalyzer.sampleAudioStream();
                spectrumAnalyzer.renderMiniBars(u8g2, 92, 28, 34, 18);

                u8g2.drawFrame(0, 53, 128, 7);
                if (durSec > 0) {
                    int progressWidth = (posSec * 126) / durSec;
                    if (progressWidth > 126) progressWidth = 126;
                    u8g2.drawBox(1, 54, progressWidth, 5);
                }
            } else {
                // Full-screen Spectrum HUD: header auto-hidden after 5s
                String displayName = audioPlayer.getCurrentTrackName();
                if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
                    displayName = trackList[currentTrackIndex];
                    int slash = displayName.lastIndexOf('/');
                    if (slash >= 0) displayName = displayName.substring(slash + 1);
                }
                Display::drawScrollingText(u8g2, displayName.c_str(), 0, 11, 126, playerTrackStartTime, true);

                uint32_t posSec = audioPlayer.getPositionMs() / 1000;
                uint32_t durSec = audioPlayer.getDurationMs() / 1000;
                char timeBuf[32];
                snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u / %02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
                u8g2.setFont(u8g2_font_5x7_tr);
                u8g2.drawStr(0, 22, timeBuf);
                u8g2.setFont(u8g2_font_6x10_tr);

                spectrumAnalyzer.sampleAudioStream();
                spectrumAnalyzer.renderMiniBars(u8g2, 88, 14, 38, 38);

                u8g2.drawFrame(0, 55, 128, 7);
                if (durSec > 0) {
                    int progressWidth = (posSec * 126) / durSec;
                    if (progressWidth > 126) progressWidth = 126;
                    u8g2.drawBox(1, 56, progressWidth, 5);
                }
            }

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
    } else if (layout == LAYOUT_SPLIT_ART) {
        renderPlayerSplitArt();
    } else if (layout == LAYOUT_COVER_HERO) {
        renderPlayerCoverHero();
    } else { // LAYOUT_SPECTRUM_HUD
        display.clear();
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        bool showBar = isTopBarVisible();

        if (showBar) {
            char headerBuf[32];
            snprintf(headerBuf, sizeof(headerBuf), "B:%d%% [%s] [1/4]",
                     battery.getPercentage(), audioPlayer.getOutputModeShortName());
            u8g2.drawStr(0, 10, headerBuf);
            u8g2.drawHLine(0, 12, 128);

            String displayName = audioPlayer.getCurrentTrackName();
            if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
                displayName = trackList[currentTrackIndex];
                int slash = displayName.lastIndexOf('/');
                if (slash >= 0) displayName = displayName.substring(slash + 1);
            }
            Display::drawScrollingText(u8g2, displayName.c_str(), 0, 23, 126, playerTrackStartTime, true);

            String stateStr = "■ STOP";
            if (audioPlayer.isPlaying()) {
                stateStr = "▶ " + String(audioPlayer.getFormatName());
            } else if (audioPlayer.isPaused()) {
                stateStr = "❚❚ PAUS";
            }
            u8g2.drawStr(0, 35, stateStr.c_str());

            uint32_t posSec = audioPlayer.getPositionMs() / 1000;
            uint32_t durSec = audioPlayer.getDurationMs() / 1000;
            char timeBuf[32];
            snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u/%02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
            u8g2.drawStr(0, 47, timeBuf);

            spectrumAnalyzer.sampleAudioStream();
            spectrumAnalyzer.renderMiniBars(u8g2, 92, 28, 34, 18);

            u8g2.drawFrame(0, 53, 128, 7);
            if (durSec > 0) {
                int progressWidth = (posSec * 126) / durSec;
                if (progressWidth > 126) progressWidth = 126;
                u8g2.drawBox(1, 54, progressWidth, 5);
            }
        } else {
            // Full-screen Spectrum HUD: header auto-hidden after 5s
            String displayName = audioPlayer.getCurrentTrackName();
            if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
                displayName = trackList[currentTrackIndex];
                int slash = displayName.lastIndexOf('/');
                if (slash >= 0) displayName = displayName.substring(slash + 1);
            }
            Display::drawScrollingText(u8g2, displayName.c_str(), 0, 11, 126, playerTrackStartTime, true);

            uint32_t posSec = audioPlayer.getPositionMs() / 1000;
            uint32_t durSec = audioPlayer.getDurationMs() / 1000;
            char timeBuf[32];
            snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u / %02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(0, 22, timeBuf);
            u8g2.setFont(u8g2_font_6x10_tr);

            spectrumAnalyzer.sampleAudioStream();
            spectrumAnalyzer.renderMiniBars(u8g2, 88, 14, 38, 38);

            u8g2.drawFrame(0, 55, 128, 7);
            if (durSec > 0) {
                int progressWidth = (posSec * 126) / durSec;
                if (progressWidth > 126) progressWidth = 126;
                u8g2.drawBox(1, 56, progressWidth, 5);
            }
        }

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
}

// ============================================================================
// Layout Style: Split Album Art (56x56 Dithered Cover + Side Info)
// ============================================================================
void UIPlayer::renderPlayerSplitArt() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    bool showBar = isTopBarVisible();

    if (showBar) {
        // Top status header
        u8g2.setFont(u8g2_font_5x7_tr);
        char headerBuf[32];
        snprintf(headerBuf, sizeof(headerBuf), "B:%d%% [%s] [1/4]",
                 battery.getPercentage(), audioPlayer.getOutputModeShortName());
        u8g2.drawStr(0, 8, headerBuf);
        u8g2.drawHLine(0, 9, 128);

        // Left: Draw 56x56 Album Art (or Default Icon)
        albumArtManager.draw(u8g2, 1, 10);

        // Right: Song Info
        u8g2.setFont(u8g2_font_5x7_tr);
        String displayName = audioPlayer.getCurrentTrackName();
        if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
            displayName = trackList[currentTrackIndex];
            int slash = displayName.lastIndexOf('/');
            if (slash >= 0) displayName = displayName.substring(slash + 1);
        }
        Display::drawScrollingText(u8g2, displayName.c_str(), 60, 18, 66, playerTrackStartTime, true);

        // Playback state
        String stateStr = audioPlayer.isPlaying() ? ("▶ " + String(audioPlayer.getFormatName())) : "❚❚ PAUS";
        u8g2.drawStr(60, 28, stateStr.c_str());

        // Time elapsed / total
        uint32_t posSec = audioPlayer.getPositionMs() / 1000;
        uint32_t durSec = audioPlayer.getDurationMs() / 1000;
        char timeBuf[20];
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u/%02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
        u8g2.drawStr(60, 39, timeBuf);

        // Output Mode / Vol
        char outBuf[16];
        snprintf(outBuf, sizeof(outBuf), "VOL:%d%%", audioPlayer.getVolume());
        u8g2.drawStr(60, 50, outBuf);

        // Mini progress bar
        u8g2.drawFrame(60, 56, 66, 6);
        if (durSec > 0) {
            int progressWidth = (posSec * 64) / durSec;
            if (progressWidth > 64) progressWidth = 64;
            u8g2.drawBox(61, 57, progressWidth, 4);
        }
    } else {
        // Full-screen Split Art (top header auto-hidden)
        // 56x56 Album Art centered vertically on 64px display at Y=4
        albumArtManager.draw(u8g2, 1, 4);

        u8g2.setFont(u8g2_font_5x7_tr);
        String displayName = audioPlayer.getCurrentTrackName();
        if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
            displayName = trackList[currentTrackIndex];
            int slash = displayName.lastIndexOf('/');
            if (slash >= 0) displayName = displayName.substring(slash + 1);
        }
        Display::drawScrollingText(u8g2, displayName.c_str(), 60, 12, 66, playerTrackStartTime, true);

        String stateStr = audioPlayer.isPlaying() ? ("▶ " + String(audioPlayer.getFormatName())) : "❚❚ PAUS";
        u8g2.drawStr(60, 23, stateStr.c_str());

        uint32_t posSec = audioPlayer.getPositionMs() / 1000;
        uint32_t durSec = audioPlayer.getDurationMs() / 1000;
        char timeBuf[20];
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u/%02u:%02u", posSec / 60, posSec % 60, durSec / 60, durSec % 60);
        u8g2.drawStr(60, 35, timeBuf);

        char outBuf[16];
        snprintf(outBuf, sizeof(outBuf), "VOL:%d%%", audioPlayer.getVolume());
        u8g2.drawStr(60, 47, outBuf);

        u8g2.drawFrame(60, 54, 66, 6);
        if (durSec > 0) {
            int progressWidth = (posSec * 64) / durSec;
            if (progressWidth > 64) progressWidth = 64;
            u8g2.drawBox(61, 55, progressWidth, 4);
        }
    }

    display.sendBuffer();
}

// ============================================================================
// Layout Style: Centered 64x64 Hero Cover Art
// ============================================================================
void UIPlayer::renderPlayerCoverHero() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    bool showBar = isTopBarVisible();

    // Centered 64x64 Album Art at X=32, Y=0
    albumArtManager.draw(u8g2, 32, 0);

    // Floating bottom HUD (visible for 5s after any button press, auto-hides for pure full screen)
    if (showBar) {
        u8g2.setFont(u8g2_font_5x7_tr);
        String displayName = audioPlayer.getCurrentTrackName();
        if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
            displayName = trackList[currentTrackIndex];
            int slash = displayName.lastIndexOf('/');
            if (slash >= 0) displayName = displayName.substring(slash + 1);
        }
        Display::drawScrollingText(u8g2, displayName.c_str(), 0, 56, 96, playerTrackStartTime, true);

        uint32_t posSec = audioPlayer.getPositionMs() / 1000;
        char timeBuf[12];
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", posSec / 60, posSec % 60);
        u8g2.drawStr(100, 56, timeBuf);

        uint32_t durSec = audioPlayer.getDurationMs() / 1000;
        u8g2.drawFrame(0, 59, 128, 4);
        if (durSec > 0) {
            int pw = (posSec * 126) / durSec;
            if (pw > 126) pw = 126;
            u8g2.drawBox(1, 60, pw, 2);
        }
    }

    display.sendBuffer();
}

// ============================================================================
// Page 2: Track & Audio Technical Details
// ============================================================================
// ============================================================================
// Page 2: Metadata Tags (ID3 / Vorbis / M4A / RIFF INFO)
// ============================================================================
void UIPlayer::renderPlayerPage2() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    if (metadataParser.getLoadedAudioPath() != audioPlayer.getCurrentTrackPath()) {
        metadataParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }

    bool showBar = isTopBarVisible();
    const TrackMetadata &meta = metadataParser.getMetadata();

    if (showBar) {
        u8g2.setFont(u8g2_font_5x7_tr);
        char headerBuf[32];
        snprintf(headerBuf, sizeof(headerBuf), "TAGS [2/4] B:%d%%", battery.getPercentage());
        u8g2.drawStr(0, 8, headerBuf);
        u8g2.drawHLine(0, 10, 128);

        if (meta.hasTags) {
            String tStr = meta.title.length() > 0 ? meta.title : audioPlayer.getCurrentTrackName();
            char lineBuf[64];
            snprintf(lineBuf, sizeof(lineBuf), "T: %s", tStr.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 19, 124, playerTrackStartTime, true);

            String aStr = meta.artist.length() > 0 ? meta.artist : "Unknown Artist";
            snprintf(lineBuf, sizeof(lineBuf), "A: %s", aStr.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 28, 124, playerTrackStartTime, true);

            String alStr = meta.album.length() > 0 ? meta.album : "Unknown Album";
            snprintf(lineBuf, sizeof(lineBuf), "Alb: %s", alStr.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 37, 124, playerTrackStartTime, true);

            String yr = meta.year.length() > 0 ? meta.year : "----";
            String gn = meta.genre.length() > 0 ? meta.genre : "----";
            snprintf(lineBuf, sizeof(lineBuf), "Yr: %s  Gen: %s", yr.c_str(), gn.c_str());
            u8g2.drawStr(2, 46, lineBuf);

            String trk = meta.trackNumber.length() > 0 ? meta.trackNumber : "--";
            snprintf(lineBuf, sizeof(lineBuf), "Trk: %s  Fmt: %s", trk.c_str(), audioPlayer.getFormatName());
            u8g2.drawStr(2, 55, lineBuf);

            snprintf(lineBuf, sizeof(lineBuf), "Rate: %ukbps %uHz", audioPlayer.getBitrateKbps(), audioPlayer.getSampleRate());
            u8g2.drawStr(2, 63, lineBuf);
        } else {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(2, 24, "No Embedded Tags");
            u8g2.setFont(u8g2_font_5x7_tr);
            String fName = audioPlayer.getCurrentTrackName();
            char lineBuf[64];
            snprintf(lineBuf, sizeof(lineBuf), "File: %s", fName.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 36, 124, playerTrackStartTime, true);
            snprintf(lineBuf, sizeof(lineBuf), "Format: %s (%d-bit)", audioPlayer.getFormatName(), audioPlayer.getBitsPerSample());
            u8g2.drawStr(2, 47, lineBuf);
            snprintf(lineBuf, sizeof(lineBuf), "Bitrate: %u kbps", audioPlayer.getBitrateKbps());
            u8g2.drawStr(2, 58, lineBuf);
        }
    } else {
        String tStr = (meta.hasTags && meta.title.length() > 0) ? meta.title : audioPlayer.getCurrentTrackName();
        u8g2.setFont(u8g2_font_6x10_tr);
        Display::drawScrollingText(u8g2, tStr.c_str(), 2, 10, 124, playerTrackStartTime, true);
        u8g2.drawHLine(0, 13, 128);

        u8g2.setFont(u8g2_font_5x7_tr);
        char lineBuf[64];
        if (meta.hasTags) {
            String aStr = meta.artist.length() > 0 ? meta.artist : "Unknown Artist";
            snprintf(lineBuf, sizeof(lineBuf), "Artist: %s", aStr.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 23, 124, playerTrackStartTime, true);

            String alStr = meta.album.length() > 0 ? meta.album : "Unknown Album";
            snprintf(lineBuf, sizeof(lineBuf), "Album:  %s", alStr.c_str());
            Display::drawScrollingText(u8g2, lineBuf, 2, 33, 124, playerTrackStartTime, true);

            String yr = meta.year.length() > 0 ? meta.year : "----";
            String gn = meta.genre.length() > 0 ? meta.genre : "----";
            snprintf(lineBuf, sizeof(lineBuf), "Year: %s   Genre: %s", yr.c_str(), gn.c_str());
            u8g2.drawStr(2, 43, lineBuf);

            String trk = meta.trackNumber.length() > 0 ? meta.trackNumber : "--";
            snprintf(lineBuf, sizeof(lineBuf), "Track: %s  Format: %s", trk.c_str(), audioPlayer.getFormatName());
            u8g2.drawStr(2, 53, lineBuf);

            if (meta.comment.length() > 0) {
                snprintf(lineBuf, sizeof(lineBuf), "Cmt: %s", meta.comment.c_str());
                Display::drawScrollingText(u8g2, lineBuf, 2, 63, 124, playerTrackStartTime, true);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%u kbps | %u.%ukHz", audioPlayer.getBitrateKbps(), audioPlayer.getSampleRate() / 1000, (audioPlayer.getSampleRate() % 1000) / 100);
                u8g2.drawStr(2, 63, lineBuf);
            }
        } else {
            u8g2.drawStr(2, 26, "Embedded ID3 / Tags:");
            u8g2.drawStr(2, 37, "[Not Found in File]");
            snprintf(lineBuf, sizeof(lineBuf), "Format: %s", audioPlayer.getFormatName());
            u8g2.drawStr(2, 49, lineBuf);
            snprintf(lineBuf, sizeof(lineBuf), "Size: %u KB", audioPlayer.getTotalBytes() / 1024);
            u8g2.drawStr(2, 60, lineBuf);
        }
    }

    display.sendBuffer();
}

// ============================================================================
// Page 4: Track & Audio Technical Details (Song Info)
// ============================================================================
void UIPlayer::renderPlayerPage4() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    bool showBar = isTopBarVisible();

    if (showBar) {
        u8g2.setFont(u8g2_font_5x7_tr);
        char headerBuf[32];
        snprintf(headerBuf, sizeof(headerBuf), "INFO [4/4] B:%d%%", battery.getPercentage());
        u8g2.drawStr(0, 8, headerBuf);
        u8g2.drawHLine(0, 10, 128);

        char buf[40];
        snprintf(buf, sizeof(buf), "Codec: %s (%d-bit)", audioPlayer.getFormatName(), audioPlayer.getBitsPerSample());
        u8g2.drawStr(2, 19, buf);

        uint32_t sr = audioPlayer.getSampleRate();
        const char* chStr = (audioPlayer.getChannels() == 1) ? "Mono" : "Stereo";
        snprintf(buf, sizeof(buf), "Rate: %u.%ukHz %s", sr / 1000, (sr % 1000) / 100, chStr);
        u8g2.drawStr(2, 28, buf);

        uint32_t kbps = audioPlayer.getBitrateKbps();
        snprintf(buf, sizeof(buf), "Bitrate: %u kbps", kbps);
        u8g2.drawStr(2, 37, buf);

        uint32_t sz = audioPlayer.getTotalBytes();
        snprintf(buf, sizeof(buf), "Size: %u.%01u MB", sz / 1048576, (sz % 1048576) / 104857);
        u8g2.drawStr(2, 46, buf);

        snprintf(buf, sizeof(buf), "Out: %s", audioPlayer.getOutputModeShortName());
        u8g2.drawStr(2, 55, buf);

        snprintf(buf, sizeof(buf), "Mode: %s (OK: Cycle)", getPlaybackModeString());
        u8g2.drawStr(2, 63, buf);
    } else {
        // Full-screen Track Info card (header auto-hidden after 5s)
        String displayName = audioPlayer.getCurrentTrackName();
        if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
            displayName = trackList[currentTrackIndex];
            int slash = displayName.lastIndexOf('/');
            if (slash >= 0) displayName = displayName.substring(slash + 1);
        }
        u8g2.setFont(u8g2_font_6x10_tr);
        Display::drawScrollingText(u8g2, displayName.c_str(), 2, 10, 124, playerTrackStartTime, true);
        u8g2.drawHLine(0, 13, 128);

        u8g2.setFont(u8g2_font_5x7_tr);
        char buf[40];
        snprintf(buf, sizeof(buf), "Codec: %s (%d-bit)", audioPlayer.getFormatName(), audioPlayer.getBitsPerSample());
        u8g2.drawStr(2, 23, buf);

        uint32_t sr = audioPlayer.getSampleRate();
        const char* chStr = (audioPlayer.getChannels() == 1) ? "Mono" : "Stereo";
        snprintf(buf, sizeof(buf), "Rate:  %u.%ukHz %s", sr / 1000, (sr % 1000) / 100, chStr);
        u8g2.drawStr(2, 33, buf);

        uint32_t kbps = audioPlayer.getBitrateKbps();
        uint32_t sz = audioPlayer.getTotalBytes();
        snprintf(buf, sizeof(buf), "Audio: %u kbps (%u.%01uMB)", kbps, sz / 1048576, (sz % 1048576) / 104857);
        u8g2.drawStr(2, 43, buf);

        snprintf(buf, sizeof(buf), "Out:   %s", audioPlayer.getOutputModeShortName());
        u8g2.drawStr(2, 53, buf);

        snprintf(buf, sizeof(buf), "Mode:  %s", getPlaybackModeString());
        u8g2.drawStr(2, 63, buf);
    }

    display.sendBuffer();
}

// ── Devanagari (Hindi) UTF-8 Detection & Rendering ───────────────────────────
#ifdef U8G2_FONT_SECTION
extern const uint8_t u8g2_font_unifont_t_devanagari[] U8G2_FONT_SECTION("u8g2_font_unifont_t_devanagari");
#else
extern const uint8_t u8g2_font_unifont_t_devanagari[];
#endif

static inline bool isDevanagariText(const String &str) {
    const uint8_t *p = (const uint8_t*)str.c_str();
    while (*p) {
        if (*p == 0xE0 && *(p + 1) != 0 && *(p + 2) != 0) {
            uint8_t b2 = *(p + 1);
            if (b2 == 0xA4 || b2 == 0xA5) return true;
            p += 3;
        } else {
            p++;
        }
    }
    return false;
}

static u8g2_uint_t drawDevanagari(U8G2 &u8g2, int16_t x, int16_t y, const char *str) {
    uint16_t e;
    u8g2_uint_t delta;
    int16_t startX = x;
    u8g2.getU8g2()->u8x8.next_cb = u8x8_utf8_next;
    u8x8_utf8_init(u8g2.getU8x8());
    u8g2.setFontMode(1);

    for (;;) {
        e = u8g2.getU8g2()->u8x8.next_cb(u8g2.getU8x8(), (uint8_t)*str);
        if (e == 0x0ffff)
            break;
        str++;
        if (e != 0x0fffe) {
            switch (e) {
                case 0x0901: x -= 14; y -= 2; break; // ँ
                case 0x0902: x -= 14; y -= 2; break; // ं
                case 0x0903: x -= 14; break;         // ः
                case 0x093e: x -= 12; break;         // ा
                case 0x093f: x -= 16; break;         // ि
                case 0x0940: x -= 12; break;         // ी
                case 0x0941: x -= 15; y += 2; break; // ु
                case 0x0942: x -= 12; y += 2; break; // ू
                case 0x0943: x -= 16; y += 2; break; // ृ
                case 0x0944: x -= 16; y += 2; break; // ॄ
                case 0x0945: x -= 14; y -= 2; break; // ॅ
                case 0x0946: x -= 14; break;         // ॆ
                case 0x0947: x -= 14; break;         // े
                case 0x0948: x -= 14; break;         // ै
                case 0x0949: x -= 14; break;         // ॉ
                case 0x094b: x -= 14; break;         // ो
                case 0x094c: x -= 14; break;         // ौ
                case 0x094d: x -= 10; break;         // ्
            }
            delta = u8g2.drawGlyph(x, y, e);
            switch (e) {
                case 0x0901: x -= 6; y += 2; break;
                case 0x0902: x -= 6; y += 2; break;
                case 0x0941: x -= 3; y -= 2; break;
                case 0x0942: x -= 6; y -= 2; break;
                case 0x0943: x -= 4; y -= 2; break;
                case 0x0944: x -= 4; y -= 2; break;
                case 0x0945: x -= 6; y += 2; break;
                case 0x0946: x -= 2; break;
                case 0x0947: x -= 2; break;
                case 0x0948: x -= 2; break;
                case 0x0949: x -= 2; break;
                case 0x094d: x -= 8; break;
            }
            x += delta;
        }
    }
    return (x > startX) ? (u8g2_uint_t)(x - startX) : 0;
}

static u8g2_uint_t getDevanagariWidth(U8G2 &u8g2, const char *str) {
    uint16_t e;
    int16_t x = 0;
    u8g2.getU8g2()->u8x8.next_cb = u8x8_utf8_next;
    u8x8_utf8_init(u8g2.getU8x8());

    for (;;) {
        e = u8g2.getU8g2()->u8x8.next_cb(u8g2.getU8x8(), (uint8_t)*str);
        if (e == 0x0ffff)
            break;
        str++;
        if (e != 0x0fffe) {
            int16_t pre = 0, post = 0;
            switch (e) {
                case 0x0901: pre = -14; post = -6; break;
                case 0x0902: pre = -14; post = -6; break;
                case 0x0903: pre = -14; break;
                case 0x093e: pre = -12; break;
                case 0x093f: pre = -16; break;
                case 0x0940: pre = -12; break;
                case 0x0941: pre = -15; post = -3; break;
                case 0x0942: pre = -12; post = -6; break;
                case 0x0943: pre = -16; post = -4; break;
                case 0x0944: pre = -16; post = -4; break;
                case 0x0945: pre = -14; post = -6; break;
                case 0x0946: pre = -14; post = -2; break;
                case 0x0947: pre = -14; post = -2; break;
                case 0x0948: pre = -14; post = -2; break;
                case 0x0949: pre = -14; post = -2; break;
                case 0x094b: pre = -14; break;
                case 0x094c: pre = -14; break;
                case 0x094d: pre = -10; post = -8; break;
            }
            int8_t delta = u8g2_GetGlyphWidth(u8g2.getU8g2(), e);
            x += pre + post + delta;
        }
    }
    return (x > 0) ? (u8g2_uint_t)x : 0;
}

static u8g2_uint_t getLyricTextWidth(U8G2 &u8g2, const String &str, bool isDevanagari) {
    if (str.isEmpty()) return 0;
    if (isDevanagari) {
        u8g2.setFont(u8g2_font_unifont_t_devanagari);
        return getDevanagariWidth(u8g2, str.c_str());
    } else {
        return u8g2.getUTF8Width(str.c_str());
    }
}

static u8g2_uint_t drawLyricText(U8G2 &u8g2, int16_t x, int16_t y, const String &str, bool isDevanagari) {
    if (str.isEmpty()) return 0;
    if (isDevanagari) {
        u8g2.setFont(u8g2_font_unifont_t_devanagari);
        u8g2.setFontMode(1);
        return drawDevanagari(u8g2, x, y, str.c_str());
    } else {
        return u8g2.drawUTF8(x, y, str.c_str());
    }
}

// ============================================================================
// Page 3: Synchronized LRC Lyrics Viewer
// ============================================================================
void UIPlayer::renderPlayerPage3() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    if (lyricsParser.getLoadedAudioPath() != audioPlayer.getCurrentTrackPath()) {
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }

    bool showBar = isTopBarVisible();

    // ── Header (visible for 5s after button press, auto-hides for full screen) ─
    if (showBar) {
        u8g2.setFont(u8g2_font_5x7_tr);
        char headerBuf[32];
        const char* modeTag = lyricsParser.isSynchronized() ? "[SYNC]"
                            : (lyricsParser.hasLyrics()     ? "[MANUAL]" : "[NONE]");
        snprintf(headerBuf, sizeof(headerBuf), "LRC %s [3/4] B:%d%%", modeTag, battery.getPercentage());
        u8g2.drawStr(0, 8, headerBuf);
        u8g2.drawHLine(0, 10, 128);
    }

    if (lyricsParser.hasLyrics()) {
        int curIdx = 0;
        if (lyricsParser.isSynchronized()) {
            uint32_t curPos = audioPlayer.getPositionMs();
            curIdx = lyricsParser.getCurrentLineIndex(curPos);
        } else {
            if (manualLyricIndex >= (int)lyricsParser.getLineCount()) {
                manualLyricIndex = (int)lyricsParser.getLineCount() - 1;
            }
            if (manualLyricIndex < 0) manualLyricIndex = 0;
            curIdx = manualLyricIndex;
        }

        if (curIdx < 0) curIdx = 0;
        if (curIdx >= (int)lyricsParser.getLineCount()) {
            curIdx = (int)lyricsParser.getLineCount() - 1;
        }

        // ── Font pair selection driven by playerConfig ──────────────────────
        // Each entry: { smallFont (prev/next lines), largeFont (current line) }
        struct LyricsFontPair { const uint8_t *small; const uint8_t *large; };
        static const LyricsFontPair FONT_PAIRS[7] = {
            { u8g2_font_5x7_tr,             u8g2_font_6x10_tr            }, // 0 CLASSIC
            { u8g2_font_04b_03_tr,           u8g2_font_baby_tr             }, // 1 CUTE
            { u8g2_font_blipfest_07_tr,      u8g2_font_nokiafc22_tr        }, // 2 CHIPTUNE
            { u8g2_font_4x6_tr,              u8g2_font_courB08_tr          }, // 3 TYPEWRITER
            { u8g2_font_5x7_tr,              u8g2_font_pressstart2p_8u     }, // 4 ARCADE
            { u8g2_font_4x6_tr,              u8g2_font_5x7_tr              }, // 5 PIXIE
            { u8g2_font_unifont_t_devanagari, u8g2_font_unifont_t_devanagari }, // 6 HINDI
        };
        uint8_t fontIdx = (uint8_t)playerConfig.getLyricsFont();
        if (fontIdx >= 7) fontIdx = 0;
        const LyricsFontPair &fp = FONT_PAIRS[fontIdx];

        // ── Current line — pixel word-wrap in highlight box ──
        String curText = lyricsParser.getLineText(curIdx);
        bool curIsDev = (fontIdx == LYRICS_FONT_HINDI) || isDevanagariText(curText);

        LyricsSize sz = playerConfig.getLyricsSize();
        const uint8_t *curFont = curIsDev ? u8g2_font_unifont_t_devanagari
                               : ((sz == LYRICS_SIZE_SMALL) ? fp.small : fp.large);
        u8g2.setFont(curFont);

        // Pixel-based word-wrap: split at last space before 124px width boundary
        String wrapL1 = curText;
        String wrapL2 = "";
        if (getLyricTextWidth(u8g2, curText, curIsDev) > 124) {
            String candidate = "";
            int lastSpaceByteIdx = -1;
            for (int bi = 0; bi < (int)curText.length(); ) {
                uint8_t c = (uint8_t)curText[bi];
                int cpLen = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                String next = curText.substring(0, bi + cpLen);
                if (getLyricTextWidth(u8g2, next, curIsDev) > 124) break;
                candidate = next;
                if (curText[bi] == ' ') lastSpaceByteIdx = bi;
                bi += cpLen;
            }
            if (lastSpaceByteIdx > 0) {
                wrapL1 = curText.substring(0, lastSpaceByteIdx);
                wrapL2 = curText.substring(lastSpaceByteIdx + 1);
                // Truncate second line to fit
                while (wrapL2.length() > 0 && getLyricTextWidth(u8g2, wrapL2, curIsDev) > 124) {
                    size_t cp = LyricsParser::countUtf8CodePoints(wrapL2);
                    if (cp == 0) break;
                    wrapL2 = LyricsParser::truncateUtf8CodePoints(wrapL2, cp - 1);
                }
            } else {
                wrapL1 = candidate; // hard split, no second line shown
                wrapL2 = "";
            }
        }

        if (curIsDev) {
            // ── Devanagari 16px rendering ───────────────────────────────────
            if (!wrapL2.isEmpty()) {
                // Two wrapped lines of 16px font
                if (showBar) {
                    u8g2.drawBox(0, 12, 128, 38);
                    u8g2.setDrawColor(0);
                    drawLyricText(u8g2, 2, 27, wrapL1, true);
                    drawLyricText(u8g2, 2, 45, wrapL2, true);
                    u8g2.setDrawColor(1);
                } else {
                    // Full screen 2-line wrapped Hindi (height 48px, y=8..56)
                    u8g2.drawBox(0, 8, 128, 48);
                    u8g2.setDrawColor(0);
                    drawLyricText(u8g2, 2, 26, wrapL1, true);
                    drawLyricText(u8g2, 2, 46, wrapL2, true);
                    u8g2.setDrawColor(1);
                }
            } else {
                // Single line of 16px font
                if (showBar) {
                    u8g2.drawBox(0, 19, 128, 24);
                    u8g2.setDrawColor(0);
                    drawLyricText(u8g2, 2, 36, wrapL1, true);
                    u8g2.setDrawColor(1);

                    // If previous line is ASCII, show it in remaining space (y=11..18)
                    if (curIdx > 0) {
                        String prevText = lyricsParser.getLineText(curIdx - 1);
                        if (!isDevanagariText(prevText) && fontIdx != LYRICS_FONT_HINDI) {
                            u8g2.setFont(fp.small);
                            while (prevText.length() > 0 && u8g2.getUTF8Width(prevText.c_str()) > 124) {
                                size_t cp = LyricsParser::countUtf8CodePoints(prevText);
                                if (cp == 0) break;
                                prevText = LyricsParser::truncateUtf8CodePoints(prevText, cp - 1);
                            }
                            u8g2.drawUTF8(2, 17, prevText.c_str());
                        }
                    }

                    // If next line is ASCII, show it in remaining space (y=44..55)
                    if (curIdx + 1 < (int)lyricsParser.getLineCount()) {
                        String nextText = lyricsParser.getLineText(curIdx + 1);
                        if (!isDevanagariText(nextText) && fontIdx != LYRICS_FONT_HINDI) {
                            u8g2.setFont(fp.small);
                            while (nextText.length() > 0 && u8g2.getUTF8Width(nextText.c_str()) > 124) {
                                size_t cp = LyricsParser::countUtf8CodePoints(nextText);
                                if (cp == 0) break;
                                nextText = LyricsParser::truncateUtf8CodePoints(nextText, cp - 1);
                            }
                            u8g2.drawUTF8(2, 53, nextText.c_str());
                        }
                    }
                } else {
                    // Full screen single-line: box y=18..46 (height 28)
                    // Full 64px display allows previous and next lines to be shown cleanly!
                    if (curIdx > 0) {
                        String prevText = lyricsParser.getLineText(curIdx - 1);
                        bool prevIsDev = (fontIdx == LYRICS_FONT_HINDI) || isDevanagariText(prevText);
                        u8g2.setFont(prevIsDev ? u8g2_font_unifont_t_devanagari : fp.small);
                        while (prevText.length() > 0 && getLyricTextWidth(u8g2, prevText, prevIsDev) > 124) {
                            size_t cp = LyricsParser::countUtf8CodePoints(prevText);
                            if (cp == 0) break;
                            prevText = LyricsParser::truncateUtf8CodePoints(prevText, cp - 1);
                        }
                        drawLyricText(u8g2, 2, 14, prevText, prevIsDev);
                    }

                    u8g2.drawBox(0, 18, 128, 28);
                    u8g2.setDrawColor(0);
                    drawLyricText(u8g2, 2, 36, wrapL1, true);
                    u8g2.setDrawColor(1);

                    if (curIdx + 1 < (int)lyricsParser.getLineCount()) {
                        String nextText = lyricsParser.getLineText(curIdx + 1);
                        bool nextIsDev = (fontIdx == LYRICS_FONT_HINDI) || isDevanagariText(nextText);
                        u8g2.setFont(nextIsDev ? u8g2_font_unifont_t_devanagari : fp.small);
                        while (nextText.length() > 0 && getLyricTextWidth(u8g2, nextText, nextIsDev) > 124) {
                            size_t cp = LyricsParser::countUtf8CodePoints(nextText);
                            if (cp == 0) break;
                            nextText = LyricsParser::truncateUtf8CodePoints(nextText, cp - 1);
                        }
                        drawLyricText(u8g2, 2, nextIsDev ? 59 : 58, nextText, nextIsDev);
                    }
                }
            }
        } else {
            // ── Standard Latin/ASCII 3-line rendering ───────────────────────
            if (showBar) {
                // Previous line (y=12–19)
                u8g2.setFont(fp.small);
                if (curIdx > 0) {
                    String prevText = lyricsParser.getLineText(curIdx - 1);
                    while (prevText.length() > 0 && u8g2.getUTF8Width(prevText.c_str()) > 124) {
                        size_t cp = LyricsParser::countUtf8CodePoints(prevText);
                        if (cp == 0) break;
                        prevText = LyricsParser::truncateUtf8CodePoints(prevText, cp - 1);
                    }
                    u8g2.drawUTF8(2, 19, prevText.c_str());
                }

                // Current line in 22px highlight box (y=21..43)
                u8g2.setFont(curFont);
                u8g2.drawBox(0, 21, 128, 22);
                u8g2.setDrawColor(0);
                if (wrapL2.isEmpty()) {
                    u8g2.drawUTF8(2, 34, wrapL1.c_str());
                } else {
                    u8g2.drawUTF8(2, 31, wrapL1.c_str());
                    u8g2.drawUTF8(2, 42, wrapL2.c_str());
                }
                u8g2.setDrawColor(1);

                // Next line (y=46–53)
                u8g2.setFont(fp.small);
                if (curIdx + 1 < (int)lyricsParser.getLineCount()) {
                    String nextText = lyricsParser.getLineText(curIdx + 1);
                    while (nextText.length() > 0 && u8g2.getUTF8Width(nextText.c_str()) > 124) {
                        size_t cp = LyricsParser::countUtf8CodePoints(nextText);
                        if (cp == 0) break;
                        nextText = LyricsParser::truncateUtf8CodePoints(nextText, cp - 1);
                    }
                    u8g2.drawUTF8(2, 53, nextText.c_str());
                }
            } else {
                // Full screen ASCII 3-line mode:
                // Previous line (y=4..13)
                u8g2.setFont(fp.small);
                if (curIdx > 0) {
                    String prevText = lyricsParser.getLineText(curIdx - 1);
                    while (prevText.length() > 0 && u8g2.getUTF8Width(prevText.c_str()) > 124) {
                        size_t cp = LyricsParser::countUtf8CodePoints(prevText);
                        if (cp == 0) break;
                        prevText = LyricsParser::truncateUtf8CodePoints(prevText, cp - 1);
                    }
                    u8g2.drawUTF8(2, 13, prevText.c_str());
                }

                // Current line in 28px highlight box (y=18..46)
                u8g2.setFont(curFont);
                u8g2.drawBox(0, 18, 128, 28);
                u8g2.setDrawColor(0);
                if (wrapL2.isEmpty()) {
                    u8g2.drawUTF8(2, 35, wrapL1.c_str());
                } else {
                    u8g2.drawUTF8(2, 30, wrapL1.c_str());
                    u8g2.drawUTF8(2, 42, wrapL2.c_str());
                }
                u8g2.setDrawColor(1);

                // Next line (y=50..60)
                u8g2.setFont(fp.small);
                if (curIdx + 1 < (int)lyricsParser.getLineCount()) {
                    String nextText = lyricsParser.getLineText(curIdx + 1);
                    while (nextText.length() > 0 && u8g2.getUTF8Width(nextText.c_str()) > 124) {
                        size_t cp = LyricsParser::countUtf8CodePoints(nextText);
                        if (cp == 0) break;
                        nextText = LyricsParser::truncateUtf8CodePoints(nextText, cp - 1);
                    }
                    u8g2.drawUTF8(2, 58, nextText.c_str());
                }
            }
        }

        // ── Footer row (y=56–63) (Only shown when showBar is true) ──────────
        if (showBar) {
            char footBuf[28];
            if (lyricsParser.isSynchronized()) {
                snprintf(footBuf, sizeof(footBuf), "> Synced [%s]",
                         (curIsDev && fontIdx != LYRICS_FONT_HINDI) ? "HINDI" : playerConfig.getLyricsFontName());
            } else {
                snprintf(footBuf, sizeof(footBuf), "Line %d/%d (UP/DN)",
                         curIdx + 1, (int)lyricsParser.getLineCount());
            }
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(2, 63, footBuf);
        }

    } else {
        // No lyrics fallback
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 26, "No Lyrics Available");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(6, 40, "Place [song].lrc or .txt");
        u8g2.drawStr(6, 50, "next to audio file on SD");
        u8g2.drawStr(6, 62, "HOLD OK: Return");
    }

    display.sendBuffer();
}

UIPlayer uiPlayer;
