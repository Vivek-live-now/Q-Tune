#include "ui_player.h"
#include "led_manager.h"
#include "battery.h"
#include "spectrum_analyzer.h"

UIPlayer::UIPlayer() :
    currentTrackIndex(0),
    trackScrollOffset(0),
    inListMode(true),
    playbackMode(PLAY_MODE_ALL),
    volumeOverlayExpiry(0),
    currentView(VIEW_CATEGORIES),
    previousView(VIEW_CATEGORIES),
    currentPage(PAGE_NOW_PLAYING),
    categorySelection(0),
    categoryScrollOffset(0),
    currentCategoryTitle("Q-TUNES MUSIC"),
    currentFolderPath("/music"),
    folderSelection(0),
    folderScrollOffset(0) {}

void UIPlayer::begin() {
    if (!sdManager.isMounted()) sdManager.begin();

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
    playbackMode = PLAY_MODE_ALL;
}

void UIPlayer::openNowPlaying() {
    currentView = VIEW_PLAYER;
    inListMode = false;
    currentPage = PAGE_NOW_PLAYING;
    lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
}

PlayerPage UIPlayer::getCurrentPage() const {
    return currentPage;
}

void UIPlayer::setPage(PlayerPage page) {
    currentPage = page;
}

void UIPlayer::cyclePage() {
    currentPage = (PlayerPage)((currentPage + 1) % 3);
    if (currentPage == PAGE_LYRICS) {
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }
}

void UIPlayer::refreshTrackList() {
    if (!sdManager.isMounted()) sdManager.begin();
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
        currentView = VIEW_CATEGORIES;
        ledManager.onPlaybackStop();
    }
    lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
}

