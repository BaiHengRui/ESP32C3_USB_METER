# FUSB302 USB PD Library for ESP32 Arduino

面向 ESP32（Arduino / PlatformIO）的 FUSB302 USB Type-C 与 USB Power Delivery
驱动库。参考并重构了以下项目的优点：

- [manuelbl/usb-pd-arduino](https://github.com/manuelbl/usb-pd-arduino)：清晰的
  Arduino API、PD 报文/PDO 解析与协议分析仪输出。
- [kralonur/kws-x1-standalone-rs](https://github.com/kralonur/kws-x1-standalone-rs)：
  FUSB302 寄存器级初始化、RX FIFO 报文重组、CRC 校验、SOP'/SOP''、eMarker
  读取与模拟、固定/PPS/AVS 请求构造。

本库不依赖 STM32 外设，仅使用 Arduino `Wire`、`Print`、`millis()`/`delay()`，
可直接放进 PlatformIO 的 `lib/FUSB302` 或 Arduino 的 `libraries/FUSB302`。

## 功能

| 模块 | 说明 |
| --- | --- |
| CC 检测 | Rd 下拉、CC1/CC2 方向识别、Rp 电流等级（500/1500/3000 mA）、VBUSOK |
| PD 协议读取 | RX FIFO 帧重组，支持 SOP / SOP' / SOP'' / Debug，CRC-32 校验 |
| 报文读取 | `PDMessage` 头字段、Data Object、扩展报文头、时间戳、错误帧标记 |
| 协议分析 | 报文名称、Sender、PDO/RDO 解码、`Print` 输出（Serial/串口屏均可） |
| PD 发送 | 控制/数据报文、自动重试、GoodCRC 跟踪、Hard/Soft Reset |
| 电源请求 | Fixed / PPS / SPR AVS RDO 构造与发送 |
| Source 能力解析 | Fixed / Battery / Variable / PPS / AVS APDO |
| eMarker 读取 | VCONN 上电、SOP' Soft_Reset、Discover Identity、身份解码 |
| eMarker 模拟 | 作为 Cable Plug 自动应答 SOP' Discover Identity（5 A 模拟等） |

## 目录结构

```
lib/FUSB302/
├── library.json
├── library.properties
├── README.md
├── LICENSE
├── examples/
│   ├── CC_Detect/CC_Detect.ino
│   ├── PD_Analyzer/PD_Analyzer.ino
│   ├── PD_Sink/PD_Sink.ino
│   ├── EMark_Dump/EMark_Dump.ino
│   └── EMark_Emulator/EMark_Emulator.ino
└── src/
    ├── FUSB302.h              // 总头文件
    ├── FUSB302_Registers.h    // 寄存器/位域/FIFO token
    ├── FUSB302_Message.h/.cpp // PD 报文、PDO、RDO、分析仪
    ├── FUSB302_Cable.h/.cpp   // eMarker 解码/模拟
    └── FUSB302_Device.h/.cpp  // I2C 驱动、RX/TX、角色与电缆流程
```

## 硬件与接线

- I2C 地址：`0x22`（FUSB302BMPX/BVMPX/BUCX；B01/B10/B11 为 `0x23..0x25`）。
- 逻辑电平 3.3 V；`SCL/SDA` 需要上拉。
- `INT_N` 可以不接：本库轮询状态/中断寄存器，不依赖外部中断。
- FUSB302 的 `CC1/CC2` 必须接到 USB-C 连接器的 CC 线；VBUS 测量仍由板上的
  INA228/INA226 完成，本库不测量 VBUS。
- 作为 Sink 时，FUSB302 在 CC 上提供 Rd；作为被动分析仪时不要加 Rd，否则会
  干扰原链路。
- **eMarker 读取**需要把 5 V VCONN 接到 FUSB302 的 VCONN 引脚（具体以硬件原理图
  为准）；库只负责闭合 VCONN 开关和 SOP' 通信。没有 VCONN 时读取会超时。
- **eMarker 模拟**会让本端口对 Source 表现为线缆 Plug，必须确认 CC/VCONN 走线
  与角色允许，避免与正常 Sink 行为冲突。

> 警告：USB PD 涉及 5–48 V 电压与可达 5 A 的电流。请确认板的耐压、走线、连接器
> 与保护器件后再上电实验；不要在未验证的硬件上直接请求高压。

## 快速开始

### 1. 被动 PD 协议分析仪

```cpp
#include <FUSB302.h>

FUSB302 pd;

void setup() {
    Serial.begin(115200);
    pd.begin(7, 6);          // SDA, SCL
    pd.beginMonitor(1);      // 监听 CC1
}

void loop() {
    pd.poll();
    fusb302::PDMessage msg;
    while (pd.readMessage(msg)) {
        fusb302::ProtocolAnalyzer::printMessage(Serial, msg, true);
    }
}
```

输出示例：

```
12345ms RX CC1 SOP Source Source_Capabilities id=0 hdr=0x11A1 2A100A01 ...
  PDO1: Fixed 5000mV 3000mA  raw=...
  PDO2: PPS 3300-11000mV 3000mA  raw=...
```

监听模式配置与 KWS-X1 的被动 sniffer 一致：Type-C 状态机 DRP 但不 toggle，
`SWITCHES1` 只保留 PD 2.0 SPECREV（AUTO_CRC 和两路 TX 都关闭），`POWER` 只开
bandgap + receiver + measure，`SWITCHES0` 只选通测量 CC，不接 Rd/Rp。默认只接收
SOP（Source_Capabilities 走 SOP）。需要同时抓取线缆 eMarker 的 SOP'/SOP'' 时，
编译时加 `-DFUSB302_MONITOR_SOP_PRIME=1`。

监听模式注意事项：

- 被动抽头没有 Rd，FUSB302 启动前已经完成的协商收不到。上电后重新插拔一次，
  或让充电器重新触发 PD。
- CC 方向取决于插头方向；`beginMonitor(1)` 收不到时改 `beginMonitor(2)`，或调用
  `setActiveCC(2)` 在运行时切换。
- FUSB302 的 CC1/CC2 必须接到被测 USB-C 的 CC 网络；只接 VBUS/GND 收不到报文。
  模块上若自带 5.1 kΩ Rd，会额外加载 CC，建议去掉。
- 被动监听不会发送 GoodCRC。若总线上的 Source 因超时反复 Hard Reset，属于正常
  现象，重新抓取即可。
- `readCableIdentity()` 在监听模式下会临时在非活动 CC 上加 VCONN，并在活动 CC 上
  发送 SOP' Soft_Reset/Discover Identity；这是一次显式的主动探测，不是完全被动。
  探测结束会恢复 `beginMonitor()` 的配置。线缆没有 eMarker、VCONN 未接或 PD 总线
  正忙时会超时/失败，重试即可。

### 2. Sink / 触发板

```cpp
pd.begin(7, 6);
pd.beginSink();

// 收到 Source_Capabilities 后：
fusb302::PDAdvertisements ads;
fusb302::parseSourceCapabilities(msg, ads);
pd.requestFixed(2, 2000);                     // PDO2, 2 A
// 或 PPS：
pd.requestPPS(3, 9000, 2000);                 // 9 V / 2 A
// 或 SPR AVS：
pd.requestAVS(4, 15000, 2000);
```

### 3. 读取 eMarker

```cpp
uint8_t cc;
pd.detectOrientation(cc);
fusb302::CableIdentity id;
if (pd.readCableIdentity(id, 300)) {
    fusb302::printCableIdentity(Serial, id);
}
```

### 4. 模拟 eMarker

```cpp
fusb302::CableEmulationConfig cable;
cable.kind = fusb302::CableKind::Passive;
cable.maxCurrent_mA = 5000;
cable.maxVoltage_mV = 20000;
pd.setCableEmulation(cable);
pd.enableCableEmulation(true);
```

## API 概览

### 初始化与角色

| API | 说明 |
| --- | --- |
| `bool begin()` / `begin(TwoWire&, addr)` / `begin(sda,scl,freq)` | 检测芯片并复位 PD 逻辑 |
| `bool beginSink()` | Rd + 方向识别 + TX + 自动 GoodCRC |
| `bool beginMonitor(uint8_t cc)` | 被动监听，不拉 Rd；默认不发送，显式读 eMarker 时才临时启用 VCONN/SOP' |
| `bool detectOrientation(uint8_t& cc)` | 重新测量 CC 方向（返回是否检测到 Source） |
| `bool setActiveCC(uint8_t cc)` | 手动指定 CC |
| `bool isConnected()` | Sink：VBUSOK + BC_LVL；Monitor：BMC 活动 |
| `uint16_t sourceCurrent_mA()` | Rp 等级：0/500/1500/3000 mA |

### 报文读取

| API | 说明 |
| --- | --- |
| `uint8_t poll()` | 读中断并把最多 4 帧放入内部队列 |
| `bool readMessage(PDMessage&)` | 从队列取一帧；队列空时自动 poll |
| `bool waitForMessage(msg, ms, sop, type)` | 阻塞等待指定 SOP/类型 |
| `uint32_t rxFrameCount()` / `invalidFrameCount()` | 统计 |

`PDMessage` 提供：

- `header()`、`type()`、`objectCount()`、`messageId()`、`revision()`
- `isControl()` / `isData()` / `extended()`
- `portPowerRole()`（SOP 电源角色 / SOP' Cable Plug）
- `object(i)`、`setObject(i, v)`
- `typeName()`、`crcValid`、`timestamp`、`cc`、`sop`
- 扩展报文：`extendedHeader()`

### 报文发送

| API | 说明 |
| --- | --- |
| `sendMessage(PDMessage&, timeout, waitGoodCrc)` | 发送任意 SOP / 扩展报文 |
| `sendControl(type, msgId)` | 控制报文 |
| `sendData(type, objects, count, msgId)` | 数据报文 |
| `requestFixed(pos, mA)` / `requestPPS(pos, mV, mA)` / `requestAVS(...)` | RDO 请求 |
| `softReset()` / `hardReset()` | 复位 |
| `setMessageId()` / `nextMessageId()` | MessageID 管理 |

### eMarker

| API | 说明 |
| --- | --- |
| `readCableIdentity(id, timeout)` | VCONN 上电→SOP' Soft_Reset→Discover Identity→解码；Sink/Monitor 均可，Monitor 完成后自动恢复监听 |
| `decodeCableIdentity(objects, count, rev, id)` | 离线解码（可用于分析仪日志） |
| `setCableEmulation(config)` | 设置模拟的线缆能力 |
| `enableCableEmulation(bool)` | 开关自动应答 SOP' Discover Identity |

### 诊断

`readRegisters()`、`readStatus()`、`deviceId()`、`lastError()` /
`lastErrorString()`、`lastInterrupts()`、`hardResetReceived()`、
`softResetReceived()`。

## 集成到 FreeRTOS 项目

本库不是中断驱动，建议在 1–5 ms 周期的任务或主循环中调用 `poll()`。若 `Wire`
与其他设备（INA228 等）共享，可把现有互斥量交给库：

```cpp
static void wireLock()   { xSemaphoreTake(xWireMutex, portMAX_DELAY); }
static void wireUnlock() { xSemaphoreGive(xWireMutex); }

pd.setI2CLock(wireLock, wireUnlock);
```

## 实现说明

- RX FIFO 的首字节是 FUSB302 的 SOP* token（`bits 7:5`），后续为
  header + data + little-endian CRC-32。本库按 Header 的 NDO/Extended 字段计算
  长度：扩展报文按 NDO×4 读取（含 4 字节对齐填充），`extendedDataSize()` 返回
  Extended Header 中未填充的 Data Size；支持单包扩展报文与单 Chunk（不组装多
  Chunk）。
- 发送使用 FUSB302 的 TX FIFO token：SYNC1/2/3、PACKSYM、JAMCRC、EOP、TXOFF、
  TXON；硬件计算 CRC，自动重试 3 次。
- `sendControl()` / `sendData()` / `softReset()` 使用最近一次收到的 SOP 报文
  协商出的 revision（`peerRevision()`，1=PD2、2=PD3）；自定义报文可直接设置
  Header。
- 自动 GoodCRC：Sink 模式设置 `SWITCHES1.AUTO_CRC`，GoodCRC 的 SPECREV 保持
  `01b`（PD 2.0），这是 FUSB302 的推荐做法。
- eMarker 读取按 USB PD 3.2 Table 6.33–6.44：SVDM 版本、Passive v1.0 /
  Active v1.3 布局、3 A/5 A、20 V/50 V、EPR、SOP'' 控制器等字段。
- eMarker 模拟只应答 Discover Identity；其他 SOP' 报文由硬件自动 GoodCRC 后
  不回复（与无模式线缆行为一致）。

## 限制

- 未实现 Source（Rp）角色、VCONN Swap、PR/DR Swap 等策略引擎。
- 不组装多 Chunk 扩展报文；EPR 报文可按单 Chunk 读取，但不进行 EPR 进入/退出
  协商。
- eMarker 读取会短暂占用 CC/VCONN 并可能打断正常 PD 流量，读取后应重新
  `detectOrientation()` 或让上层重新同步。
- 本库只负责 FUSB302；VBUS 电压/电流测量、保护与电源路径请使用板上 INA228/
  INA226 及硬件保护。

## 参考与许可

MIT License。算法与寄存器定义参考：

- ON Semiconductor FUSB302B datasheet
- Linux `drivers/usb/typec/tcpm/fusb302.c` / `fusb302_reg.h`
- manuelbl/usb-pd-arduino (MIT)
- kralonur/kws-x1-standalone-rs, `fusb302-pd` (MIT OR Apache-2.0)
- kralonur/embedded-charging-protocols-rs, `pd-protocol` (MIT OR Apache-2.0)

详见 `LICENSE`。
