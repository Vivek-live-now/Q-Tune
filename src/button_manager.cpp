#include "button_manager.h"

ButtonManager::ButtonManager() :
    btnUp{BTN_UP, HIGH, HIGH, 0},
    btnOk{BTN_OK, HIGH, HIGH, 0},
    btnDn{BTN_DN, HIGH, HIGH, 0},
    btnCancel{BTN_CANCEL, HIGH, HIGH, 0} {}

void ButtonManager::begin() {
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);
    pinMode(BTN_DN, INPUT_PULLUP);
    pinMode(BTN_CANCEL, INPUT_PULLUP);
}

bool ButtonManager::checkButton(Button &btn) {
    bool reading = digitalRead(btn.pin);
    if (reading != btn.lastState) {
        btn.lastDebounceTime = millis();
    }
    btn.lastState = reading;

    if ((millis() - btn.lastDebounceTime) > debounceDelay) {
        if (reading != btn.currentState) {
            btn.currentState = reading;
            if (btn.currentState == LOW) {
                return true;
            }
        }
    }
    return false;
}

ButtonEvent ButtonManager::update() {
    if (checkButton(btnUp)) return BTN_EVENT_UP_PRESS;
    if (checkButton(btnOk)) return BTN_EVENT_OK_PRESS;
    if (checkButton(btnDn)) return BTN_EVENT_DN_PRESS;
    if (checkButton(btnCancel)) return BTN_EVENT_CANCEL_PRESS;
    return BTN_EVENT_NONE;
}

ButtonManager buttonManager;
