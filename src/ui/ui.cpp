// This file is ui-transition.cpp
// only used for UI transition branch

#include "ui.h"
#include "../hal/hal_lcd.h"
#include "../assets/fonts/Font1_12.h"   //MiSans-Demibold(EN)
#include "../assets/fonts/Font1_14.h"   //OPPOSans-B-2(EN)
#include "../assets/fonts/Font1_18.h"   //OPPOSans-M-2(CN)
#include "../assets/fonts/Font1_45.h"   //Hun-DIN1451(EN)
#include "../assets/imgs/arrow_left.h"  //W:20px H:20px
#include "../assets/imgs/arrow_right.h" //W:20px H:20px

// 菜单首次进入标记 (模块作用域, 供 Menu() 和 TransitionTo 共用)
static bool menuFirstEntry = true;

// ============================================================
// 内部绘制辅助函数 — 仅绘制到 spr, 不管理 sprite 生命周期
// ============================================================

static void _DrawMainContent() {
    spr.setTextDatum(CC_DATUM);
    spr.fillScreen(0x0000);

    //-- 主信息显示 --
    spr.loadFont(Font1_45);

    spr.setCursor(1,5);
    spr.setTextColor(0x2EA6);
    spr.print(INA.voltage, INA.voltage < 10 ? 4 : 3);
    spr.setCursor(128,5);
    spr.print("V");

    spr.setCursor(1, 50);
    spr.setTextColor(0xFEE0);
    spr.print(INA.current, INA.current < 10 ? 4 : 3);
    spr.setCursor(128,50);
    spr.print("A");

    spr.setCursor(1, 95);
    spr.setTextColor(0xF8E8);
    spr.print(INA.power, INA.power < 10 ? 4 : 3);
    spr.setCursor(128,95);
    spr.print("W");

    spr.unloadFont();
    //-- 分割线 --
    spr.drawLine(159,0,159,135,0xAD55); //分割线
    spr.drawLine(160,0,160,135,0xAD55);

    //-- 辅助信息显示 --
    if (INA.charge_mAh <= 99999 || INA.energy_mWh <= 99999) {
        spr.loadFont(Font1_12);
        spr.setCursor(168,5);
        spr.setTextColor(0xFEA0);
        spr.print("mAh/mWh");
        spr.unloadFont();
        spr.loadFont(Font1_14);
        spr.setCursor(166,20);
        spr.setTextColor(0x07FF);
        spr.print("C: ");
        spr.print(INA.charge_mAh, 
            INA.charge_mAh >= 10000 ? 0 :
            INA.charge_mAh >= 1000  ? 1 :
            INA.charge_mAh >= 100   ? 2 :
            INA.charge_mAh >= 10    ? 3 : 4
        );
        spr.setCursor(166,40);
        spr.print("E: ");
        spr.print(INA.energy_mWh, 
            INA.energy_mWh >= 10000 ? 0 :
            INA.energy_mWh >= 1000  ? 1 :
            INA.energy_mWh >= 100   ? 2 :
            INA.energy_mWh >= 10    ? 3 : 4
        );
        spr.unloadFont();
    }
    else {
        spr.loadFont(Font1_12);
        spr.setCursor(180,5);
        spr.setTextColor(0xFEA0);
        spr.print("Ah/Wh");
        spr.unloadFont();
        spr.loadFont(Font1_14);
        spr.setCursor(166,20);
        spr.setTextColor(0x07FF);
        spr.print("C: ");
        spr.print(INA.charge_Ah, 
            INA.charge_Ah >= 10000 ? 0 :
            INA.charge_Ah >= 1000  ? 1 :
            INA.charge_Ah >= 100   ? 2 :
            INA.charge_Ah >= 10    ? 3 : 4
        );
        spr.setCursor(166,40);
        spr.print("E: ");
        spr.print(INA.energy_Wh, 
            INA.energy_Wh >= 10000 ? 0 :
            INA.energy_Wh >= 1000  ? 1 :
            INA.energy_Wh >= 100   ? 2 :
            INA.energy_Wh >= 10    ? 3 : 4
        );
        spr.unloadFont();
    }

    spr.loadFont(Font1_14);
    spr.setTextColor(0xFFFF);
    spr.setCursor(168,95);
    spr.print(INA.temperature, 2);
    spr.print(" ℃");
    spr.setCursor(168,115);
    // 显示阈值计时
    spr.print(HAL::Get_Threshold_Time());
    // 显示系统状态
    spr.setCursor(190,67);
    spr.print(HAL::Get_System_Status());
    spr.unloadFont();

    if(tft.getRotation() == 1){
        spr.pushImage(166,64,20,20, INA.current_direction ? arrow_right : arrow_left); //电流方向箭头 1方向
    }else{
        spr.pushImage(166,64,20,20, INA.current_direction ? arrow_left : arrow_right); //电流方向箭头 默认3方向
    }
}

