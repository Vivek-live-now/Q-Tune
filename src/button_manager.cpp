#include "button_manager.h"

ButtonManager::ButtonManager() :
    btnUp{BTN_UP, HIGH, HIGH, 0, 0, false, 0},
    btnOk{BTN_OK, HIGH, HIGH, 0, 0, false, 0},
    btnDn{BTN_DN, HIGH, HIGH, 0, 0, false, 0},
    btnCancel{BTN_CANCEL, HIGH, HIGH, 0, 0, false, 0} {}

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
                btn.pressStartTime = millis();
                btn.holdTriggered = false;
                btn.lastRepeatTime = millis();
                return true;
            }
        }
    }
    return false;
}

bool ButtonManager::checkButtonHold(Button &btn) {
    if (btn.currentState == LOW) {
        unsigned long now = millis();
        if (!btn.holdTriggered && (now - btn.pressStartTime >= holdThreshold)) {
            btn.holdTriggered = true;
            btn.lastRepeatTime = now;
            return true;
        } else if (btn.holdTriggered && (now - btn.lastRepeatTime >= repeatInterval)) {
            btn.lastRepeatTime = now;
            return true;
        }
    }
    return false;
}

ButtonEvent ButtonManager::update() {
    // Hold / repeat events
    if (checkButtonHold(btnUp)) return BTN_EVENT_UP_HOLD;
    if (checkButtonHold(btnDn)) return BTN_EVENT_DN_HOLD;
    if (checkButtonHold(btnOk)) return BTN_EVENT_OK_HOLD;
    if (checkButtonHold(btnCancel)) return BTN_EVENT_CANCEL_HOLD;

    // Transition press events
    if (checkButton(btnUp)) return BTN_EVENT_UP_PRESS;
    if (checkButton(btnOk)) return BTN_EVENT_OK_PRESS;
    if (checkButton(btnDn)) return BTN_EVENT_DN_PRESS;
    if (checkButton(btnCancel)) return BTN_EVENT_CANCEL_PRESS;
    return BTN_EVENT_NONE;
}

ButtonManager buttonManager;