void UIPlayer::playPreviousTrack() {
    if (trackList.empty()) return;
    currentTrackIndex = (currentTrackIndex - 1 + trackList.size()) % trackList.size();
    audioPlayer.playFile(trackList[currentTrackIndex]);
    lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
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
    if (!inListMode && currentView == VIEW_PLAYER && audioPlayer.hasFinished()) {
        audioPlayer.clearFinished();
        playNextTrack();
    }

    ButtonEvent evt = buttonManager.update();

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
        catLabels.push_back("Playlists");
        catLabels.push_back("Recently Played");
        catLabels.push_back(String("Playback: ") + getPlaybackModeString());
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
                    // If playing, select currently playing track
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
                case 4: // Playlists
                    currentCategoryTitle = "PLAYLISTS";
                    trackList = sdManager.listPlaylists();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 5: // Recently Played
                    currentCategoryTitle = "RECENT TRACKS";
                    trackList = sdManager.getRecentTracks();
                    currentView = VIEW_TRACK_LIST;
                    inListMode = true;
                    trackScrollOffset = 0;
                    currentTrackIndex = 0;
                    break;
                case 6: // Playback Mode toggle
                    cyclePlaybackMode();
                    break;
                case 7: // Rescan Library
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
        renderCategories();

    } else if (currentView == VIEW_TRACK_LIST) {
        // --- 2. Track List View ---
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

                // If this is a Playlist file (.m3u), load its songs
                if (selected.endsWith(".m3u") || selected.endsWith(".m3u8")) {
                    currentCategoryTitle = "PLAYLIST";
                    trackList = sdManager.loadPlaylist(selected);
                    currentTrackIndex = 0;
                    trackScrollOffset = 0;
                    return true;
                }

                // If user selected the track that is ALREADY PLAYING:
                // DO NOT RESTART FROM BEGINNING! Return straight to Now Playing!
                if ((audioPlayer.isPlaying() || audioPlayer.isPaused()) &&
                    audioPlayer.getCurrentTrackPath() == selected) {
                    openNowPlaying();
                    ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
                    return true;
                }

                // Play new track
                audioPlayer.playFile(selected);
                openNowPlaying();
                ledManager.onPlaybackStart();
            } else {
                refreshTrackList();
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            currentView = VIEW_CATEGORIES;
        }
        renderTrackList();

    } else if (currentView == VIEW_FOLDER_BROWSER) {
        // --- 3. Hierarchical Folder Browser View ---
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
                    // Go up one directory level
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
                // Enter subdirectory
                String newPath = currentFolderPath;
                if (!newPath.endsWith("/")) newPath += "/";
                newPath += currentFolderDirs[idx];
                enterFolder(newPath);
            } else {
                // Play audio file
                int fileIdx = idx - currentFolderDirs.size();
                if (fileIdx >= 0 && fileIdx < (int)currentFolderFiles.size()) {
                    String selected = currentFolderFiles[fileIdx];
                    if ((audioPlayer.isPlaying() || audioPlayer.isPaused()) &&
                        audioPlayer.getCurrentTrackPath() == selected) {
                        // Already playing: DO NOT RESTART!
                        openNowPlaying();
                        return true;
                    }
                    // Load folder's files into trackList for consecutive playback
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
        renderFolderBrowser();

    } else {
        // --- 4. Now Playing Multi-Page View (VIEW_PLAYER) ---
        if (evt == BTN_EVENT_SEL_PRESS) {
            if (currentPage == PAGE_TRACK_INFO) {
                // On Page 2 (Info), single click OK cycles Playback Mode
                cyclePlaybackMode();
                ledManager.triggerButtonPulse(CRGB::Magenta, 1, 60);
            } else {
                // On Page 1 (Now Playing) and Page 3 (Lyrics), single click OK toggles Play/Pause
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
            if (currentPage == PAGE_TRACK_INFO || currentPage == PAGE_LYRICS) {
                // Short CANCEL from Page 2 or Page 3 returns directly to Page 1 (Now Playing)
                currentPage = PAGE_NOW_PLAYING;
            } else {
                // Short CANCEL from Page 1 returns to Category / Library menu
                currentView = VIEW_CATEGORIES;
                inListMode = true;
            }
        }

        renderPlayer();
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
    items.push_back("Playlists");
    items.push_back("Recently Played");
    items.push_back(String("Playback: ") + getPlaybackModeString());
    items.push_back("Rescan Library");

    for (size_t i = 0; i < items.size(); i++) {
        itemPtrs.push_back(items[i].c_str());
    }

    display.drawStandardMenu("Q-TUNES MUSIC", itemPtrs.data(), (int)items.size(), categorySelection, categoryScrollOffset, (const String*)nullptr, batBuf);
    display.sendBuffer();
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

            // Add playing indicator if currently active!
            if (trackList[i] == currentPlaying && (audioPlayer.isPlaying() || audioPlayer.isPaused())) {
                name = "▶ " + name;
            }

            if (name.length() > 18) name = name.substring(0, 16) + "..";
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
        if (name.length() > 18) name = name.substring(0, 16) + "..";
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
        if (name.length() > 18) name = name.substring(0, 16) + "..";
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
        case PAGE_TRACK_INFO:
            renderPlayerPage2();
            break;
        case PAGE_LYRICS:
            renderPlayerPage3();
            break;
        case PAGE_NOW_PLAYING:
        default:
            renderPlayerPage1();
            break;
    }
}

// ============================================================================
// Page 1: Standard Now Playing with Mini Spectrum & Progress
// ============================================================================
void UIPlayer::renderPlayerPage1() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Top status header (Battery, Output Mode, Page 1/3)
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "B:%d%% [%s] [1/3]",
             battery.getPercentage(), audioPlayer.getOutputModeShortName());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    // Track Title
    String displayName = audioPlayer.getCurrentTrackName();
    if (displayName.length() == 0 && !trackList.empty() && currentTrackIndex < (int)trackList.size()) {
        displayName = trackList[currentTrackIndex];
        int slash = displayName.lastIndexOf('/');
        if (slash >= 0) displayName = displayName.substring(slash + 1);
    }
    if (displayName.length() > 20) {
        displayName = displayName.substring(0, 18) + "..";
    }
    u8g2.drawStr(0, 23, displayName.c_str());

    // Playback state
    String stateStr = "■ STOPPED";
    if (audioPlayer.isPlaying()) {
        stateStr = "▶ PLAYING (" + String(audioPlayer.getFormatName()) + ")";
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

    // Mini Spectrum Visualizer (Live audio tap)
    spectrumAnalyzer.sampleAudioStream();
    spectrumAnalyzer.renderMiniBars(u8g2, 92, 28, 34, 18);

    // Progress bar
    u8g2.drawFrame(0, 53, 128, 7);
    if (durSec > 0) {
        int progressWidth = (posSec * 126) / durSec;
        if (progressWidth > 126) progressWidth = 126;
        u8g2.drawBox(1, 54, progressWidth, 5);
    }

    // Volume popup overlay
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

// ============================================================================
// Page 2: Track & Audio Technical Details
// ============================================================================
void UIPlayer::renderPlayerPage2() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_5x7_tr);

    // Top status header
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "TRACK INFO  [2/3]  B:%d%%", battery.getPercentage());
    u8g2.drawStr(0, 8, headerBuf);
    u8g2.drawHLine(0, 10, 128);

    // Format & Codec
    char buf[40];
    snprintf(buf, sizeof(buf), "Codec: %s (%d-bit)", audioPlayer.getFormatName(), audioPlayer.getBitsPerSample());
    u8g2.drawStr(2, 19, buf);

    // Sample Rate & Channels
    uint32_t sr = audioPlayer.getSampleRate();
    const char* chStr = (audioPlayer.getChannels() == 1) ? "Mono" : "Stereo";
    snprintf(buf, sizeof(buf), "Rate: %u.%ukHz %s", sr / 1000, (sr % 1000) / 100, chStr);
    u8g2.drawStr(2, 28, buf);

    // Bitrate
    uint32_t kbps = audioPlayer.getBitrateKbps();
    snprintf(buf, sizeof(buf), "Bitrate: %u kbps", kbps);
    u8g2.drawStr(2, 37, buf);

    // File size in MB
    uint32_t sz = audioPlayer.getTotalBytes();
    snprintf(buf, sizeof(buf), "Size: %u.%01u MB", sz / 1048576, (sz % 1048576) / 104857);
    u8g2.drawStr(2, 46, buf);

    // Output Route
    snprintf(buf, sizeof(buf), "Out: %s", audioPlayer.getOutputModeName());
    u8g2.drawStr(2, 55, buf);

    // Mode toggle prompt
    snprintf(buf, sizeof(buf), "Mode: %s (OK: Cycle)", getPlaybackModeString());
    u8g2.drawStr(2, 63, buf);

    display.sendBuffer();
}

