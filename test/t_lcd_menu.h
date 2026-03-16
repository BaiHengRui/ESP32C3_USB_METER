#pragma once

#include "hal.h"

namespace MenuConfig {

    extern uint8_t selectedIndex;
    extern bool isEditing;
    extern uint8_t editItem;

    constexpr uint8_t menuItemCount = 3; // Brightness, Rotation, Exit

    void Init();
    void SelectNext();
    void SelectPrev();
    void EnterEdit();
    void ExitEdit();
    void AdjustValue(int8_t delta);
    void ConfirmSelection(); // 用于 Exit

    const char* GetTitle(uint8_t index);
    void GetValueStr(uint8_t index, char* buffer);
    bool ShouldShowValue(uint8_t index); // 判断是否显示值（Exit 不显示）

} // namespace MenuConfig

namespace MenuColors {
    constexpr uint16_t BACKGROUND     = 0x0000;  // 黑
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF;  // 白
    constexpr uint16_t TEXT_SECONDARY = 0x8410;  // 中性灰
    constexpr uint16_t TEXT_SELECTED  = 0xFFFF;  // 白（
    constexpr uint16_t SELECT_BG      = 0x0320;  // 深绿
    constexpr uint16_t SEPARATOR      = 0x4A4A;  // 分隔线
}