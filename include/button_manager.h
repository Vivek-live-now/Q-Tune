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
    BTN_EVENT_CANCEL_PRESS
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
    };

    Button btnUp;
    Button btnOk;
    Button btnDn;
    Button btnCancel;
    const unsigned long debounceDelay = 50;

    bool checkButton(Button &btn);
};

extern ButtonManager buttonManager;

#endif // BUTTON_MANAGER_H
