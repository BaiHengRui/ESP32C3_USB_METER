# INA226 Arduino Library

[![Version](https://img.shields.io/badge/version-1.0.0-blue)](https://github.com/BaiHengRui/INA226Lib/releases)
[![Platform](https://img.shields.io/badge/platform-Arduino%20|%20ESP32%20|%20ESP8266-green)](https://github.com/BaiHengRui/INA226Lib)
[![Framework](https://img.shields.io/badge/framework-Arduino-teal)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-GPL%20v3-blue)](https://www.gnu.org/licenses/gpl-3.0)
[![Chip](https://img.shields.io/badge/chip-INA226-orange)](https://www.ti.com/product/INA226)

Arduino 库，用于 Texas Instruments **INA226** 高侧/低侧双向电流/功率监视器。支持通过 I²C 接口读取总线电压、分流电压、电流和功率，并提供完整的 Alert 报警功能。

> 参考文档: [TI INA226 Datasheet](https://www.ti.com/product/INA226)

---

## 目录

- [硬件连接](#硬件连接)
- [安装](#安装)
- [快速开始](#快速开始)
- [API 参考](#api-参考)
  - [初始化与配置](#初始化与配置)
  - [数据读取](#数据读取)
  - [量程查询](#量程查询)
  - [Alert 报警功能](#alert-报警功能)
  - [状态查询](#状态查询)
  - [芯片信息](#芯片信息)
- [示例](#示例)
- [许可证](#许可证)

---

## 项目结构

```
INA226Lib/
├── INA226.h                 # 库头文件
├── INA226.cpp               # 库实现文件
├── library.properties       # Arduino Library Manager 配置
├── LICENSE                  # GPL v3 许可证
├── README.md                # 项目说明
└── examples/
    └── demo.ino             # 示例程序
```

---

## 硬件连接

INA226 使用 I²C 通信，只需连接 4 根线：

| INA226 引脚 | Arduino (Uno/Nano) | Arduino (Mega) |
|:-----------:|:------------------:|:--------------:|
| VCC         | 3.3V / 5V          | 3.3V / 5V      |
| GND         | GND                | GND            |
| SDA         | A4                 | 20             |
| SCL         | A5                 | 21             |

> **注意：** INA226 的 VCC 通常为 3.3V，但部分模块板载 LDO 支持 5V 供电，请确认你的模块规格。

典型应用电路：

```
    负载电源 ──┬── R_shunt ──┬── 负载 ──┐
               │             │          │
               ├─ IN+   IN- ─┘          │
               │   INA226              GND
               └────────────────────────┘
```

---

## 安装

### 方法一：Arduino Library Manager（推荐）

本库已包含 [`library.properties`](library.properties)，可通过 Arduino Library Manager 直接安装：

1. 打开 Arduino IDE
2. 点击 **项目** → **加载库** → **管理库**（或左侧 Library Manager 图标）
3. 搜索 `INA226Lib`
4. 点击 **安装**

### 方法二：手动安装

1. 点击本页绿色 `Code` 按钮，选择 **Download ZIP**
2. 在 Arduino IDE 中：**项目** → **加载库** → **添加 .ZIP 库**
3. 选择下载的 ZIP 文件即可

### 方法三：Git 克隆

```bash
cd ~/Arduino/libraries/
git clone https://github.com/BaiHengRui/INA226Lib.git
```

---

## 快速开始

```cpp
#include <Wire.h>
#include "INA226.h"

INA226 ina(Wire);

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 初始化 INA226（默认地址 0x40）
    if (!ina.begin()) {
        Serial.println("未找到 INA226，请检查接线！");
        while (1);
    }

    // 设置采样平均次数: 16次, 转换时间: 1.1ms, 连续模式
    ina.configure(INA226_AVERAGES_16,
                  INA226_CONV_TIME_1100US,
                  INA226_CONV_TIME_1100US,
                  INA226_MODE_SHUNT_BUS_CONT);

    // 校准: 分流电阻 0.01Ω, 最大期望电流 5A
    ina.calibrate(0.01, 5.0);
}

void loop() {
    Serial.print("总线电压: "); Serial.print(ina.readBusVoltage(), 3);   Serial.println(" V");
    Serial.print("分流电压: "); Serial.print(ina.readShuntVoltage() * 1000, 2); Serial.println(" mV");
    Serial.print("电流:     "); Serial.print(ina.readCurrent(), 3);       Serial.println(" A");
    Serial.print("功率:     "); Serial.print(ina.readPower(), 3);         Serial.println(" W");
    Serial.println("---");
    delay(1000);
}
```

---

## API 参考

### 初始化与配置

#### `INA226(TwoWire &w)`

构造函数，传入 Wire 对象。

```cpp
INA226 ina(Wire);
```

#### `bool begin(uint8_t address = INA226_ADDRESS)`

初始化 I²C 通信并应用默认配置。

| 参数 | 说明 | 默认值 |
|:----|:-----|:------|
| `address` | I²C 地址 | `0x40` |

返回 `true` 表示初始化成功。

> INA226 的 A0/A1 引脚可设置不同地址（`0x40` ~ `0x4F`）。

#### `bool configure(avg, busConvTime, shuntConvTime, mode)`

设置芯片工作参数。

| 参数 | 类型 | 可选值 | 说明 |
|:-----|:-----|:------|:-----|
| `avg` | `ina226_averages_t` | `INA226_AVERAGES_1` ~ `INA226_AVERAGES_1024` | 采样平均次数 |
| `busConvTime` | `ina226_convTime_t` | `INA226_CONV_TIME_140US` ~ `INA226_CONV_TIME_8244US` | 总线电压转换时间 |
| `shuntConvTime` | `ina226_convTime_t` | 同上 | 分流电压转换时间 |
| `mode` | `ina226_mode_t` | 见下表 | 工作模式 |

**工作模式：**

| 枚举值 | 说明 |
|:-------|:-----|
| `INA226_MODE_POWER_DOWN` | 掉电模式 |
| `INA226_MODE_SHUNT_TRIG` | 分流电压单次触发 |
| `INA226_MODE_BUS_TRIG` | 总线电压单次触发 |
| `INA226_MODE_SHUNT_BUS_TRIG` | 分流+总线单次触发 |
| `INA226_MODE_ADC_OFF` | ADC 关闭 |
| `INA226_MODE_SHUNT_CONT` | 分流电压连续测量 |
| `INA226_MODE_BUS_CONT` | 总线电压连续测量 |
| `INA226_MODE_SHUNT_BUS_CONT` | 分流+总线连续测量 ⭐推荐 |

#### `bool calibrate(rShuntValue, iMaxCurrentExcepted)`

设置分流电阻值和最大期望电流，库会自动计算校准值。

| 参数 | 说明 | 示例 |
|:-----|:-----|:-----|
| `rShuntValue` | 分流电阻阻值（Ω） | `0.01` (10mΩ) |
| `iMaxCurrentExcepted` | 最大期望电流（A） | `5.0` |

```cpp
// 10mΩ 分流电阻，最大电流 5A
ina.calibrate(0.01, 5.0);
```

---

### 数据读取

所有读取函数返回值均为 `float` 类型，单位为标准 SI 单位。

| 函数 | 返回值 | 单位 |
|:-----|:------|:----:|
| `readBusVoltage()` | 总线电压 | V |
| `readShuntVoltage()` | 分流电压 | V |
| `readCurrent()` | 电流 | A |
| `readPower()` | 功率 | W |

```cpp
float busV   = ina.readBusVoltage();    // e.g. 12.005 V
float shuntV = ina.readShuntVoltage();  // e.g. 0.000025 V (25 µV)
float curr   = ina.readCurrent();       // e.g. 1.234 A
float power  = ina.readPower();         // e.g. 14.808 W
```

---

### 量程查询

校准后可查询当前配置下的最大可测量范围：

| 函数 | 说明 |
|:-----|:-----|
| `getMaxPossibleCurrent()` | 理论最大电流（受分流电阻和 81.92mV 量程限制） |
| `getMaxCurrent()` | 实际最大电流（同时受 LSB 和分流电阻限制） |
| `getMaxShuntVoltage()` | 最大可测分流电压 |
| `getMaxPower()` | 最大可测功率 |

---

### Alert 报警功能

INA226 具有一个可配置的 Alert 引脚，可在以下条件触发：

#### 启用报警

| 函数 | 触发条件 |
|:-----|:---------|
| `enableShuntOverLimitAlert()` | 分流电压超上限 |
| `enableShuntUnderLimitAlert()` | 分流电压超下限 |
| `enableBusOvertLimitAlert()` | 总线电压超上限 |
| `enableBusUnderLimitAlert()` | 总线电压超下限 |
| `enableOverPowerLimitAlert()` | 功率超上限 |
| `enableConversionReadyAlert()` | 转换完成 |

#### 设置报警阈值

| 函数 | 参数 |
|:-----|:-----|
| `setBusVoltageLimit(voltage)` | 总线电压阈值（V） |
| `setShuntVoltageLimit(voltage)` | 分流电压阈值（V） |
| `setPowerLimit(watts)` | 功率阈值（W） |

#### 报警行为配置

| 函数 | 说明 |
|:-----|:-----|
| `setAlertInvertedPolarity(bool)` | 设置 Alert 引脚极性（`true` = 高有效） |
| `setAlertLatch(bool)` | 设置 Alert 锁存（`true` = 锁存直到读取） |

```cpp
// 示例：当总线电压低于 4.5V 时触发报警
ina.enableBusUnderLimitAlert();
ina.setBusVoltageLimit(4.5);
```

---

### 状态查询

| 函数 | 说明 |
|:-----|:-----|
| `isConversionReady()` | 转换是否完成 |
| `isAlert()` | Alert 是否触发 |
| `isMathOverflow()` | 是否发生数学溢出 |

---

### 芯片信息

| 函数 | 返回值 |
|:-----|:-------|
| `getManufacturerID()` | 制造商 ID（TI = `0x5449`） |
| `readDeviceID()` | 芯片 ID（INA226 = `0x2260`） |

---

## 示例

完整示例请见 [`examples/demo.ino`](examples/demo.ino)。

---

## 许可证

本项目采用 **GNU General Public License v3.0** 开源许可证。详情请参阅 [LICENSE](LICENSE) 文件。

---

&copy; 2026 BaiHengRui. Refer to [TI INA226 Datasheet](https://www.ti.com/product/INA226) for hardware details.
