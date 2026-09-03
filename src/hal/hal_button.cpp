#include <Button2.h>
// #include "hal.h"
#include "lcd_menu.h"

void Key1Click(Button2&btn1);
void Key2Click(Button2&btn2);
void Key1LongPress(Button2& btn1);
void Key2LongPress(Button2& btn2);
void Key2DoubleClick(Button2& btn2);
void Key1DoubleClick(Button2& btn1);

Button2 btn1;
Button2 btn2;

// uint16_t triggerTime = 500;
uint16_t timeOut = 1000;

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
    btn1.setDoubleClickHandler(Key1DoubleClick);
    btn2.setDoubleClickHandler(Key2DoubleClick);
}

void HAL::Button_Click(){
    btn1.loop();
    btn2.loop();
}

// ============================================================
// 按键逻辑: 左键=后退/减, 右键=前进/加
// 物理键 SW0/SW1 与左/右键的映射随屏幕方向(currentRotation)自动交换,
// 故只需维护一份左右逻辑, 回调仅做“物理键→逻辑键”转发。
// ============================================================

// 物理键 -> 是否逻辑左键 (默认方向3: SW0=左, SW1=右; 方向1: 对调)
static bool keyIsLeft(int sw) {
    return (sw == BUTTON_SW0) ? (currentRotation != 1) : (currentRotation == 1);
}

static void onLeftClick() {
    if (nowApp == AppState::MENU) {
        switch (MenuConfig::currentMode) {
            case MenuConfig::MODE_IDLE:
                MenuConfig::Menu_Exit();
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
}

static void onRightClick() {
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
    } else if (nowApp == AppState::STOREAGE_DATA) {
        if (record_interval_s == 0) { HAL::ShowToast("数据保存未开启"); return; }
        HAL::Storage_RequestSelectNext();
    }
}

static void onLeftLongPress() {
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
}

static void onRightLongPress() {
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
    } else if (nowApp == AppState::STOREAGE_DATA) {
        if (record_interval_s == 0) { HAL::ShowToast("数据保存未开启"); return; }
        HAL::Storage_RequestToggleRecord();
    } else if (nowApp == AppState::MAIN) {
        nowApp = AppState::SYSTEM_INFO;
    }
}

static void onLeftDoubleClick() {
    // 左键双击: 切换采样率
    sample_mode = (sample_mode + 1) % 3;
    HAL::Sys_NVS_Write("sample_mode", sample_mode);
    HAL::INA22x_SetConfig(sample_mode);
    const char* modeNames[] = {"快速", "默认", "慢速"};
    char msg[32];
    snprintf(msg, sizeof(msg), "采样速度: %s", modeNames[sample_mode]);
    HAL::ShowToast(msg);
}

static void onRightDoubleClick() {
    if (nowApp == AppState::STOREAGE_DATA) {
        if (record_interval_s == 0) { HAL::ShowToast("数据保存未开启"); return; }
        HAL::Storage_RequestDeleteSelected();
        return;
    }
    // 右键双击: 切换屏幕方向
    uint8_t newRotation = (currentRotation == 1) ? 3 : 1;
    HAL::Sys_NVS_Write("rotation", newRotation);
    pendingRotation = newRotation;
    HAL::ShowToast(newRotation == 1 ? "方向: 下" : "方向: 上");
}

// ============================================================
// Button2 回调: 物理键 -> 逻辑键 -> 统一处理
// ============================================================

void Key1Click(Button2& btn1) {
    if (keyIsLeft(BUTTON_SW0)) onLeftClick(); else onRightClick();
}

void Key2Click(Button2& btn2) {
    if (keyIsLeft(BUTTON_SW1)) onLeftClick(); else onRightClick();
}

void Key1LongPress(Button2& btn1) {
    if (keyIsLeft(BUTTON_SW0)) onLeftLongPress(); else onRightLongPress();
}

void Key2LongPress(Button2& btn2) {
    if (keyIsLeft(BUTTON_SW1)) onLeftLongPress(); else onRightLongPress();
}

void Key1DoubleClick(Button2& btn1) {
    if (nowApp == AppState::MENU) return;   // 设置菜单中禁用全局双击快捷键
    if (keyIsLeft(BUTTON_SW0)) onLeftDoubleClick(); else onRightDoubleClick();
}

void Key2DoubleClick(Button2& btn2) {
    if (nowApp == AppState::MENU) return;
    if (keyIsLeft(BUTTON_SW1)) onLeftDoubleClick(); else onRightDoubleClick();
}