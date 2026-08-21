#include "lcd_menu.h"

namespace MenuConfig {

    // =====================================================================
    // 菜单定义区 —— 增删菜单项/子菜单，只需改这一块
    //
    // 每个“组”是一个 Item 数组，组在 GROUPS 表中的位置就是组号（0=主菜单）。
    // Item = { 标题, 类型, 参数 }
    //   类型: KIND_MENU   = 进入子菜单（参数 = 组号）
    //         KIND_VALUE  = 可编辑项（参数 = 编辑项 ID，见下方 ID_*）
    //         KIND_BACK   = 返回上一级
    //         KIND_ACTION = 执行动作（参数 = 动作 ID，见下方 ACT_*）
    // =====================================================================

    enum : uint8_t { KIND_MENU, KIND_VALUE, KIND_BACK, KIND_ACTION }; // 菜单项类型
    enum : uint8_t { ID_BRIGHT, ID_ROTATE, ID_SAMPLE, ID_UI };        // 编辑项 ID
    enum : uint8_t { ACT_SYSTEM_INFO };                               // 动作 ID

    struct Item {
        const char* title;
        uint8_t     type;   // KIND_MENU / KIND_VALUE / KIND_BACK / KIND_ACTION
        uint8_t     param;  // KIND_MENU→组号; KIND_VALUE→编辑项ID; KIND_ACTION→动作ID; KIND_BACK→忽略
    };

    // —— 组 0：主菜单 ——
    static const Item ROOT_ITEMS[] = {
        { "显示",     KIND_MENU,   1 },               // → 组 1
        { "采样",     KIND_MENU,   2 },               // → 组 2
        { "动效",     KIND_MENU,   3 },               // → 组 3
        { "返回",     KIND_BACK,   0 },
    };
    // —— 组 1：显示 ——
    static const Item DISPLAY_ITEMS[] = {
        { "屏幕亮度", KIND_VALUE, ID_BRIGHT },
        { "屏幕方向", KIND_VALUE, ID_ROTATE },
        { "返回",     KIND_BACK,  0 },
    };
    // —— 组 2：采样 ——
    static const Item SAMPLING_ITEMS[] = {
        { "采样速度", KIND_VALUE, ID_SAMPLE },
        { "返回",     KIND_BACK,  0 },
    };
    // —— 组 3：动效 ——
    static const Item INTERFACE_ITEMS[] = {
        { "动画效果", KIND_VALUE, ID_UI },
        { "返回",     KIND_BACK,  0 },
    };

    struct Group {
        const Item* items;
        uint8_t     count;
        const char* title;
    };

    static const Group GROUPS[] = {
        { ROOT_ITEMS,      4, "设置"     },   // 组 0 / 5个子项
        { DISPLAY_ITEMS,   3, "显示"     },   // 组 1
        { SAMPLING_ITEMS,  2, "采样速度" },   // 组 2
        { INTERFACE_ITEMS, 2, "动画效果" },   // 组 3
    };

    // =====================================================================
    // 状态
    // =====================================================================
    MenuMode currentMode  = MODE_IDLE;   // 对外（UI/按键读取）
    uint8_t  selectedIndex = 0;          // 对外

    static uint8_t  currentGroup  = 0;   // 当前组号（0=主菜单）
    static uint8_t  editItem      = 0;   // 正在编辑的项 ID
    static int16_t  tempBrightness = 50;
    static int16_t  tempRotation   = 3;
    static int16_t  tempSampleMode = 0;
    static int16_t  tempUIEffects  = 1;

    // 当前组
    static const Group& cur()      { return GROUPS[currentGroup]; }
    static uint8_t      curCount() { return cur().count; }

    // =====================================================================
    // 编辑项行为：显示 / 调整 / 保存 的规则集中在这里
    // =====================================================================

