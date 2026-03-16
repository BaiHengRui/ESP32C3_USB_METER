#include "lcd_menu.h"

namespace MenuConfig {
    MenuMode currentMode = MODE_IDLE;
    uint8_t selectedIndex = 0;
    uint8_t editItem = 0;

    int16_t tempBrightness = 50;
    int16_t tempRotation = 3;
    int16_t tempSampleMode = 0;

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
            nowApp = AppState::UI_MAIN;            // 直接退出菜单
            return;
        }
        editItem = selectedIndex;
        // 从 NVS 读取当前值作为临时值
        tempBrightness = HAL::Sys_NVS_Valid("light", 50, 100, 1);
        tempRotation = HAL::Sys_NVS_Valid("rotation", 3, 3);
        if (tempRotation != 1 && tempRotation != 3) tempRotation = 1;
        tempSampleMode = HAL::Sys_NVS_Valid("sample_mode", 0);
        if (tempSampleMode > 2) tempSampleMode = 0;

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
                    break;
            }
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
                tempBrightness += (delta > 0) ? 10 : -10;
                if (tempBrightness < 10) tempBrightness = 10;
                if (tempBrightness > 100) tempBrightness = 100;
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
        }
    }

    const char* GetTitle(uint8_t index) {
        static const char* titles[] = {"Brightness", "Rotation", "Sample Rate", "Exit"};
        return (index < menuItemCount) ? titles[index] : "";
    }

    void GetValueStr(uint8_t index, char* buffer) {
        buffer[0] = '\0';
        const char* mode_str[] = {"Fast", "Normal", "Slow"};

        if (currentMode == MODE_EDIT && index == editItem) {
            // 显示临时值
            switch (index) {
                case 0: sprintf(buffer, "%d%%", tempBrightness); break;
                case 1: sprintf(buffer, "%d", tempRotation); break;
                case 2: sprintf(buffer, "%s", mode_str[tempSampleMode]); break;
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
                    sprintf(buffer, "%d", v);
                    break;
                }
                case 2: {
                    uint8_t m = HAL::Sys_NVS_Valid("sample_mode", 0);
                    if (m > 2) m = 0;
                    sprintf(buffer, "%s", mode_str[m]);
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