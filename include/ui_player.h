#ifndef UI_PLAYER_H
#define UI_PLAYER_H

#include <Arduino.h>
#include <vector>
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"
#include "lyrics_parser.h"

enum PlaybackMode {
    PLAY_MODE_ALL,        // Repeat All tracks sequentially
    PLAY_MODE_REPEAT_ONE, // Repeat Current track indefinitely
    PLAY_MODE_SHUFFLE,    // Play tracks randomly
    PLAY_MODE_SINGLE      // Stop when current track ends
};

enum UIPlayerView {
    VIEW_CATEGORIES,       // Music Categories Menu (Now Playing, All Songs, Folders, etc.)
    VIEW_TRACK_LIST,       // List of tracks (All Songs, Artist tracks, Album tracks, etc.)
    VIEW_FOLDER_BROWSER,   // Hierarchical directory browser
    VIEW_PLAYER            // Player screens (Pages 1, 2, 3)
};

enum PlayerPage {
    PAGE_NOW_PLAYING = 0,  // Page 1: Progress, spectrum, elapsed/total, format
    PAGE_TRACK_INFO = 1,   // Page 2: Audio engine & track technical details
    PAGE_LYRICS = 2        // Page 3: Synchronized LRC lyrics
};

class UIPlayer {
public:
    UIPlayer();
    void begin();
    bool update(); // Returns true while active, false when user exits to Main Menu

    void playNextTrack();
    void playPreviousTrack();
    void setPlaybackMode(PlaybackMode mode);
    PlaybackMode getPlaybackMode() const;
    void cyclePlaybackMode();
    const char* getPlaybackModeString() const;

    PlayerPage getCurrentPage() const;
    void setPage(PlayerPage page);
    void cyclePage();
    void openNowPlaying();

private:
    std::vector<String> trackList;
    int currentTrackIndex;
    int trackScrollOffset;
    bool inListMode;
    PlaybackMode playbackMode;
    unsigned long volumeOverlayExpiry;

    // View & Page State
    UIPlayerView currentView;
    UIPlayerView previousView;
    PlayerPage currentPage;

    // Category navigation
    int categorySelection;
    int categoryScrollOffset;
    String currentCategoryTitle;

    // Folder navigation
    String currentFolderPath;
    std::vector<String> currentFolderDirs;
    std::vector<String> currentFolderFiles;
    int folderSelection;
    int folderScrollOffset;

    void refreshTrackList();
    void renderCategories();
    void renderTrackList();
    void renderFolderBrowser();
    void renderPlayer();
    void renderPlayerPage1();
    void renderPlayerPage2();
    void renderPlayerPage3();
    void showVolumeOverlay();
    void enterFolder(const String &folderPath);
    void refreshCurrentFolder();
};

extern UIPlayer uiPlayer;

#endif // UI_PLAYER_H
