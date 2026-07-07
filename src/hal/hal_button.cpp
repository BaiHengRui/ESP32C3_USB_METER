#include <Button2.h>
// #include "hal.h"
#include "lcd_menu.h"

void Key1Click(Button2&btn1);
void Key2Click(Button2&btn2);
void Key1LongPress(Button2& btn1);
void Key2LongPress(Button2& btn2);

Button2 btn1;
Button2 btn2;

// uint16_t triggerTime = 500;
uint16_t timeOut = 2000;

void HAL::Button_Init(){
    pinMode(BUTTON_SW0, INPUT_PULLDOWN);
    pinMode(BUTTON_SW1, INPUT_PULLDOWN);
    btn1.begin(BUTTON_SW0); //boot pin
    btn2.begin(BUTTON_SW1);

    btn1.setLongClickTime(timeOut);
    btn2.setLongClickTime(timeOut);

    btn1.setClickHandler(Key1Click);
    btn2.setClickHandler(Key2Click);
    btn1.setLongClickDetectedHandler(Key1LongPress);
    btn2.setLongClickDetectedHandler(Key2LongPress);
}

void HAL::Button_Click(){
    btn1.loop();
    btn2.loop();
}

void Key1Click(Button2& btn1) {
    // Rotation 1时，SW0物理在右侧 → 交换左右功能
    bool isLeft = (currentRotation == 1) ? false : true;
    if (isLeft) {
        // === 左键逻辑 (原Key1Click) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    nowApp = AppState::MAIN;
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::SelectPrev();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::AdjustValue(-1);
                    break;
            }
        } else {
            nowApp = (nowApp + 1) % (maxApp + 1);
        }
    } else {
        // === 右键逻辑 (原Key2Click) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    MenuConfig::EnterSelectMode();
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::SelectNext();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::AdjustValue(+1);
                    break;
            }
        } else if (nowApp == AppState::WAVEGRAPH) {
            graphPaused = !graphPaused;
        }
    }
}

void Key2Click(Button2& btn2) {
    // Rotation 1时，SW1物理在左侧 → 交换左右功能
    bool isLeft = (currentRotation == 1) ? true : false;
    if (isLeft) {
        // === 左键逻辑 (原Key1Click) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    nowApp = AppState::MAIN;
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::SelectPrev();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::AdjustValue(-1);
                    break;
            }
        } else {
            nowApp = (nowApp + 1) % (maxApp + 1);
        }
    } else {
        // === 右键逻辑 (原Key2Click) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    MenuConfig::EnterSelectMode();
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::SelectNext();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::AdjustValue(+1);
                    break;
            }
        } else if (nowApp == AppState::WAVEGRAPH) {
            graphPaused = !graphPaused;
        }
    }
}

void Key1LongPress(Button2& btn1) {
    // Rotation 1时，SW0物理在右侧 → 交换左右功能
    bool isLeft = (currentRotation == 1) ? false : true;
    if (isLeft) {
        // === 左键长按逻辑 (原Key1LongPress) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::ExitSelectMode();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::ExitEditMode(false);
                    break;
            }
        } else {
            nowApp = AppState::MAIN;
        }
    } else {
        // === 右键长按逻辑 (原Key2LongPress) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    MenuConfig::EnterSelectMode();
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::EnterEditMode();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::ExitEditMode(true);
                    break;
            }
        } else if (nowApp == AppState::WAVEGRAPH) {
            graphPaused = !graphPaused;
        } else if (nowApp == AppState::MAIN) {
            nowApp = AppState::SYSTEM_INFO;
        }
    }
}

void Key2LongPress(Button2& btn2) {
    // Rotation 1时，SW1物理在左侧 → 交换左右功能
    bool isLeft = (currentRotation == 1) ? true : false;
    if (isLeft) {
        // === 左键长按逻辑 (原Key1LongPress) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::ExitSelectMode();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::ExitEditMode(false);
                    break;
            }
        } else {
            nowApp = AppState::MAIN;
        }
    } else {
        // === 右键长按逻辑 (原Key2LongPress) ===
        if (nowApp == AppState::MENU) {
            switch (MenuConfig::currentMode) {
                case MenuConfig::MODE_IDLE:
                    MenuConfig::EnterSelectMode();
                    break;
                case MenuConfig::MODE_SELECT:
                    MenuConfig::EnterEditMode();
                    break;
                case MenuConfig::MODE_EDIT:
                    MenuConfig::ExitEditMode(true);
                    break;
            }
        } else if (nowApp == AppState::WAVEGRAPH) {
            graphPaused = !graphPaused;
        } else if (nowApp == AppState::MAIN) {
            nowApp = AppState::SYSTEM_INFO;
        }
    }
}