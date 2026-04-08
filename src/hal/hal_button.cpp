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
    if (nowApp == AppState::MENU) {
        switch (MenuConfig::currentMode) {
            case MenuConfig::MODE_IDLE:
                // 空闲模式短按左键：退出菜单
                nowApp = AppState::MAIN;
                break;
            case MenuConfig::MODE_SELECT:
                MenuConfig::SelectPrev();   // 上一项
                break;
            case MenuConfig::MODE_EDIT:
                MenuConfig::AdjustValue(+1); // 增加
                break;
        }
    } else {
        // 非菜单：切换应用
        nowApp = (nowApp + 1) % (maxApp + 1);
    }
    HAL::LOG_INFO("SW1 OnClick");
}

void Key2Click(Button2& btn2) {
    if (nowApp == AppState::MENU) {
        switch (MenuConfig::currentMode) {
            case MenuConfig::MODE_IDLE:
                // 空闲模式短按右键：无操作（可忽略）
                break;
            case MenuConfig::MODE_SELECT:
                MenuConfig::SelectNext();   // 下一项
                break;
            case MenuConfig::MODE_EDIT:
                MenuConfig::AdjustValue(-1); // 减少
                break;
        }
    } else if (nowApp == AppState::WAVEGRAPH) {
        graphPaused = !graphPaused;
    }
    HAL::LOG_INFO("SW2 OnClick");
}

void Key1LongPress(Button2& btn1) {
    if (nowApp == AppState::MENU) {
        switch (MenuConfig::currentMode) {
            case MenuConfig::MODE_IDLE:
                // 空闲模式长按左键：无操作
                break;
            case MenuConfig::MODE_SELECT:
                MenuConfig::ExitSelectMode(); // 返回空闲
                break;
            case MenuConfig::MODE_EDIT:
                MenuConfig::ExitEditMode(false); // 不保存退出编辑
                break;
        }
    } else {
        nowApp = AppState::MAIN;
    }
    HAL::LOG_INFO("SW1 TimeOut");
}

void Key2LongPress(Button2& btn2) {
    if (nowApp == AppState::MENU) {
        switch (MenuConfig::currentMode) {
            case MenuConfig::MODE_IDLE:
                MenuConfig::EnterSelectMode(); // 空闲 → 选择
                break;
            case MenuConfig::MODE_SELECT:
                MenuConfig::EnterEditMode();   // 选择 → 编辑
                break;
            case MenuConfig::MODE_EDIT:
                MenuConfig::ExitEditMode(true); // 保存并退出编辑
                break;
        }
    } else if (nowApp == AppState::WAVEGRAPH) {
        graphPaused = !graphPaused;
    }else if (nowApp == AppState::MAIN) {
        nowApp = AppState::SYSTEM_INFO;
    }
    HAL::LOG_INFO("SW2 TimeOut");
}