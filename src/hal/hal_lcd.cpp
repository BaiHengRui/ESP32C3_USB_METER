// 不建议超过64MHz的频率

#include "TFT_eSPI.h"
#include "hal.h"
#include "lcd_menu.h"
#include "../assets/fonts/Font1_12.h"   //MiSans-Demibold(EN)
#include "../assets/fonts/Font1_14.h"   //OPPOSans-B-2(EN)
#include "../assets/fonts/Font1_18.h"   //OPPOSans-M-2(CN)
#include "../assets/fonts/Font1_45.h"   //Hun-DIN1451(EN)
#include "../assets/imgs/arrow_left.h"  //W:20px H:20px
#include "../assets/imgs/arrow_right.h" //W:20px H:20px

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH,TFT_HEIGHT); //WIDTH, HEIGHT
TFT_eSprite spr = TFT_eSprite(&tft);

uint8_t defaultBrightness = 50; //默认亮度
uint8_t defaultRotation = 3;   //默认旋转
unsigned long lastFPSTime = 0;
unsigned int frameCount = 0;
float currentFPS = 0;

constexpr int GRAPH_WIDTH = 180; // 3/4 of 240
float voltageBuffer[GRAPH_WIDTH] = {0};
float currentBuffer[GRAPH_WIDTH] = {0};
int graphIndex = 0;
bool graphDataInitialized = false;
float vDisplayMin = 0.0f, vDisplayMax = 5.0f, vHistoryMax = 0.0f; // initial guess
float iDisplayMin = 0.0f, iDisplayMax = 2.0f, iHistoryMax = 0.0f;
bool graphRangeInitialized = false;
bool graphPaused = false;


void HAL::LCD_Init(){
    analogWrite(LCD_BL_PIN,0);
    spr.init();
    // spr.invertDisplay(1); //根据屏幕实际情况选择是否反色显示，去掉优化约5ms
    tft.setRotation(HAL::Sys_NVS_Valid("rotation", defaultRotation, 3));
    // tft.setRotation(3); //只能在tft对象操作。0-3 1/3观看为横屏
    spr.setColorDepth(16); //设置颜色深度为16位
    HAL::LCD_SetBrightness(HAL::Sys_NVS_Valid("light", defaultBrightness, 100, 1));
    HAL::LOG_INFO("LCD Initialized.");
    startTime = millis();
}

void HAL::LCD_SetBrightness(uint8_t brightness){
    brightness = constrain(brightness,1,100);//限制大小
    int light_pwm = 255 - ((100 - brightness) * 1.5);
    analogWrite(LCD_BL_PIN,light_pwm);
}
 
void HAL::LCD_SetRotation(uint8_t rotation){
    rotation = constrain(rotation,0,3);
    tft.setRotation(rotation);
}

void HAL::LCD_SetTextColor(uint16_t color){
    spr.setTextColor(color);
}

void HAL::LCD_Refresh_Screen(uint32_t bgcolor){
    spr.fillScreen(bgcolor);
}

float HAL::Get_FPS(){
    frameCount++;
    unsigned long currentTime = millis();
    // 每 1000ms (1秒) 计算一次帧率
    if (currentTime - lastFPSTime >= 1000) 
    {
        currentFPS = frameCount * 1000.0 / (currentTime - lastFPSTime);
        lastFPSTime = currentTime;
        frameCount = 0;
    }
    return currentFPS;
}

