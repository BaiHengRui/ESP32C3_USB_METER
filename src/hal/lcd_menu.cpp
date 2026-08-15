#include "lcd_menu.h"

namespace MenuConfig {
    MenuMode currentMode = MODE_IDLE;
    uint8_t selectedIndex = 0;
    uint8_t editItem = 0;

    int16_t tempBrightness = 50;
    int16_t tempRotation = 3;
    int16_t tempSampleMode = 0;
    int16_t tempUIEffects = 1;

    void Menu_Init() {
        currentMode = MODE_IDLE;
        selectedIndex = 0;      // 值保留但不显示高亮
    }

    void EnterSelectMode() {
        currentMode = MODE_SELECT;
        selectedIndex = 0;       // 默认选中第一项
    }

    void ExitSelectMode() {
        currentMode = MODE_IDLE;
        // 保持 selectedIndex 不变，但不再高亮
    }

    void EnterEditMode() {
        if (selectedIndex == menuItemCount - 1) { // Exit 项
            nowApp = AppState::MAIN;            // 直接退出菜单
            return;
        }
        editItem = selectedIndex;
        // 从 NVS 读取当前值作为临时值
        tempBrightness = HAL::Sys_NVS_Valid("light", 50, 100, 1);
        tempRotation = HAL::Sys_NVS_Valid("rotation", 3, 3);
        if (tempRotation != 1 && tempRotation != 3) tempRotation = 1;
        tempSampleMode = HAL::Sys_NVS_Valid("sample_mode", 0);
        if (tempSampleMode > 2) tempSampleMode = 0;
        tempUIEffects = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0);

        currentMode = MODE_EDIT;
    }

    void ExitEditMode(bool save) {
        if (save) {
            // 写入 NVS 并应用
            switch (editItem) {
                case 0: // Brightness
                    HAL::Sys_NVS_Write("light", tempBrightness);
                    HAL::LCD_SetBrightness(tempBrightness);
                    break;
                case 1: // Rotation
                    HAL::Sys_NVS_Write("rotation", tempRotation);
                    HAL::LCD_SetRotation(tempRotation);
                    break;
                case 2: // Sample Rate
                    HAL::Sys_NVS_Write("sample_mode", tempSampleMode);
                    HAL::INA22x_SetConfig(tempSampleMode);
                    sample_mode = tempSampleMode; // 同步全局变量
                    break;
                case 3: // UI Effects
                    HAL::Sys_NVS_Write("ui_effects", tempUIEffects);
                    ui_effects = tempUIEffects;   // 同步全局变量
                    break;
            }
        } else {
            // 取消保存：编辑期间亮度已被实时预览修改，恢复为 NVS 已保存值
            HAL::LCD_SetBrightness(HAL::Sys_NVS_Valid("light", 50, 100, 1));
        }
        // 无论是否保存，都回到选择模式
        currentMode = MODE_SELECT;
        // editItem 可保留，但不再使用
    }

    void SelectNext() {
        if (currentMode == MODE_SELECT) {
            selectedIndex = (selectedIndex + 1) % menuItemCount;
        }
    }

    void SelectPrev() {
        if (currentMode == MODE_SELECT) {
            selectedIndex = (selectedIndex + menuItemCount - 1) % menuItemCount;
        }
    }

    void AdjustValue(int8_t delta) {
        if (currentMode != MODE_EDIT) return;

        switch (editItem) {
            case 0: // Brightness
                tempBrightness += (delta > 0) ? 5 : -5;
                if (tempBrightness < 5) tempBrightness = 5;
                if (tempBrightness > 100) tempBrightness = 100;
                HAL::LCD_SetBrightness(tempBrightness); // 实时预览
                break;
            case 1: // Rotation (只能 1 或 3)
                // 切换
                tempRotation = (tempRotation == 1) ? 3 : 1;
                break;
            case 2: // Sample Mode (0,1,2 循环)
                if (delta > 0) {
                    tempSampleMode = (tempSampleMode + 1) % 3;
                } else {
                    tempSampleMode = (tempSampleMode + 2) % 3; // 减一等效
                }
                break;
            case 3: // UI Effects (0=Off, 1=On)
                tempUIEffects = tempUIEffects ? 0 : 1;
                break;
        }
    }

    // 计算可见窗口的起始条目索引：
    // 选中项尽量固定在 menuCursorRow 行，窗口随选中项上下滚动；
    // 顶部未到光标行时贴顶，底部不足一屏时贴底。
    uint8_t GetWindowStart() {
        if (menuVisibleCount >= menuItemCount) return 0;

        int16_t start = (int16_t)selectedIndex - (int16_t)menuCursorRow;
        if (start < 0) start = 0;

        int16_t maxStart = (int16_t)menuItemCount - (int16_t)menuVisibleCount;
        if (start > maxStart) start = maxStart;
        return (uint8_t)start;
    }

    const char* GetTitle(uint8_t index) {
        static const char* titles[] = {"Brightness", "Rotation", "Sample Rate", "UI Effects", "Exit Main(Back)"};
        return (index < menuItemCount) ? titles[index] : "";
    }

    void GetValueStr(uint8_t index, char* buffer) {
        buffer[0] = '\0';
        const char* mode_str[] = {"Fast", "Normal", "Slow"};

        if (currentMode == MODE_EDIT && index == editItem) {
            // 显示临时值
            switch (index) {
                case 0: sprintf(buffer, "%d%%", tempBrightness); break;
                case 1: sprintf(buffer, "(%d)%s", tempRotation, tempRotation == 1 ? "Down" : "UP"); break;
                case 2: sprintf(buffer, "%s", mode_str[tempSampleMode]); break;
                case 3: sprintf(buffer, "%s", tempUIEffects ? "On" : "Off"); break;
                default: break;
            }
        } else {
            // 显示实际存储值
            switch (index) {
                case 0: {
                    uint8_t v = HAL::Sys_NVS_Valid("light", 50, 100, 1);
                    sprintf(buffer, "%d%%", v);
                    break;
                }
                case 1: {
                    uint8_t v = HAL::Sys_NVS_Valid("rotation", 3, 3);
                    sprintf(buffer, "(%d)%s", v, v == 1 ? "Down" : "UP");
                    break;
                }
                case 2: {
                    uint8_t m = HAL::Sys_NVS_Valid("sample_mode", 0);
                    if (m > 2) m = 0;
                    sprintf(buffer, "%s", mode_str[m]);
                    break;
                }
                case 3: {
                    uint8_t v = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0);
                    sprintf(buffer, "%s", v ? "On" : "Off");
                    break;
                }
                default: break;
            }
        }
    }

    bool ShouldShowValue(uint8_t index) {
        return (index < menuItemCount - 1); // Exit 不显示值
    }

} // namespace MenuConfig