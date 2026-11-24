# ESP32C3 USB METER

此项目是 ESP32C3-METER 电流表项目的软件部分。

## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/project_gfxgdvkn)
## 上位机仓库 [ESP32C3-METER-Host上位机](https://github.com/BaiHengRui/ESP32C3_USB_METER_Host)
## HID底板开源平台 [ESP32C3-METER_HID扩展底板](https://oshwhub.com/bhr13151022/project_bidjtyiw)

### 默认设置
    亮度 50
    方向 3
    采样 0

#### 烧录选项
Releases里存放编译好的固件，可以使用flash_download_tool工具进行烧录
228尾缀为INA228系列固件，226尾缀为INA226系列固件

    bootloader.bin -> 0x0000
    partitions.bin -> 0x8000
    firmware.bin -> 0x10000

## 项目结构

```
ESP32C3_USB_METER/
├── platformio.ini           # PlatformIO 项目配置
├── minspiffs.csv            # 分区表 (NVS / OTA / app / LittleFS 2MB / coredump)
├── CHANGELOG.md             # 更新日志
├── LICENSE.md               # 开源协议
├── include/                 # 根目录头文件 (预留)
├── src/
│   ├── main.cpp             # FreeRTOS 任务入口
│   ├── hal/                 # 硬件抽象层
│   │   ├── hal.h            # HAL API 接口 + 数据类型定义
│   │   ├── hal.cpp          # 系统初始化 / APP 调度 / 曲线数据 / Toast 实现
│   │   ├── globals.h        # 全局变量声明
│   │   ├── globals.cpp      # 全局变量定义
│   │   ├── hal_button.cpp   # 按钮事件处理（单击/长按/双击）
│   │   ├── hal_ina.cpp      # INA228 / INA226 传感器驱动
│   │   ├── hal_lcd.h        # LCD 显示驱动头文件
│   │   ├── hal_lcd.cpp      # LCD 初始化 / 亮度 / 旋转 / FPS
│   │   ├── hal_nvs.cpp      # NVS 持久化存储
│   │   ├── hal_storage.cpp  # LittleFS 离线记录模块（多条目/会话）
│   │   ├── hal_protocol.cpp # FUSB302 PD 监听 / 挡位解析 / eMarker 读取
│   │   ├── hal_timer.cpp    # 阈值计时逻辑
│   │   ├── hal_uart.cpp     # UART 命令接口 + USB CDC 数据包
│   │   ├── lcd_menu.h       # 菜单系统头文件
│   │   └── lcd_menu.cpp     # 菜单系统实现（选择/编辑/NVS 读写）
│   ├── ui/                  # UI 界面
│   │   ├── ui.h             # UI 函数声明 + DrawToast
│   │   └── ui.cpp           # 主界面 / 波形图 / 菜单 / 系统信息 / Toast 渲染
│   └── assets/              # 字库 & 图片资源
│       ├── fonts/
│       └── imgs/
├── lib/                     # 第三方库
│   ├── Button2/             # 按钮库（支持单击/双击/长按）
│   ├── FUSB302/             # USB PD / Type-C 控制器库（监听 / Sink / eMarker）
│   ├── INA226Lib/           # INA226 驱动
│   ├── INA228Lib/           # INA228 驱动
│   └── TFT_eSPI/            # TFT LCD 驱动
└── test/                    # 测试文件（历史备份）
    └── backup/              # 旧版源码备份
        ├── PDLib/           # PD 协议库（待开发）
        └── src/             # 旧版源码
```

## 编译资源占用

```
RAM:   [=         ]   6.6% (used 21660 bytes from 327680 bytes)
Flash: [====      ]  36.4% (used 715466 bytes from 1966080 bytes)
```

> 编译环境：PlatformIO (espressif32 @ 7.0.1)，ESP32-C3 (160MHz，board 默认 f_cpu)，Arduino framework，默认 release 构建（-Os），CORE_DEBUG_LEVEL=ERROR。

## 功能流程顺序
### 系统初始化
```
系统启动
└── Sys_Init()          系统初始化 (NVS、Wire、Serial)
├── Button_Init()       按钮初始化
├── INA22x_Init()       INA228/INA226传感器初始化
├── Storage_Init()      LittleFS 离线存储初始化 (挂载/队列/互斥锁)
└── LCD_Init()          显示屏初始化
```