// ============================================================================
// Page 3: Synchronized LRC Lyrics Viewer
// ============================================================================
void UIPlayer::renderPlayerPage3() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    // Header
    u8g2.setFont(u8g2_font_5x7_tr);
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "LYRICS  [3/3]  B:%d%%", battery.getPercentage());
    u8g2.drawStr(0, 8, headerBuf);
    u8g2.drawHLine(0, 10, 128);

    if (!lyricsParser.hasLyrics()) {
        lyricsParser.loadForTrack(audioPlayer.getCurrentTrackPath());
    }

    if (lyricsParser.hasLyrics()) {
        uint32_t curPos = audioPlayer.getPositionMs();
        int curIdx = lyricsParser.getCurrentLineIndex(curPos);

        // Previous Line (dim / smaller)
        u8g2.setFont(u8g2_font_5x7_tr);
        if (curIdx > 0) {
            String prevText = lyricsParser.getLineText(curIdx - 1);
            if (prevText.length() > 24) prevText = prevText.substring(0, 22) + "..";
            u8g2.drawStr(2, 22, prevText.c_str());
        }

        // Active Current Line (Highlighted in inverted box!)
        String curText = lyricsParser.getLineText(curIdx);
        if (curText.length() > 20) curText = curText.substring(0, 18) + "..";
        u8g2.drawBox(0, 26, 128, 14);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(2, 37, curText.c_str());
        u8g2.setDrawColor(1);

        // Next Line (smaller)
        u8g2.setFont(u8g2_font_5x7_tr);
        if (curIdx + 1 < (int)lyricsParser.getLineCount()) {
            String nextText = lyricsParser.getLineText(curIdx + 1);
            if (nextText.length() > 24) nextText = nextText.substring(0, 22) + "..";
            u8g2.drawStr(2, 51, nextText.c_str());
        }

        // Timestamp tag at bottom
        uint32_t posSec = curPos / 1000;
        char tsBuf[16];
        snprintf(tsBuf, sizeof(tsBuf), "[%02u:%02u]", posSec / 60, posSec % 60);
        u8g2.drawStr(2, 62, tsBuf);
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 26, "No .lrc File Found");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(6, 40, "Place [song].lrc next to");
        u8g2.drawStr(6, 50, "audio file on SD card.");
        u8g2.drawStr(6, 62, "HOLD OK: Page 1");
    }

    display.sendBuffer();
}

UIPlayer uiPlayer;