// ============================================================
// 公共 UI 函数
// ============================================================

void UI::ShowMain(){
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    _DrawMainContent();
    UI::DrawToast();
    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

static bool _DrawWaveGraphContent() {
    // 返回 false 表示数据未就绪, 仅显示 "Sampling..."
    if (!graphDataInitialized) {
        spr.loadFont(Font1_12);
        spr.setTextColor(0xFFFF);
        spr.setTextDatum(CC_DATUM);
        spr.drawString("Sampling...", 120, 67);
        spr.unloadFont();
        return false;
    }

    // --- Constants ---
    const int graphHeight = 135;
    const int graphX = 0;
    const int graphY = 0;
    const int gridCols = 4;
    const int gridRows = 3;
    const uint16_t gridColor = 0x39E7;

    // --- Draw grid (corrected to stay within pixel bounds) ---
    for (int col = 0; col <= gridCols; col++) {
        int x = graphX + (col * (GRAPH_WIDTH - 1)) / gridCols;
        spr.drawLine(x, graphY, x, graphY + graphHeight - 1, gridColor);
    }
    for (int row = 0; row <= gridRows; row++) {
        int y = graphY + (row * (graphHeight - 1)) / gridRows;
        spr.drawLine(graphX, y, graphX + GRAPH_WIDTH - 1, y, gridColor);
    }
    // --- Safety guard: prevent division by zero in Y mapping ---
    if (vDisplayMax <= vDisplayMin) vDisplayMax = vDisplayMin + 0.1f;
    if (iDisplayMax <= iDisplayMin) iDisplayMax = iDisplayMin + 0.1f;

    // --- Draw voltage curve ---
    for (int i = 1; i < GRAPH_WIDTH; i++) {
        int x1 = graphX + i - 1;
        int x2 = graphX + i;
        float val1 = voltageBuffer[(graphIndex + i - 1) % GRAPH_WIDTH];
        float val2 = voltageBuffer[(graphIndex + i) % GRAPH_WIDTH];

        val1 = fmaxf(vDisplayMin, fminf(val1, vDisplayMax));
        val2 = fmaxf(vDisplayMin, fminf(val2, vDisplayMax));

        int y1 = graphY + graphHeight - 1 - (int)((val1 - vDisplayMin) * (graphHeight - 1) / (vDisplayMax - vDisplayMin));
        int y2 = graphY + graphHeight - 1 - (int)((val2 - vDisplayMin) * (graphHeight - 1) / (vDisplayMax - vDisplayMin));
        spr.drawLine(x1, y1, x2, y2, 0x2EA6);
    }
    // --- Draw current curve ---
    for (int i = 1; i < GRAPH_WIDTH; i++) {
        int x1 = graphX + i - 1;
        int x2 = graphX + i;
        float val1 = currentBuffer[(graphIndex + i - 1) % GRAPH_WIDTH];
        float val2 = currentBuffer[(graphIndex + i) % GRAPH_WIDTH];

        val1 = fmaxf(iDisplayMin, fminf(val1, iDisplayMax));
        val2 = fmaxf(iDisplayMin, fminf(val2, iDisplayMax));

        int y1 = graphY + graphHeight - 1 - (int)((val1 - iDisplayMin) * (graphHeight - 1) / (iDisplayMax - iDisplayMin));
        int y2 = graphY + graphHeight - 1 - (int)((val2 - iDisplayMin) * (graphHeight - 1) / (iDisplayMax - iDisplayMin));
        spr.drawLine(x1, y1, x2, y2, 0xFEE0);
    }

    // --- Right-side labels with adaptive decimal places ---
    spr.loadFont(Font1_12);
    spr.setTextDatum(CL_DATUM);
    char buf[16];

    // Voltage info
    spr.setTextColor(0x2EA6);
    spr.drawString("VBUS :", 182, 45);

    // Now V (use frozen if paused)
    float displayVoltage = graphPaused ? frozenVoltage : INA.voltage;
    if (displayVoltage >= 10.0f) {
        sprintf(buf, "Now:%.1f", displayVoltage);
    } else {
        sprintf(buf, "Now:%.2f", displayVoltage);
    }
    spr.drawString(buf, 182, 60);

    // Max V
    if (vHistoryMax >= 10.0f) {
        sprintf(buf, "Max:%.1f", vHistoryMax);
    } else {
        sprintf(buf, "Max:%.2f", vHistoryMax);
    }
    spr.drawString(buf, 182, 75);

    // Current info
    spr.setTextColor(0xFEE0);
    spr.drawString("IBUS :", 182, 95);

    // Now I (use frozen if paused)
    float displayCurrent = graphPaused ? frozenCurrent : INA.current;
    if (displayCurrent >= 10.0f) {
        sprintf(buf, "Now:%.1f", displayCurrent);
    } else {
        sprintf(buf, "Now:%.2f", displayCurrent);
    }
    spr.drawString(buf, 182, 110);

    // Max I
    if (iHistoryMax >= 10.0f) {
        sprintf(buf, "Max:%.1f", iHistoryMax);
    } else {
        sprintf(buf, "Max:%.2f", iHistoryMax);
    }
    spr.drawString(buf, 182, 125);

    // Voltage scale bottom-left = vDisplayMax
    spr.setTextColor(0xD69A);
    sprintf(buf, "%.2fV/%.2fA", vDisplayMax, iDisplayMax);
    spr.drawString(buf, 5, 10);
    // sprintf(buf, "%.1f", (vHistoryMax - vDisplayMin)/gridRows);
    // spr.drawString("V/d:" + String(buf), 130, 10);

    // Voltage scale bottom-left = vDisplayMin
    spr.setCursor(10,120);
    sprintf(buf, "%.2fV/%.2fA", vDisplayMin, iDisplayMin);
    spr.drawString(buf, 5, 125);
    // sprintf(buf, "%.1f", (iHistoryMax - iDisplayMax)/gridRows);
    // spr.drawString("A/d:" + String(buf), 130, 125);
    
    // s/div indicator
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(0x8410); // Gray
    spr.drawString("1.35s/d", 202, 15);

    // PAUSED indicator
    if (graphPaused) {
        spr.setTextDatum(CC_DATUM);
        spr.setTextColor(0xF800); // Red
        spr.drawString("PAUSED", 90, 67);
        // spr.drawString("OFF", 222, 30);
        spr.drawString("OFF", 195, 30);
    }else{
        spr.setTextDatum(CC_DATUM);
        spr.setTextColor(0x07FF); // Cyan
        // spr.drawString("ON", 222, 30);
        spr.drawString("ON", 192, 30);
    }

    spr.unloadFont();
    return true;
}

void UI::WaveGraph() {
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.fillScreen(0x0000);
    _DrawWaveGraphContent();
    UI::DrawToast();
    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

static void _DrawSystemInfoContent() {
    spr.setTextDatum(CC_DATUM);
    spr.fillScreen(0x0000);
    spr.loadFont(Font1_14);
    spr.setTextColor(0xFFFF);
    spr.setCursor(0,2);
    spr.print("  SN:");
    spr.println(String(SNID, HEX));
    spr.print("  INA:");
    spr.println(INA.device_id, HEX);
    spr.print("  SW:");
    spr.println(SOFTWARE_VERSION);
    spr.print("  HW:");
    spr.println(HARDWARE_VERSION);
    spr.printf("  Start:%d ms\n", startTime);
    spr.print("  FPS:");
    spr.println(HAL::Get_FPS(),2);
    spr.print("  Runtime:");
    spr.println(HAL::Get_System_RunTime(esp_timer_get_time()));
    spr.unloadFont();
}

void UI::System_Info(){
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    _DrawSystemInfoContent();
    UI::DrawToast();
    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

void RenderMenuItem(uint8_t index, int yPos) {
    bool isSelected = (MenuConfig::currentMode != MenuConfig::MODE_IDLE) && (index == MenuConfig::selectedIndex);
    uint16_t textColor = isSelected ? MenuColors::TEXT_SELECTED : MenuColors::TEXT_PRIMARY;
    uint16_t bgColor = isSelected ? MenuColors::SELECT_BG : MenuColors::BACKGROUND;

    spr.fillRect(0, yPos, 240, 24, bgColor);
    spr.setTextColor(textColor);
    spr.setCursor(10, yPos + 6);
    spr.print(MenuConfig::GetTitle(index));

    if (MenuConfig::ShouldShowValue(index)) {
        char valueStr[16];
        MenuConfig::GetValueStr(index, valueStr);
        spr.setTextColor(MenuColors::TEXT_SECONDARY);
        spr.setCursor(160, yPos + 6);
        spr.print(valueStr);

        // 编辑模式下，当前编辑项显示星号
        if (MenuConfig::currentMode == MenuConfig::MODE_EDIT && MenuConfig::editItem == index) {
            spr.setTextColor(TFT_RED);
            spr.setCursor(220, yPos + 6);
            spr.print("*");
        }
    }
}

static void _DrawMenuContent() {
    spr.fillScreen(MenuColors::BACKGROUND);

    // === Header ===
    spr.loadFont(Font1_18);
    spr.setTextColor(MenuColors::TEXT_PRIMARY);
    spr.setCursor(10, 2);
    spr.print("设置");
    spr.unloadFont();

    // === Separator ===
    spr.drawLine(0, 24, 239, 24, MenuColors::SEPARATOR);

    // === Menu Items ===
    spr.loadFont(Font1_14);
    spr.setCursor(170, 10);
    spr.print(SOFTWARE_VERSION);
    const uint8_t startY = 28;
    const uint8_t itemHeight = 24;
    for (uint8_t i = 0; i < MenuConfig::menuItemCount; i++) {
        RenderMenuItem(i, startY + i * itemHeight);
    }
    spr.unloadFont();

    // === Bottom Hint ===  
    spr.loadFont(Font1_12);
    spr.setTextColor(MenuColors::TEXT_SECONDARY);
    spr.setCursor(5, 122);

    // 根据模式显示不同的提示
    switch (MenuConfig::currentMode) {
        case MenuConfig::MODE_IDLE:
            spr.print("L/SW0:Next | SW1:Select");          // L = Long
            break;
        case MenuConfig::MODE_SELECT:
            spr.print("SW0↑ SW1↓ | L0:Back | L1:Select");
            break;
        case MenuConfig::MODE_EDIT:
            spr.print("SW0+ SW1- | L0:Cancel | L1:Save");
            break;
    }
    spr.unloadFont();
}

void UI::Menu() {
    if (menuFirstEntry) {
        menuFirstEntry = false;
        MenuConfig::Menu_Init();  // 进入菜单时初始化为空闲模式
    }

    spr.createSprite(240, 135);
    _DrawMenuContent();
    UI::DrawToast();
    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

// ============================================================
// 页面绘制调度器 — 将指定页面内容绘制到 spr
// ============================================================
static void _DrawPageContent(uint8_t app) {
    switch (app) {
        case AppState::MAIN:
            _DrawMainContent();
            break;
        case AppState::WAVEGRAPH:
            spr.fillScreen(0x0000);
            _DrawWaveGraphContent();
            break;
        case AppState::MENU:
            _DrawMenuContent();
            break;
        case AppState::SYSTEM_INFO:
            _DrawSystemInfoContent();
            break;
        default:
            _DrawMainContent();
            break;
    }
}

// ============================================================
// 页面切换过渡动画 — 旧页左滑出 + 新页右滑入, 双页同时可见
// Famers: 8, Delay: 5ms Total: 40ms = FreeRTOS Task Delay
// ============================================================
void UI::TransitionTo(uint8_t oldApp, uint8_t newApp) {
    const int steps = 8;              // 动画帧数
    const int stepDelay = 5;          // 每帧间隔 (ms)
    const int displayWidth = TFT_HEIGHT;  // 240

    TFT_eSprite sprOld(&tft);
    sprOld.createSprite(TFT_HEIGHT, TFT_WIDTH);
    sprOld.setTextDatum(CC_DATUM);

    // --- 1. 渲染旧页面到 spr, 再拷贝到 sprOld ---
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setTextDatum(CC_DATUM);
    _DrawPageContent(oldApp);
    // 旧页面也画上 toast (如果有)
    UI::DrawToast();
    spr.pushToSprite(&sprOld, 0, 0);
    spr.deleteSprite();

    // --- 2. 渲染新页面到 spr ---
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setTextDatum(CC_DATUM);

    // 如果新页面是菜单且首次进入, 初始化菜单状态
    if (newApp == AppState::MENU && menuFirstEntry) {
        menuFirstEntry = false;
        MenuConfig::Menu_Init();
    }

    _DrawPageContent(newApp);
    UI::DrawToast();

    // --- 3. 双页联动滑动动画 ---
    // offset 从 0 → displayWidth:
    //   旧页面: 显示列 [offset, displayWidth-1] → 逐渐左移消失
    //   新页面: 显示列 [0, offset-1]       → 从右侧逐渐滑入
    for (int i = 0; i <= steps; i++) {
        int offset = (displayWidth * i) / steps;

        if (offset < displayWidth) {
            // 旧页面: 推到屏幕左侧 (左半部分逐渐被裁剪)
            sprOld.pushSprite(0, 0, offset, 0, displayWidth - offset, TFT_WIDTH);
        }
        if (offset > 0) {
            // 新页面: 推到屏幕右侧 (从右边缘滑入)
            spr.pushSprite(displayWidth - offset, 0, 0, 0, offset, TFT_WIDTH);
        }

        delay(stepDelay);
    }

    spr.deleteSprite();
    sprOld.deleteSprite();
}

// Toast 通知绘制
void UI::DrawToast() {
    if (!HAL::IsToastActive()) return;

    spr.loadFont(Font1_14);
    spr.setTextDatum(MC_DATUM);

    int textW = spr.textWidth(toastMessage);
    int boxW = textW + 24;
    int boxH = 26;
    int boxX = (spr.width() - boxW) / 2;
    int boxY = spr.height() - boxH - 8;

    spr.fillRoundRect(boxX, boxY, boxW, boxH, 8, 0x3186);
    spr.drawRoundRect(boxX, boxY, boxW, boxH, 8, 0xCE59);
    spr.setTextColor(0xFFFF);
    spr.drawString(toastMessage, spr.width() / 2, boxY + boxH / 2);

    spr.unloadFont();
}
