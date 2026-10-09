#ifndef UI_PLAYER_H
#define UI_PLAYER_H

#include <Arduino.h>
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"

enum PlaybackMode {
    PLAY_MODE_ALL,        // Repeat All tracks sequentially
    PLAY_MODE_REPEAT_ONE, // Repeat Current track indefinitely
    PLAY_MODE_SHUFFLE,    // Play tracks randomly
    PLAY_MODE_SINGLE      // Stop when current track ends
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

private:
    std::vector<String> trackList;
    int currentTrackIndex;
    int trackScrollOffset;
    bool inListMode;
    PlaybackMode playbackMode;
    unsigned long volumeOverlayExpiry;

    void refreshTrackList();
    void renderTrackList();
    void renderPlayer();
    void showVolumeOverlay();
};

extern UIPlayer uiPlayer;

#endif // UI_PLAYER_H
