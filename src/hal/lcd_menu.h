#pragma once

#include "hal.h"

namespace MenuConfig {

    enum MenuMode {
        MODE_IDLE,      // 无选中项
        MODE_SELECT,    // 选择导航
        MODE_EDIT       // 编辑模式
    };

    extern MenuMode currentMode;
    extern uint8_t selectedIndex;      // 当前选中项（仅 SELECT/EDIT 有效）
    extern uint8_t editItem;            // 当前编辑项（仅 EDIT 有效）

    // 临时编辑值
    extern int16_t tempBrightness;
    extern int16_t tempRotation;
    extern int16_t tempSampleMode;

    constexpr uint8_t menuItemCount = 4; // Brightness, Rotation, Sample, Exit

    void Menu_Init();                   // 重置为空闲模式
    void EnterSelectMode();              // 空闲 选择，默认选中第一项
    void ExitSelectMode();               // 选择 空闲
    void EnterEditMode();                // 选择 编辑，初始化临时值
    void ExitEditMode(bool save);        // 编辑 选择，根据 save 决定是否写入 NVS
    void SelectNext();
    void SelectPrev();
    void AdjustValue(int8_t delta);      // 编辑模式下调整临时值

    const char* GetTitle(uint8_t index);
    void GetValueStr(uint8_t index, char* buffer);
    bool ShouldShowValue(uint8_t index);

} // namespace MenuConfig

namespace MenuColors {
    constexpr uint16_t BACKGROUND     = 0x0000;
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF;
    constexpr uint16_t TEXT_SECONDARY = 0x8410;
    constexpr uint16_t TEXT_SELECTED  = 0xFFFF;
    constexpr uint16_t SELECT_BG      = 0x0320;  // 深绿
    constexpr uint16_t SEPARATOR      = 0x4A4A;
}