#include "lcd_menu.h"

namespace MenuConfig {

    uint8_t selectedIndex = 0;
    bool isEditing = false;
    uint8_t editItem = 0;

    void Init() {
        selectedIndex = 0;
        isEditing = false;
    }

    void SelectNext() {
        if (!isEditing) {
            selectedIndex = (selectedIndex + 1) % menuItemCount;
        }
    }

    void SelectPrev() {
        if (!isEditing) {
            selectedIndex = (selectedIndex + menuItemCount - 1) % menuItemCount;
        }
    }

    void EnterEdit() {
        if (selectedIndex == 2) { // Exit
            nowApp = AppState::UI_MAIN;
            return;
        }
        isEditing = true;
        editItem = selectedIndex;
    }

    void ExitEdit() {
        isEditing = false;
    }

    void AdjustValue(int8_t delta) {
        switch (editItem) {
            case 0: { // Brightness
                int current = EEPROM.read(eeprom_light_addr);
                current += (delta > 0) ? 10 : -10;
                current = constrain(current, 10, 100);
                EEPROM.write(eeprom_light_addr, current);
                EEPROM.commit();
                HAL::LCD_SetBacklight(current);
                break;
            }
            case 1: { // Rotation: only 1 or 3
                int current = EEPROM.read(rotation_addr);
                if (current != 1 && current != 3) current = 1;
                current = (current == 1) ? 3 : 1;
                EEPROM.write(rotation_addr, current);
                EEPROM.commit();
                HAL::LCD_SetRotation(current);
                break;
            }
        }
    }

    void ConfirmSelection() {
        if (selectedIndex == 2) {
            nowApp = AppState::UI_MAIN;
        }
    }

    // --- 辅助函数 ---
    const char* GetTitle(uint8_t index) {
        static const char* titles[] = {"Brightness", "Rotation", "Exit"};
        return (index < menuItemCount) ? titles[index] : "";
    }

    void GetValueStr(uint8_t index, char* buffer) {
        buffer[0] = '\0';
        switch (index) {
            case 0:
                sprintf(buffer, "%d%%", EEPROM.read(eeprom_light_addr));
                break;
            case 1:
                sprintf(buffer, "%d", EEPROM.read(rotation_addr));
                break;
        }
    }

    bool ShouldShowValue(uint8_t index) {
        return (index < menuItemCount - 1); // 所有非 Exit 项都显示值（更通用）
    }

} // namespace MenuConfig