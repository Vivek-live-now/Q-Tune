#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "hw_config.h"

enum ButtonEvent {
    BTN_EVENT_NONE,
    BTN_EVENT_UP_PRESS,
    BTN_EVENT_SEL_PRESS,
    BTN_EVENT_OK_PRESS = BTN_EVENT_SEL_PRESS,
    BTN_EVENT_DN_PRESS,
    BTN_EVENT_CANCEL_PRESS,
    BTN_EVENT_UP_HOLD,
    BTN_EVENT_DN_HOLD,
    BTN_EVENT_OK_HOLD,
    BTN_EVENT_SEL_HOLD = BTN_EVENT_OK_HOLD,
    BTN_EVENT_CANCEL_HOLD
};

class ButtonManager {
public:
    ButtonManager();
    void begin();
    ButtonEvent update();

private:
    struct Button {
        uint8_t pin;
        bool lastState;
        bool currentState;
        unsigned long lastDebounceTime;
        unsigned long pressStartTime;
        bool holdTriggered;
        unsigned long lastRepeatTime;
    };

    Button btnUp;
    Button btnOk;
    Button btnDn;
    Button btnCancel;
    const unsigned long debounceDelay = 50;
    const unsigned long holdThreshold = 450;
    const unsigned long repeatInterval = 150;

    bool checkButton(Button &btn);
    bool checkButtonHold(Button &btn);
};

extern ButtonManager buttonManager;

#endif // BUTTON_MANAGER_H