void HAL::UI_ShowMain(){
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setTextDatum(CC_DATUM); //设置文字对齐方式为居中
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
    spr.print(HAL::Get_System_RunTime(millis()));
    // System Alert Display
    spr.setCursor(190,67);
    spr.print(HAL::Get_System_Status());
    spr.unloadFont();
    spr.pushImage(166,64,20,20, INA.current_direction ? arrow_left : arrow_right); //电流方向箭头

    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

void HAL::UI_WaveGraph() {
    // Static state variables (persist between calls)
    static bool wasPaused = false;          // Previous pause state
    static float frozenVoltage = 0.0f;      // Frozen V when paused
    static float frozenCurrent = 0.0f;      // Frozen I when paused

    float newVoltage = INA.voltage;
    float newCurrent = INA.current;

    // --- Detect rising edge of pause: freeze current values ---
    if (!wasPaused && graphPaused) {
        frozenVoltage = newVoltage;
        frozenCurrent = newCurrent;
    }
    wasPaused = graphPaused; // Update state memory

    // --- Sampling (only when NOT paused) ---
    if (!graphPaused) {
        voltageBuffer[graphIndex] = newVoltage;
        currentBuffer[graphIndex] = newCurrent;
        graphIndex = (graphIndex + 1) % GRAPH_WIDTH;
        graphDataInitialized = true;

        //  更新最大值
        if (newVoltage > vHistoryMax) vHistoryMax = newVoltage;
        if (newCurrent > iHistoryMax) iHistoryMax = newCurrent;

        // Sticky auto-scale: only expand, never shrink
        const float marginFactor = 0.05f;

        if (!graphRangeInitialized) {
            vDisplayMin = 0.0f;
            vDisplayMax = fmaxf(0.1f, newVoltage * (1.0f + marginFactor));
            iDisplayMin = 0.0f;
            iDisplayMax = fmaxf(0.1f, newCurrent * (1.0f + marginFactor));
            graphRangeInitialized = true;
        } else {
            if (newVoltage > vDisplayMax) {
                vDisplayMax = newVoltage * (1.0f + marginFactor);
            }
            if (newCurrent > iDisplayMax) {
                iDisplayMax = newCurrent * (1.0f + marginFactor);
            }
            vDisplayMin = 0.0f;
            iDisplayMin = 0.0f;
        }

        if (vDisplayMax <= vDisplayMin) vDisplayMax = vDisplayMin + 0.1f;
        if (iDisplayMax <= iDisplayMin) iDisplayMax = iDisplayMin + 0.1f;
    }

    // --- Start drawing ---
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.fillScreen(0x0000);

    if (!graphDataInitialized) {
        spr.loadFont(Font1_12);
        spr.setTextColor(0xFFFF);
        spr.setTextDatum(CC_DATUM);
        spr.drawString("Sampling...", 120, 67);
        spr.unloadFont();
        spr.pushSprite(0, 0);
        spr.deleteSprite();
        return;
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
    
    // PAUSED indicator
    if (graphPaused) {
        spr.setTextDatum(CC_DATUM);
        spr.setTextColor(0xF800); // Red
        spr.drawString("PAUSED", 90, 67);
        // spr.drawString("OF", 222, 25);
        spr.drawString("OF", 192, 25);
    }else{
        spr.setTextDatum(CC_DATUM);
        spr.setTextColor(0x07FF); // Cyan
        // spr.drawString("ON", 222, 25);
        spr.drawString("ON", 192, 25);
    }

    spr.unloadFont();
    spr.pushSprite(0, 0);
    spr.deleteSprite();
}

void HAL::UI_System_Info(){
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setTextDatum(CC_DATUM); //设置文字对齐方式为居中
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
    spr.println(HAL::Get_System_RunTime(millis()));
    spr.unloadFont();

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

void HAL::UI_Menu() {
    static bool firstEntry = true;
    if (firstEntry) {
        firstEntry = false;
        MenuConfig::Menu_Init();  // 进入菜单时初始化为空闲模式
    }

    spr.createSprite(240, 135);
    spr.fillScreen(MenuColors::BACKGROUND);

    // === Header ===
    spr.loadFont(Font1_18);
    spr.setTextColor(MenuColors::TEXT_PRIMARY);
    spr.setCursor(10, 2);
    spr.print("Settings");
    spr.unloadFont();

    // === Separator ===
    spr.drawLine(0, 24, 239, 24, MenuColors::SEPARATOR);

    // === Menu Items ===
    spr.loadFont(Font1_14);
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
            spr.print("L SW0:Next | L SW1:Select");          // L = Long
            break;
        case MenuConfig::MODE_SELECT:
            spr.print("SW0↑ SW1↓ | L0:Back | L1:Edit");
            break;
        case MenuConfig::MODE_EDIT:
            spr.print("SW0+ SW1- | L0:Cancel | L1:Save");
            break;
    }
    spr.unloadFont();

    spr.pushSprite(0, 0);
    spr.deleteSprite();
}
