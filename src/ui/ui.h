#pragma once
#include "../hal/hal_lcd.h"
#include "../hal/hal.h"
#include "../hal/lcd_menu.h"

namespace UI
{
    void ShowMain();
    void System_Info();
    void WaveGraph();
    void Menu();
    void DrawToast();
    void TransitionTo(uint8_t oldApp, uint8_t newApp);
} // namespace UI
