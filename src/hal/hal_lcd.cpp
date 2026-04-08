// 不建议超过64MHz的SPI频率
#include "hal.h"
#include "hal_lcd.h"

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH,TFT_HEIGHT); //WIDTH, HEIGHT
TFT_eSprite spr = TFT_eSprite(&tft);

uint8_t defaultBrightness = 50; //默认亮度
uint8_t defaultRotation = 3;   //默认旋转
unsigned long lastFPSTime = 0;
unsigned int frameCount = 0;
float currentFPS = 0;

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
