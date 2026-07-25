# INA228 Arduino Library

[![Version](https://img.shields.io/badge/version-1.0.0-blue)](https://github.com/BaiHengRui/INA228Lib/releases)
[![Platform](https://img.shields.io/badge/platform-Arduino%20|%20ESP32%20|%20ESP8266-green)](https://github.com/BaiHengRui/INA228Lib)
[![Framework](https://img.shields.io/badge/framework-Arduino-teal)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-GPL%20v3-blue)](https://www.gnu.org/licenses/gpl-3.0)
[![Chip](https://img.shields.io/badge/chip-INA228-orange)](https://www.ti.com/product/INA228)

Arduino 库，用于 Texas Instruments **INA228** 高精度数字功率/电流/电压监控器（20 位 Δ-Σ ADC）。支持总线电压、分流电压、电流、功率、温度、能量（J / mWh）、电荷量（C / mAh）的读取，分流电阻温度补偿、硬件能量与电荷累计、以及完整的阈值告警与诊断功能。

> 参考文档: [TI INA228 Datasheet](https://www.ti.com/product/INA228)

---

## 目录

- [硬件连接](#硬件连接)
- [安装](#安装)
- [快速开始](#快速开始)
- [API 参考](#api-参考)
  - [初始化与配置](#初始化与配置)
  - [数据读取](#数据读取)
  - [温度补偿](#温度补偿)
  - [Alert 报警功能](#alert-报警功能)
  - [状态查询](#状态查询)
  - [芯片信息与控制](#芯片信息与控制)
- [示例](#示例)
- [许可证](#许可证)

---

## 项目结构

```
INA228Lib/
├── INA228.h                 # 库头文件
├── INA228.cpp               # 库实现文件
├── library.properties       # Arduino Library Manager 配置
├── LICENSE                  # GPL v3 许可证
├── README.md                # 项目说明
└── examples/
    └── demo.ino             # 示例程序
```

---

## 硬件连接

INA228 使用 I²C 通信，只需连接 4 根线：

| INA228 引脚 | Arduino (Uno/Nano) | Arduino (Mega) |
|:-----------:|:------------------:|:--------------:|
| VCC         | 3.3V / 5V          | 3.3V / 5V      |
| GND         | GND                | GND            |
| SDA         | A4                 | 20             |
| SCL         | A5                 | 21             |

> **注意：** INA228 的 VCC 通常为 3.3V，但部分模块板载 LDO 支持 5V 供电，请确认你的模块规格。

典型应用电路：

```
    负载电源 ──┬── R_shunt ──┬── 负载 ──┐
               │             │          │
               ├─ IN+   IN- ─┘          │
               │   INA228              GND
               │                         │
               ├─ VBUS ─────────────────┘
               │
               └─ GND ──────────────── GND
```

> **选型提示：** 选择 $R_{shunt}$ 使 $V_{shunt\_max} = I_{max} \times R_{shunt}$ 在所选 ADC 量程内（±163.84 mV 或 ±40.96 mV）。

---

## 安装

### 方法一：Arduino Library Manager（推荐）

本库已包含 [`library.properties`](library.properties)，可通过 Arduino Library Manager 直接安装：

1. 打开 Arduino IDE
2. 点击 **项目** → **加载库** → **管理库**（或左侧 Library Manager 图标）
3. 搜索 `INA228Lib`
4. 点击 **安装**

### 方法二：手动安装

1. 点击本页绿色 `Code` 按钮，选择 **Download ZIP**
2. 在 Arduino IDE 中：**项目** → **加载库** → **添加 .ZIP 库**
3. 选择下载的 ZIP 文件即可

### 方法三：Git 克隆

```bash
cd ~/Arduino/libraries/
git clone https://github.com/BaiHengRui/INA228Lib.git
```

---

## 快速开始

```cpp
#include <Wire.h>
#include "INA228.h"

INA228 ina(Wire);

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 初始化 INA228（默认地址 0x40）
    if (!ina.begin()) {
        Serial.println("未找到 INA228，请检查接线！");
        while (1);
    }

    // 设置 ADC：16 次平均，1052 µs 转换时间，全通道连续模式
    ina.configure(INA228_AVERAGES_16,
                  INA228_CONV_TIME_1052US,
                  INA228_CONV_TIME_1052US,
                  INA228_CONV_TIME_1052US,
                  INA228_MODE_ALL_CONTINUOUS);

    // 校准: 分流电阻 0.01Ω, 最大期望电流 5A, ±40.96mV 量程
    ina.calibrate(0.01, 5.0, ADC_RANGE_40_96mV);

    // 温度补偿: 100 ppm/°C（锰铜）
    ina.setShuntTemperatureCoefficient(100);
    ina.enableTemperatureCompensation(true);
}

void loop() {
    Serial.print("总线电压: "); Serial.print(ina.readBusVoltage(), 3);   Serial.println(" V");
    Serial.print("分流电压: "); Serial.print(ina.readShuntVoltage() * 1000, 2); Serial.println(" mV");
    Serial.print("电流:     "); Serial.print(ina.readCurrent(), 3);       Serial.println(" A");
    Serial.print("功率:     "); Serial.print(ina.readPower(), 3);         Serial.println(" W");
    Serial.print("温度:     "); Serial.print(ina.readTemperature(), 2);   Serial.println(" °C");
    Serial.println("---");
    delay(1000);
}
```

---

## API 参考

### 初始化与配置

#### `INA228(TwoWire &w)`

构造函数，传入 Wire 对象。

```cpp
INA228 ina(Wire);
```

#### `bool begin(uint8_t address = INA228_ADDRESS)`

初始化 I²C 通信。

| 参数 | 说明 | 默认值 |
|:----|:-----|:------|
| `address` | I²C 地址 | `0x40` |

返回 `true` 表示初始化成功。

> INA228 的 A0/A1 引脚可设置不同地址（`0x40` ~ `0x4F`）。

#### `bool configure(avg, busConvTime, shuntConvTime, tempConvTime, mode)`

设置芯片工作参数。

| 参数 | 类型 | 可选值 | 说明 |
|:-----|:-----|:------|:-----|
| `avg` | `ina228_averages_t` | `INA228_AVERAGES_1` ~ `INA228_AVERAGES_1024` | 采样平均次数 |
| `busConvTime` | `ina228_convTime_t` | 见下表 | 总线电压转换时间 |
| `shuntConvTime` | `ina228_convTime_t` | 见下表 | 分流电压转换时间 |
| `tempConvTime` | `ina228_convTime_t` | 见下表 | 温度转换时间 |
| `mode` | `ina228_mode_t` | 见下表 | 工作模式 |

**转换时间 `ina228_convTime_t`：**

| 枚举值 | 时间 |
|:-------|:-----|
| `INA228_CONV_TIME_50US` | 50 µs |
| `INA228_CONV_TIME_84US` | 84 µs |
| `INA228_CONV_TIME_150US` | 150 µs |
| `INA228_CONV_TIME_280US` | 280 µs |
| `INA228_CONV_TIME_540US` | 540 µs |
| `INA228_CONV_TIME_1052US` | 1052 µs |
| `INA228_CONV_TIME_2074US` | 2074 µs |
| `INA228_CONV_TIME_4120US` | 4120 µs |

**工作模式 `ina228_mode_t`：**

| 枚举值 | 说明 |
|:-------|:-----|
| `INA228_MODE_SHUTDOWN` | 关断模式 |
| `INA228_MODE_BUS_SINGLE` | 总线电压单次触发 |
| `INA228_MODE_SHUNT_SINGLE` | 分流电压单次触发 |
| `INA228_MODE_BUS_SHUNT_SINGLE` | 总线+分流单次触发 |
| `INA228_MODE_TEMP_SINGLE` | 温度单次触发 |
| `INA228_MODE_BUS_TEMP_SINGLE` | 总线+温度单次触发 |
| `INA228_MODE_SHUNT_TEMP_SINGLE` | 分流+温度单次触发 |
| `INA228_MODE_ALL_SINGLE` | 全通道单次触发 |
| `INA228_MODE_BUS_CONTINUOUS` | 总线电压连续测量 |
| `INA228_MODE_SHUNT_CONTINUOUS` | 分流电压连续测量 |
| `INA228_MODE_BUS_SHUNT_CONTINUOUS` | 总线+分流连续测量 |
| `INA228_MODE_TEMP_CONTINUOUS` | 温度连续测量 |
| `INA228_MODE_BUS_TEMP_CONTINUOUS` | 总线+温度连续测量 |
| `INA228_MODE_SHUNT_TEMP_CONTINUOUS` | 分流+温度连续测量 |
| `INA228_MODE_ALL_CONTINUOUS` | 全通道连续测量 ⭐推荐 |

#### `bool setMode(ina228_mode_t mode)`

运行时切换工作模式，不改变其他配置。

```cpp
ina.setMode(INA228_MODE_SHUNT_CONTINUOUS);  // 仅测量分流电压
```

#### `bool calibrate(rShuntValue, iMaxCurrentExpected, adcRange)`

设置分流电阻值和最大期望电流，库会自动计算校准值。

| 参数 | 说明 | 示例 |
|:-----|:-----|:-----|
| `rShuntValue` | 分流电阻阻值（Ω） | `0.01`（10 mΩ） |
| `iMaxCurrentExpected` | 最大期望电流（A） | `5.0` |
| `adcRange` | ADC 量程 | `ADC_RANGE_163_84mV`（默认）或 `ADC_RANGE_40_96mV` |

```cpp
// 10 mΩ 分流电阻，最大电流 5A，使用高分辨率量程
ina.calibrate(0.01, 5.0, ADC_RANGE_40_96mV);
```

> **量程选择**：计算 $V_{shunt\_max} = I_{max} \times R_{shunt}$，选择刚好覆盖的量程以获得最佳分辨率。使用 `ADC_RANGE_40_96mV` 时，库会自动将 `SHUNT_CAL` 乘以 4（符合数据手册 §8.1.2）。

---

### 数据读取

所有读取函数返回值均为 `float` 类型，单位为标准 SI 单位。

| 函数 | 返回值 | 单位 |
|:-----|:------|:----:|
| `readBusVoltage()` | 总线电压 | V |
| `readShuntVoltage()` | 分流电压 | V |
| `readCurrent()` | 电流 | A |
| `readPower()` | 功率 | W |
| `readTemperature()` | 温度 | °C |
| `readEnergy()` | 能量 | J（焦耳） |
| `readEnergy_mWh()` | 能量 | mWh（毫瓦时） |
| `readCharge()` | 电荷 | C（库仑） |
| `readCharge_mAh()` | 电荷 | mAh（毫安时） |

```cpp
float busV   = ina.readBusVoltage();      // e.g. 12.005 V
float shuntV = ina.readShuntVoltage();    // e.g. 0.000025 V (25 µV)
float curr   = ina.readCurrent();         // e.g. 1.234 A
float power  = ina.readPower();           // e.g. 14.808 W
float temp   = ina.readTemperature();     // e.g. 35.2 °C
float energy = ina.readEnergy();          // e.g. 1234.5 J
float charge = ina.readCharge_mAh();      // e.g. 500.0 mAh
```

> **注意：** 能量和电荷的硬件累计需要芯片工作在连续模式下。可使用 `resetAccumulation()` 随时清零。

---

### 温度补偿

INA228 支持分流电阻温度补偿（TEMPCOMP），可根据温度变化自动修正电流测量值。

| 函数 | 说明 |
|:-----|:-----|
| `setShuntTemperatureCoefficient(ppm)` | 设置分流电阻温度系数（ppm/°C），范围 0 ~ 16383 |
| `enableTemperatureCompensation(enable)` | 启用 / 禁用温度补偿 |
| `readShuntTemperatureCoefficient()` | 读取当前温度系数设置 |

常见分流材料温度系数：

| 材料 | ppm/°C |
|:-----|:------:|
| 锰铜 (Manganin) | 50 ~ 100 |
| 康铜 (Constantan) | ±30 |
| 镍铬 (Nichrome) | 150 |
| 铜 (Copper) | 3930 |

```cpp
ina.setShuntTemperatureCoefficient(100);   // 100 ppm/°C
ina.enableTemperatureCompensation(true);
```

---

### Alert 报警功能

INA228 具有一个可配置的 Alert 引脚，可在以下条件触发。

#### 设置报警阈值

| 函数 | 参数 |
|:-----|:-----|
| `setShuntOverVoltageLimit(voltage)` | 分流过压阈值（V） |
| `setShuntUnderVoltageLimit(voltage)` | 分流欠压阈值（V） |
| `setBusOverVoltageLimit(voltage)` | 总线过压阈值（V） |
| `setBusUnderVoltageLimit(voltage)` | 总线欠压阈值（V） |
| `setTemperatureLimit(celsius)` | 温度上限阈值（°C） |
| `setPowerLimit(watts)` | 功率上限阈值（W） |

#### 报警行为配置

| 函数 | 说明 |
|:-----|:-----|
| `setAlertLatch(bool)` | 设置 Alert 锁存（`true` = 锁存直到读取） |
| `setAlertInvertedPolarity(bool)` | 设置 Alert 引脚极性（`true` = 高有效） |

```cpp
// 示例：当总线电压低于 4.5V 时触发报警
ina.setBusUnderVoltageLimit(4.5);
ina.setAlertLatch(true);
```

---

### 状态查询

| 函数 | 说明 |
|:-----|:-----|
| `isConversionReady()` | 转换是否完成 |
| `isAlert()` | Alert 是否触发 |
| `isMathOverflow()` | 是否发生数学溢出 |
| `isEnergyOverflow()` | 能量寄存器是否溢出 |
| `isChargeOverflow()` | 电荷寄存器是否溢出 |
| `isTempOverLimit()` | 温度是否超限 |
| `isShuntOverLimit()` | 分流电压是否超上限 |
| `isShuntUnderLimit()` | 分流电压是否低下限 |
| `isBusOverLimit()` | 总线电压是否超上限 |
| `isBusUnderLimit()` | 总线电压是否低下限 |
| `isPowerOverLimit()` | 功率是否超上限 |

---

### 芯片信息与控制

| 函数 | 说明 |
|:-----|:-----|
| `readManufacturerID()` | 制造商 ID（TI = `0x5449`） |
| `readDeviceID()` | 芯片 ID（INA228 = `0x2280` 或 `0x2281`） |
| `readConfig()` | 读取 CONFIG 寄存器原始值 |
| `reset()` | 软件复位 |
| `resetAccumulation()` | 能量/电荷累计清零 |
| `setADCRange(range)` | 手动切换 ADC 量程 |

---

## 示例

完整示例请见 [`examples/demo.ino`](examples/demo.ino)。

---

## 许可证

本项目采用 **GNU General Public License v3.0** 开源许可证。详情请参阅 [LICENSE](LICENSE) 文件。

---

&copy; 2026 BaiHengRui. Refer to [TI INA228 Datasheet](https://www.ti.com/product/INA228) for hardware details.

## 📄 许可

GPL v3.0 © 2026 BaiHengRui
