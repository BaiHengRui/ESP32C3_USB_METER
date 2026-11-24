// 不建议超过64MHz的SPI频率
#include "hal.h"
#include "hal_lcd.h"

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH,TFT_HEIGHT);
TFT_eSprite spr = TFT_eSprite(&tft);

unsigned long lastFPSTime = 0;
unsigned int frameCount = 0;
float currentFPS = 0;
static bool lcdDmaEnabled = false;

void HAL::LCD_Init(){
    analogWrite(LCD_BL_PIN,0);
    spr.init();
    // spr.invertDisplay(1); //根据屏幕实际情况选择是否反色显示，去掉优化约5ms
    currentRotation = defaultRotation;
    tft.setRotation(currentRotation);
    // tft.setRotation(3); //只能在tft对象操作。0-3 1/3观看为横屏
    spr.setColorDepth(16); //设置颜色深度为16位

    // initDMA() 会为 IDF 驱动重新配置 SPI 引脚；本工程 CS 接地，
    // 屏幕会看到这次总线切换，所以之后需要重新初始化一次显示。
    lcdDmaEnabled = tft.initDMA();
    if (lcdDmaEnabled) {
        tft.init();
        tft.setRotation(currentRotation);
    }

    HAL::LCD_SetBrightness(defaultBrightness);
    HAL::LOG_INFO("LCD Initialized (DMA %s).", lcdDmaEnabled ? "enabled" : "disabled");
    startTime = millis();
}

void HAL::LCD_PushSprite(){
    if (!lcdDmaEnabled || spr.getColorDepth() != 16) {
        spr.pushSprite(0, 0);
        return;
    }

    // 与 spr.pushSprite() 保持一致：sprite 数据按原始 RGB565 推送
    bool oldSwapBytes = tft.getSwapBytes();
    tft.setSwapBytes(false);

    // 锁住 CS 直到 DMA 完成；本工程 TFT_CS=-1 时同样安全
    tft.startWrite();
    tft.pushImageDMA(0, 0, spr.width(), spr.height(), (uint16_t*)spr.getPointer());
    tft.dmaWait();
    tft.endWrite();

    tft.setSwapBytes(oldSwapBytes);
}

void HAL::LCD_SetBrightness(uint8_t brightness){
    brightness = constrain(brightness,1,100);//限制大小
    int light_pwm = 255 - ((100 - brightness) * 1.5);
    analogWrite(LCD_BL_PIN,light_pwm);
}
 
void HAL::LCD_SetRotation(uint8_t rotation){
    rotation = constrain(rotation,0,3);
    tft.setRotation(rotation);
    currentRotation = rotation;
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
