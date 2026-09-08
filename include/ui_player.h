#ifndef UI_PLAYER_H
#define UI_PLAYER_H

#include <Arduino.h>
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"

class UIPlayer {
public:
    UIPlayer();
    void begin();
    void update();

private:
    std::vector<String> trackList;
    int currentTrackIndex;
    bool inListMode;

    void refreshTrackList();
    void renderTrackList();
    void renderPlayer();
};

extern UIPlayer uiPlayer;

#endif // UI_PLAYER_H
