#include "button_manager.h"

ButtonManager::ButtonManager() :
    btnUp{BTN_UP, HIGH, HIGH, 0},
    btnSel{BTN_SEL, HIGH, HIGH, 0},
    btnDn{BTN_DN, HIGH, HIGH, 0} {}

void ButtonManager::begin() {
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_SEL, INPUT_PULLUP);
    pinMode(BTN_DN, INPUT_PULLUP);
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
    if (checkButton(btnSel)) return BTN_EVENT_SEL_PRESS;
    if (checkButton(btnDn)) return BTN_EVENT_DN_PRESS;
    return BTN_EVENT_NONE;
}

ButtonManager buttonManager;
