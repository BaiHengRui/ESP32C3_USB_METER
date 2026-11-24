#pragma once
#include "../hal/hal_lcd.h"
#include "../hal/hal.h"
#include "../hal/lcd_menu.h"

namespace UI
{
    void ShowMain();
    void WaveGraph();
    void PD_Data();
    void EMark_Data();
    void Menu();
    void Storage_Data();
    void System_Info();
    void DrawToast();
    void TransitionTo(uint8_t oldApp, uint8_t newApp);
} // namespace UI
