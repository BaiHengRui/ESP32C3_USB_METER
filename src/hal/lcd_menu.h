#pragma once

#include "hal.h"

namespace MenuConfig {

    // 菜单模式
    enum MenuMode {
        MODE_IDLE,      // 空闲（未选中任何项）
        MODE_SELECT,    // 选择导航
        MODE_EDIT       // 编辑值
    };

    // 状态（供 UI 与按键读取）
    extern MenuMode currentMode;
    extern uint8_t  selectedIndex;   // 当前组内选中项

    constexpr uint8_t menuVisibleCount = 4;  // 一屏可见的菜单项数量
    constexpr uint8_t menuCursorRow   = 2;   // 选中项固定在窗口中的行（0-based）

    // ---- 导航 ----
    void Menu_Init();               // 进入菜单时重置
    void Menu_Exit();               // 退出菜单（回主界面）
    void EnterSelectMode();         // 空闲 → 选择
    void ExitSelectMode();          // 返回上一级
    void EnterEditMode();           // “确认”键：进子菜单 / 编辑值 / 返回
    void ExitEditMode(bool save);   // 编辑 → 选择（保存或取消）
    void SelectNext();
    void SelectPrev();
    void AdjustValue(int8_t delta);

    // ---- 供 UI 绘制 ----
    uint8_t     GetItemCount();     // 当前组条目数
    uint8_t     GetWindowStart();   // 滚动窗口起始索引
    const char* GetGroupTitle();    // 当前组标题
    const char* GetTitle(uint8_t index);
    void        GetValueStr(uint8_t index, char* buffer);
    bool        ShouldShowValue(uint8_t index);
    bool        IsSubmenu(uint8_t index);

} // namespace MenuConfig

namespace MenuColors {
    constexpr uint16_t BACKGROUND     = 0x0000;  // 黑色
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF;  // 白色
    constexpr uint16_t TEXT_SECONDARY = 0x8410;  // 浅灰
    constexpr uint16_t TEXT_SELECTED  = 0xFFFF;  // 白色
    constexpr uint16_t SELECT_BG      = 0x0320;  // 深绿
    constexpr uint16_t SEPARATOR      = 0x4A4A;  // 分隔线灰
}