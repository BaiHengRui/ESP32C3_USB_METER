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
├── minspiffs.csv            # 分区表 (NVS / OTA / app / SPIFFS 2MB / coredump)
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
│   │   ├── hal_storage.cpp  # SPIFFS 离线记录模块（多条目/会话）
│   │   ├── hal_protocol.cpp # USB 协议预留（当前为空实现）
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
RAM:   [=         ]   6.3% (used 20636 bytes from 327680 bytes)
Flash: [===       ]  34.0% (used 669040 bytes from 1966080 bytes)
```

> 编译环境：PlatformIO (espressif32 @ 7.0.1)，ESP32-C3 (160MHz)，Release 模式。
## 功能流程顺序
### 系统初始化
```
系统启动
└── Sys_Init()          系统初始化 (NVS、Wire、Serial)
├── Button_Init()       按钮初始化
├── INA22x_Init()       INA228/INA226传感器初始化
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
│       ├── restart             重启设备
│       └── help                显示命令帮助
│
├── Task_Graph_Update (20ms, 优先级 2)
│   └── 曲线数据采样 → 环形缓冲区 → 自动量程
│
└── Task_APP_Run (40ms, ~25 FPS, 优先级 1 — 最低)
    └── ApplyPendingRotation()  ← 帧间安全切换方向
        └── UI 渲染
            ├── MAIN         主界面 (V/A/W / 能量 / 温度 / 阈值计时 / 系统状态)
            ├── WAVEGRAPH    波形曲线图 (电压+电流双曲线 / 暂停)
            ├── MENU         设置菜单 (亮度/方向/采样率)
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
## 更新日志

完整更新日志请见 [CHANGELOG.md](./CHANGELOG.md)。

## AI Coding 辅助说明
- 本项目在开发过程中使用了 AI Coding 辅助工具进行代码编写和调试。