    // 把 NVS 当前值读入临时值
    static void loadTemps() {
        tempBrightness = HAL::Sys_NVS_Valid("light", 50, 100, 1);
        tempRotation   = HAL::Sys_NVS_Valid("rotation", 3, 3);
        if (tempRotation != 1 && tempRotation != 3) tempRotation = 1;
        tempSampleMode = HAL::Sys_NVS_Valid("sample_mode", 0);
        if (tempSampleMode > 2) tempSampleMode = 0;
        tempUIEffects  = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0);
    }

    // 显示某项的值（编辑中显示临时值，否则显示已存值）
    void GetValueStr(uint8_t index, char* buffer) {
        buffer[0] = '\0';
        if (index >= curCount()) return;
        const Item& it = cur().items[index];
        if (it.type != KIND_VALUE) return;

        const char* sampleNames[] = { "快速", "默认", "慢速" };
        uint8_t id = it.param;
        int16_t v;

        // 取值：编辑中 → 临时值；否则 → NVS
        if (currentMode == MODE_EDIT && index == selectedIndex) {
            switch (id) {
                case ID_BRIGHT: v = tempBrightness; break;
                case ID_ROTATE: v = tempRotation;   break;
                case ID_SAMPLE: v = tempSampleMode; break;
                case ID_UI:     v = tempUIEffects;  break;
                default:        v = 0;
            }
        } else {
            switch (id) {
                case ID_BRIGHT: v = HAL::Sys_NVS_Valid("light", 50, 100, 1);  break;
                case ID_ROTATE: v = HAL::Sys_NVS_Valid("rotation", 3, 3);     break;
                case ID_SAMPLE: v = HAL::Sys_NVS_Valid("sample_mode", 0);     break;
                case ID_UI:     v = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0); break;
                default:        v = 0;
            }
        }

        // 格式化
        switch (id) {
            case ID_BRIGHT: sprintf(buffer, "%d%%", v); break;
            case ID_ROTATE: sprintf(buffer, "(%d)%s", v, v == 1 ? "方向下" : "方向上"); break;
            case ID_SAMPLE: if (v > 2) v = 0; sprintf(buffer, "%s", sampleNames[v]); break;
            case ID_UI:     sprintf(buffer, "%s", v ? "开启" : "关闭"); break;
        }
    }

    // 编辑模式下调值
    void AdjustValue(int8_t delta) {
        if (currentMode != MODE_EDIT) return;

        switch (editItem) {
            case ID_BRIGHT:
                tempBrightness += (delta > 0) ? 5 : -5;
                if (tempBrightness < 5)   tempBrightness = 5;
                if (tempBrightness > 100) tempBrightness = 100;
                HAL::LCD_SetBrightness(tempBrightness);   // 实时预览
                break;
            case ID_ROTATE:
                tempRotation = (tempRotation == 1) ? 3 : 1;
                break;
            case ID_SAMPLE:
                tempSampleMode = (tempSampleMode + (delta > 0 ? 1 : 2)) % 3;
                break;
            case ID_UI:
                tempUIEffects = tempUIEffects ? 0 : 1;
                break;
        }
    }

    // 保存或取消编辑
    void ExitEditMode(bool save) {
        if (save) {
            switch (editItem) {
                case ID_BRIGHT:
                    HAL::Sys_NVS_Write("light", tempBrightness);
                    HAL::LCD_SetBrightness(tempBrightness);
                    break;
                case ID_ROTATE:
                    HAL::Sys_NVS_Write("rotation", tempRotation);
                    HAL::LCD_SetRotation(tempRotation);
                    break;
                case ID_SAMPLE:
                    HAL::Sys_NVS_Write("sample_mode", tempSampleMode);
                    HAL::INA22x_SetConfig(tempSampleMode);
                    sample_mode = tempSampleMode;
                    break;
                case ID_UI:
                    HAL::Sys_NVS_Write("ui_effects", tempUIEffects);
                    ui_effects = tempUIEffects;
                    break;
            }
        } else {
            // 取消：亮度预览已实时改变，恢复为已存值
            HAL::LCD_SetBrightness(HAL::Sys_NVS_Valid("light", 50, 100, 1));
        }
        currentMode = MODE_SELECT;
    }

    // =====================================================================
    // 动作项行为：点击确认后立即执行（无值、无子项）
    // =====================================================================
    static void runAction(uint8_t act) {
        switch (act) {
            case ACT_SYSTEM_INFO:
                Menu_Init();                    // 重置菜单状态
                nowApp = AppState::SYSTEM_INFO; // 跳转到系统信息页
                break;
        }
    }

    // =====================================================================
    // 导航状态机（IDLE / SELECT / EDIT 三个模式）
    // =====================================================================

    void Menu_Init() {
        currentMode  = MODE_IDLE;
        currentGroup = 0;
        selectedIndex = 0;
        editItem      = 0;
    }

    void Menu_Exit() {
        Menu_Init();
        nowApp = AppState::MAIN;
    }

    void EnterSelectMode() {
        currentMode  = MODE_SELECT;
        currentGroup = 0;
        selectedIndex = 0;
    }

    void ExitSelectMode() {
        if (currentGroup != 0) {      // 子菜单 → 主菜单
            currentGroup = 0;
            selectedIndex = 0;
            currentMode = MODE_SELECT;
        } else {                       // 主菜单 → 空闲
            currentMode = MODE_IDLE;
        }
    }

    void SelectNext() {
        if (currentMode == MODE_SELECT && curCount() > 0)
            selectedIndex = (selectedIndex + 1) % curCount();
    }

    void SelectPrev() {
        if (currentMode == MODE_SELECT && curCount() > 0)
            selectedIndex = (selectedIndex + curCount() - 1) % curCount();
    }

    // “确认”键：根据当前项类型，进入子菜单 / 编辑 / 返回
    void EnterEditMode() {
        if (selectedIndex >= curCount()) return;
        const Item& it = cur().items[selectedIndex];

        if (it.type == KIND_MENU) {            // 进入子菜单
            currentGroup = it.param;
            selectedIndex = 0;
            currentMode = MODE_SELECT;
        } else if (it.type == KIND_BACK) {     // 返回
            if (currentGroup != 0) {
                currentGroup = 0;
                selectedIndex = 0;
                currentMode = MODE_SELECT;
            } else {
                Menu_Exit();                   // 主菜单的“返回”= 退出菜单
            }
        } else if (it.type == KIND_ACTION) {   // 执行动作
            runAction(it.param);
        } else {                               // KIND_VALUE：进入编辑
            editItem = it.param;
            loadTemps();
            currentMode = MODE_EDIT;
        }
    }

    // =====================================================================
    // 查询接口（UI 绘制用）
    // =====================================================================
    uint8_t GetItemCount() { return curCount(); }

    const char* GetGroupTitle() { return cur().title; }

    const char* GetTitle(uint8_t index) {
        return (index < curCount()) ? cur().items[index].title : "";
    }

    bool ShouldShowValue(uint8_t index) {
        return (index < curCount()) && cur().items[index].type == KIND_VALUE;
    }

    bool IsSubmenu(uint8_t index) {
        return (index < curCount()) && cur().items[index].type == KIND_MENU;
    }

    // 滚动窗口起始索引（选中项尽量固定在 menuCursorRow 行）
    uint8_t GetWindowStart() {
        uint8_t count = curCount();
        if (menuVisibleCount >= count) return 0;

        int16_t start = (int16_t)selectedIndex - (int16_t)menuCursorRow;
        if (start < 0) start = 0;

        int16_t maxStart = (int16_t)count - (int16_t)menuVisibleCount;
        if (start > maxStart) start = maxStart;
        return (uint8_t)start;
    }

} // namespace MenuConfig