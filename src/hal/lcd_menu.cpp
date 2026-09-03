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
    enum : uint8_t { ID_BRIGHT, ID_ROTATE, ID_SAMPLE, ID_UI, ID_RECORD_SWITCH, ID_RECORD, ID_THRESH_AUTO }; // 编辑项 ID
    enum : uint8_t { ACT_SYSTEM_INFO, ACT_CLEAR_DATA };               // 动作 ID

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
        { "数据", KIND_MENU,   4 },               // → 组 4
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
    // —— 组 4：离线数据(动态: 开关关闭时隐藏保存时间/阈值自动) ——
    static const uint8_t GROUP_OFFLINE = 4;

    static Item    gOfflineItems[5];   // 最多 5 项
    static uint8_t gOfflineCount = 0;

    static void syncOfflineItems() {
        bool on = (record_interval_s != 0);
        uint8_t n = 0;
        gOfflineItems[n++] = { "启用数据保存功能", KIND_VALUE,  ID_RECORD_SWITCH };
        if (on) {
            gOfflineItems[n++] = { "保存时间间隔", KIND_VALUE,  ID_RECORD };
            gOfflineItems[n++] = { "阈值自动开始/停止", KIND_VALUE,  ID_THRESH_AUTO };
        }
        gOfflineItems[n++] = { "清除所有数据", KIND_ACTION, ACT_CLEAR_DATA };
        gOfflineItems[n++] = { "返回",     KIND_BACK,   0 };
        gOfflineCount = n;
    }

    struct Group {
        const Item* items;
        uint8_t     count;
        const char* title;
    };

    static const Group GROUPS[] = {
        { ROOT_ITEMS,      5, "设置"     },   // 组 0
        { DISPLAY_ITEMS,   3, "显示设置"     },   // 组 1
        { SAMPLING_ITEMS,  2, "采样设置" },   // 组 2
        { INTERFACE_ITEMS, 2, "动画效果设置" },   // 组 3
        { gOfflineItems,   5, "离线数据" },   // 组 4 (条目数动态, 见 curCount)
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
    static int16_t  tempRecord       = 0;   // 记录时间选项索引 0-4
    static int16_t  tempRecordSwitch = 1;   // 离线数据功能开关 0/1
    static int16_t  tempThreshAuto   = 0;   // 阈值自动 0/1

    // 当前组
    static const Group& cur()      { return GROUPS[currentGroup]; }
    static uint8_t      curCount() {
        if (currentGroup == GROUP_OFFLINE) { syncOfflineItems(); return gOfflineCount; }
        return cur().count;
    }
    static const Item*  curItems() {
        if (currentGroup == GROUP_OFFLINE) { syncOfflineItems(); return gOfflineItems; }
        return cur().items;
    }

    // =====================================================================
    // 编辑项行为：显示 / 调整 / 保存 的规则集中在这里
    // =====================================================================

    // 记录时间选项(秒), 0 档由"功能开关"控制, 此处只提供分段间隔
    static const uint32_t recordChoices[]    = { 1, 5, 10, 30, 60 };
    static const uint8_t  recordChoiceCount  = 5;

    static uint8_t recordSecondsToIndex(uint32_t sec) {
        for (uint8_t i = 0; i < recordChoiceCount; i++) {
            if (sec == recordChoices[i]) return i;
        }
        return 0;   // 未知值回退到 1s (索引 0)
    }

    // 把 NVS 当前值读入临时值
    static void loadTemps() {
        tempBrightness = HAL::Sys_NVS_Valid("light", 50, 100, 1);
        tempRotation   = HAL::Sys_NVS_Valid("rotation", 3, 3);
        if (tempRotation != 1 && tempRotation != 3) tempRotation = 1;
        tempSampleMode = HAL::Sys_NVS_Valid("sample_mode", 0);
        if (tempSampleMode > 2) tempSampleMode = 0;
        tempUIEffects  = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0);
        tempRecord       = recordSecondsToIndex(HAL::Sys_NVS_ReadUInt("record_s", 1));
        tempRecordSwitch = (HAL::Sys_NVS_ReadUInt("record_s", 1) != 0) ? 1 : 0;
        tempThreshAuto   = HAL::Sys_NVS_Valid("thr_auto", 0, 1, 0);
    }

    // 显示某项的值（编辑中显示临时值，否则显示已存值）
    void GetValueStr(uint8_t index, char* buffer) {
        buffer[0] = '\0';
        if (index >= curCount()) return;
        const Item& it = curItems()[index];
        if (it.type != KIND_VALUE) return;

        const char* sampleNames[] = { "快速", "默认", "慢速" };
        uint8_t id = it.param;
        int16_t v;

        // 取值：编辑中 → 临时值；否则 → NVS
        if (currentMode == MODE_EDIT && index == selectedIndex) {
            switch (id) {
                case ID_BRIGHT:      v = tempBrightness; break;
                case ID_ROTATE:      v = tempRotation;   break;
                case ID_SAMPLE:      v = tempSampleMode; break;
                case ID_UI:          v = tempUIEffects;  break;
                case ID_RECORD_SWITCH: v = tempRecordSwitch; break;
                case ID_RECORD:      v = tempRecord;     break;
                case ID_THRESH_AUTO: v = tempThreshAuto; break;
                default:             v = 0;
            }
        } else {
            switch (id) {
                case ID_BRIGHT:      v = HAL::Sys_NVS_Valid("light", 50, 100, 1);  break;
                case ID_ROTATE:      v = HAL::Sys_NVS_Valid("rotation", 3, 3);     break;
                case ID_SAMPLE:      v = HAL::Sys_NVS_Valid("sample_mode", 0);     break;
                case ID_UI:          v = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0); break;
                case ID_RECORD_SWITCH: v = (HAL::Sys_NVS_ReadUInt("record_s", 1) != 0) ? 1 : 0; break;
                case ID_RECORD:      v = recordSecondsToIndex(HAL::Sys_NVS_ReadUInt("record_s", 1)); break;
                case ID_THRESH_AUTO: v = HAL::Sys_NVS_Valid("thr_auto", 0, 1, 0); break;
                default:             v = 0;
            }
        }

        // 格式化
        switch (id) {
            case ID_BRIGHT: sprintf(buffer, "%d%%", v); break;
            case ID_ROTATE: sprintf(buffer, "(%d)%s", v, v == 1 ? "方向下" : "方向上"); break;
            case ID_SAMPLE: if (v > 2) v = 0; sprintf(buffer, "%s", sampleNames[v]); break;
            case ID_UI:     sprintf(buffer, "%s", v ? "开启" : "关闭"); break;
            case ID_RECORD_SWITCH: sprintf(buffer, "%s", v ? "开" : "关"); break;
            case ID_RECORD: {
                if (v < 0 || v >= (int16_t)recordChoiceCount) v = 0;
                static const char* recNames[] = {"1s", "5s", "10s", "30s", "60s"};
                sprintf(buffer, "%s", recNames[v]);
                break;
            }
            case ID_THRESH_AUTO: sprintf(buffer, "%s", v ? "开" : "关"); break;
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
            case ID_RECORD_SWITCH:
                tempRecordSwitch = tempRecordSwitch ? 0 : 1;
                break;
            case ID_RECORD:
                tempRecord = (tempRecord + (delta > 0 ? 1 : (int16_t)(recordChoiceCount - 1))) % recordChoiceCount;
                break;
            case ID_THRESH_AUTO:
                tempThreshAuto = tempThreshAuto ? 0 : 1;
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
                case ID_RECORD_SWITCH:
                    // 开: 若当前为关闭(0)则恢复默认 1s; 关: 置 0
                    record_interval_s = tempRecordSwitch ? ((record_interval_s == 0) ? 1 : record_interval_s) : 0;
                    HAL::Sys_NVS_WriteUInt("record_s", record_interval_s);
                    break;
                case ID_RECORD:
                    record_interval_s = recordChoices[tempRecord];
                    HAL::Sys_NVS_WriteUInt("record_s", record_interval_s);
                    break;
                case ID_THRESH_AUTO:
                    HAL::Sys_NVS_Write("thr_auto", tempThreshAuto);
                    threshold_auto = tempThreshAuto;
                    break;
            }
        } else {
            // 取消：亮度预览已实时改变，恢复为已存值
            HAL::LCD_SetBrightness(HAL::Sys_NVS_Valid("light", 50, 100, 1));
        }
        currentMode = MODE_SELECT;
        if (selectedIndex >= curCount()) selectedIndex = (curCount() > 0) ? (curCount() - 1) : 0;
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
            case ACT_CLEAR_DATA:
                if (HAL::Storage_Erase()) {
                    HAL::ShowToast("清除");
                } else {
                    HAL::ShowToast("清除失败");
                }
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
        const Item& it = curItems()[selectedIndex];

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
        return (index < curCount()) ? curItems()[index].title : "";
    }

    bool ShouldShowValue(uint8_t index) {
        return (index < curCount()) && curItems()[index].type == KIND_VALUE;
    }

    bool IsSubmenu(uint8_t index) {
        return (index < curCount()) && curItems()[index].type == KIND_MENU;
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