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

    // --- 寻找电压缓冲区的最高点和最低点 ---
    if (graphDataInitialized) {
        // 电压极值查找
        int vMaxIdx = 0, vMinIdx = 0;
        float vMaxVal = voltageBuffer[0];
        float vMinVal = voltageBuffer[0];
        for (int i = 1; i < GRAPH_WIDTH; i++) {
            float v = voltageBuffer[i];
            if (v > vMaxVal) { vMaxVal = v; vMaxIdx = i; }
            if (v < vMinVal) { vMinVal = v; vMinIdx = i; }
        }

        // 电压最高点 -> 红色向下三角 (顶点朝下)
        int offsetMaxV = (vMaxIdx - graphIndex + GRAPH_WIDTH) % GRAPH_WIDTH;
        int xMaxV = graphX + offsetMaxV;
        int yMaxV = graphY + graphHeight - 2 - 
                    (int)((vMaxVal - vDisplayMin) * (graphHeight - 2) / (vDisplayMax - vDisplayMin));
        spr.fillTriangle(xMaxV, yMaxV + 2,        // 下顶点
                        xMaxV - 2, yMaxV - 2,    // 左上
                        xMaxV + 2, yMaxV - 2,    // 右上
                        0xF800);                  // 红色

        // 电压最低点 -> 蓝色向上三角 (顶点朝上)
        int offsetMinV = (vMinIdx - graphIndex + GRAPH_WIDTH) % GRAPH_WIDTH;
        int xMinV = graphX + offsetMinV;
        int yMinV = graphY + graphHeight - 2 - 
                    (int)((vMinVal - vDisplayMin) * (graphHeight - 2) / (vDisplayMax - vDisplayMin));
        spr.fillTriangle(xMinV, yMinV - 2,        // 上顶点
                        xMinV - 2, yMinV + 2,    // 左下
                        xMinV + 2, yMinV + 2,    // 右下
                        0x001F);                  // 蓝色

        // --- 电流极值查找（同理）---
        int iMaxIdx = 0, iMinIdx = 0;
        float iMaxVal = currentBuffer[0];
        float iMinVal = currentBuffer[0];
        for (int i = 1; i < GRAPH_WIDTH; i++) {
            float c = currentBuffer[i];
            if (c > iMaxVal) { iMaxVal = c; iMaxIdx = i; }
            if (c < iMinVal) { iMinVal = c; iMinIdx = i; }
        }

        // 电流最高点 -> 红色向下三角
        int offsetMaxI = (iMaxIdx - graphIndex + GRAPH_WIDTH) % GRAPH_WIDTH;
        int xMaxI = graphX + offsetMaxI;
        int yMaxI = graphY + graphHeight - 2 - 
                    (int)((iMaxVal - iDisplayMin) * (graphHeight - 2) / (iDisplayMax - iDisplayMin));
        spr.fillTriangle(xMaxI, yMaxI + 2,
                        xMaxI - 2, yMaxI - 2,
                        xMaxI + 2, yMaxI - 2,
                        0xF800);

        // 电流最低点 -> 蓝色向上三角
        int offsetMinI = (iMinIdx - graphIndex + GRAPH_WIDTH) % GRAPH_WIDTH;
        int xMinI = graphX + offsetMinI;
        int yMinI = graphY + graphHeight - 2 - 
                    (int)((iMinVal - iDisplayMin) * (graphHeight - 2) / (iDisplayMax - iDisplayMin));
        spr.fillTriangle(xMinI, yMinI - 2,
                        xMinI - 2, yMinI + 2,
                        xMinI + 2, yMinI + 2,
                        0x001F);
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