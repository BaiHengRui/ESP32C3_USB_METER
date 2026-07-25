# PDLib — 清洁版 USB PD FUSB302 库 v2.0

## ✨ 新特性

### 核心架构重构
```
PDLib/
├── FUSB302_Reg.h          # 寄存器/Bit定义 (共享头文件)
├── FUSB302_Driver.h/.cpp  # 硬件抽象层 (支持 Passive/Active 双模式)
├── PD_Parser.h/.cpp       # PD消息解析器 (纯函数, 无状态)
├── PD_Sniffer.h/.cpp      # 被动监听器 (Bridge/Sniffer 模式)
└── PD_Sink.h/.cpp         # 主动Sink控制器 (UFP模式)
```

### 🔑 关键修复: Bridge模式不再干扰PD握手

**旧库问题**: FUSB302 在 Bridge 模式下仍启用 `AUTO_CRC`, 自动回复 GoodCRC, 干扰实际 PD 通信。

**新库方案**: 
- `FUSB302_MODE_PASSIVE`: 完全不启用 AUTO_CRC, 不配置 TXCC, 纯监听
- `FUSB302_MODE_ACTIVE`:  正常 PD 通信模式

### 📡 PPS 监听增强
- 正确解析 Request 消息中的 PPS 电压/电流 (20mV/50mA 单位)
- 解析 PPS_Status 扩展消息获取实时输出参数
- 自动识别 Source Cap 中的 APDO (PPS 能力)

## 🚀 快速使用

### 方式一: 被动监听 (Bridge/Sniffer 模式)
```cpp
#include <PD_Sniffer.h>

PD_Sniffer sniffer;

void setup() {
    Wire.begin(4, 5);              // I2C: SDA=4, SCL=5
    sniffer.begin(12);             // INT pin = GPIO12
}

void loop() {
    sniffer.update();
    
    if (sniffer.isConnected()) {
        Serial.printf("V: %.2fV | I: %.2fA | %s | Pkts: %u\n",
            sniffer.getVoltage(),
            sniffer.getCurrent(),
            sniffer.isPPS() ? "PPS" : "FIX",
            sniffer.getPacketCount()
        );
    }
    delay(10);
}
```

### 方式二: 主动Sink (UFP模式, 可请求PD)
```cpp
#include <PD_Sink.h>

PD_Sink sink;

void setup() {
    Wire.begin(4, 5);
    sink.begin(12);                // INT pin = GPIO12
    sink.setPowerOption(PD_OPTION_MAX_POWER);
}

void loop() {
    sink.update();
    
    if (sink.isReady()) {
        Serial.printf("PD Ready: %.2fV %.2fA %s\n",
            sink.getVoltage(),
            sink.getCurrent(),
            sink.isPPS() ? "PPS" : "FIX"
        );
        
        // PPS 调压
        if (sink.isPPS()) {
            sink.setPPS(12.0f, 2.0f);  // 12V 2A
        }
    }
    delay(10);
}
```

## 📋 API 对照表

| 功能 | 旧库 (PD_UFP) | 新库 (PD_Sniffer) |
|------|--------------|-------------------|
| 初始化 | `init_Bridge(pin)` | `begin(pin)` |
| 主循环 | `run_Bridge()` | `update()` |
| 电压 | `get_bridge_voltage()` | `getVoltage()` |
| 电流 | `get_bridge_current()` | `getCurrent()` |
| 功率 | 手动计算 | `getPower()` |
| PPS状态 | 间接 | `isPPS()` |
| Source PDO | `get_bridge_src_cap_count()` | `getSourcePDOCount()` |
| PDO详情 | 无直接API | `getSourcePDO(i, &info)` |
| CC引脚 | `get_bridge_cc_pin()` | `getCCPin()` |
| 包计数 | `get_bridge_packet_count()` | `getPacketCount()` |
| PPS实时值 | 无 | `getPPSOutputVoltage()` |

## 🛠 迁移指南

如果你正在使用旧库的 `PD_UFP_Log_c`, 按以下方式迁移:

**旧代码:**
```cpp
PD_UFP_Log_c PD_UFP(PD_LOG_LEVEL_INFO, PD_BRIDGE_LOG_LEVEL_DETAILED);
PD_UFP.init_Bridge(FUSB302_INT_PIN);
PD_UFP.run_Bridge();
PD_Voltage = PD_UFP.get_bridge_voltage();
PD_Current = PD_UFP.get_bridge_current();
```

**新代码:**
```cpp
PD_Sniffer sniffer;
sniffer.begin(FUSB302_INT_PIN);
// loop中:
sniffer.update();
PD_Voltage = sniffer.getVoltage();
PD_Current = sniffer.getCurrent();
```

## ⚙️ 平台要求
- Arduino 框架 (ESP32 / ESP8266)
- Wire (I2C) 库
- FUSB302 芯片 (I2C地址 0x22)