### FreeRTOS任务架构
```
系统启动 (Core 0)
│
├── Task_Button_Click (10ms, 优先级 4 — 最高)
│   └── 按键事件处理
│       ├── SW0 单击     (根据屏幕方向自动适配左右功能)
│       ├── SW1 单击     (根据屏幕方向自动适配左右功能)
│       ├── SW0 双击     全局: 循环切换采样率 (Fast → Normal → Slow)
│       ├── SW1 双击     全局: 切换屏幕方向 (UP ↔ DOWN)
│       ├── SW0 长按     返回 / 取消
│       └── SW1 长按     进入系统信息 / 菜单: 确认
│
├── Task_INA22x (10ms, 优先级 3)
│   └── INA228/INA226 数据采集 → 阈值计时更新
│
├── Task_PD (2ms, 优先级 3)
│   └── FUSB302 轮询 → Source_Capabilities / Request RDO / eMarker 探测 / Soft Reset 重新协商
│
├── Task_UART_Command (10ms, 优先级 3)
│   └── 串口命令解析
│       ├── brightness:<1-100>  设置亮度
│       ├── rotation:<0-3>      设置屏幕方向
│       ├── sample:<0-2>        设置采样率
│       ├── info                设备信息
│       ├── data                发送 USB CDC 数据包
│       ├── set_start=<mV>,<mA> 设置起始阈值
│       ├── set_end=<mV>,<mA>   设置结束阈值
│       ├── threshold           查看阈值配置
│       ├── record:<0|1|5|10|30|60>  设置离线记录间隔(秒)
│       ├── rec:enable:<0|1>    离线数据功能开关
│       ├── rec:start / rec:stop  开始/停止记录
│       ├── export:list         列出存储文件
│       ├── export:erase        清除全部条目
│       ├── export[:<编号>]     分块导出条目
│       ├── restart             重启设备
│       └── help                显示命令帮助
│
├── Task_Graph_Update (20ms, 优先级 2)
│   └── 曲线数据采样 → 环形缓冲区 → 自动量程
│
├── Task_Storage (阻塞接收, 优先级 2)
│   └── LittleFS 离线记录落盘 (队列 → RAM 缓冲 → 攒批写入)
│
└── Task_APP_Run (40ms, ~25 FPS, 优先级 1 — 最低)
    └── ApplyPendingRotation()  ← 帧间安全切换方向
        └── UI 渲染
            ├── MAIN         主界面 (V/A/W / 能量 / 温度 / 阈值计时 / 系统状态)
            ├── WAVEGRAPH    波形曲线图 (电压+电流双曲线 / 暂停)
            ├── STOREAGE_DATA 离线存储
            ├── MENU         设置菜单 (显示/采样/动效/数据/USB PD)
            └── SYSTEM_INFO  系统信息 (SN/版本/FPS/运行时间)
            └── DrawToast()  ← Toast 通知叠加 (1.5秒自动消失)
```

### 按键功能速查

| 操作 | SW0 (左键) | SW1 (右键) |
|------|-----------|-----------|
| 单击 | 主界面: 切换应用 / 菜单: 上/减少 | 菜单: 下/增加 / 波形: 暂停 |
| **双击** | **全局: 切换采样率** | **全局: 切换屏幕方向** |
| 长按 | 返回主界面 / 菜单: 取消 | 进入系统信息 / 菜单: 确认 |

> 屏幕方向切换后，SW0/SW1 的左右功能自动交换，保持物理位置一致性。

### Toast 通知系统
- 双击切换采样率/方向时，屏幕底部显示半透明通知（持续 1.5 秒）
- 采样率: `Sample: Fast` / `Sample: Normal` / `Sample: Slow`
- 方向: `Rotation: UP` / `Rotation: DOWN`
- 通过 `pendingRotation` 延迟到帧间切换，避免 SPI 竞态导致花屏/反色

## USB PD 监听（FUSB302）

电表默认以**被动监听**方式挂在 CC 线上

| 项目 | 说明 |
|------|------|
| 轮询周期 | **2 ms**（无中断引脚连接） |
| CC 方向 | **100 ms** 检测一次；如果当前 CC 测不到 Rp 时试探另一面，不打断正在收包的 CC |
| 挡位列表 | 来自 Source_Capabilities（PDO 列表） |
| 当前挡位 | 来自 Request RDO（PPS/AVS 显示协商设定电压/电流），绿色 `*` 标记 |
| 溢出统计 | RX FIFO 溢出次数，PD 页 LISTENING 状态显示为 `LOST` 字段 |
| 按键 | SW1 长按切换 CC1/CC2，SW0 返回菜单 |
| 光标 | 仅 SNK 模式（编译 `-DFUSB302_PD_MONITOR=0`），此时电表自己发 Request |

### 设置 → USB PD → Soft Reset

> 开关：**开 / 关**

该选项为解决PD设备协商速度小于初始化速度的时候无法获取到PDO报文和挡位

**触发条件**（必须全部满足）

1. 处于监听模式
2. VBUS > 4 V
3. 开机（或该功能被打开）后已超过 1.2 s
4. 尚未解析到任何 PD 报文，且没拿到 Source_Capabilities

**执行动作**

- FUSB302 暂时接管 CC 线，替 Source 发 **Soft_Reset**，让充电器重新广播 Source_Capabilities，从而完整抓到整个握手
- 最多 **3 次**，间隔 **1.5 s**
- 动作结束立即恢复纯被动监听
- 一旦解析到 Source_Capabilities / Request，计数自动复位，不会反复触发
- 链路始终不通（例如接的是非 PD 充电器）时，3 次之后停止

**警告信息**

会让PD 链路**重新协商一次**（PPS/AVS 可能短暂回落到 5 V 再切回，相当于一次"重新插拔"级别的事件）。如有无法重新握手等问题请关闭

## 更新日志

完整更新日志请见 [CHANGELOG.md](./CHANGELOG.md)。

## AI Coding 辅助说明
- 本项目在开发过程中使用了 AI Coding 辅助工具进行代码编写和调试。
