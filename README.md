# 此项目是ESP32C3-METER电流表项目的软件部分
> ⚠️ **此分支为 UI 美化版本** — 新增页面切换从左到右滑动过渡动画。稳定版请见 [`main`](https://github.com/BaiHengRui/ESP32C3_USB_METER/tree/main) 分支。

## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/project_gfxgdvkn)
## 上位机仓库 [ESP32C3-METER-Host上位机](https://github.com/BaiHengRui/ESP32C3_USB_METER_Host)
## HID底板开源平台 [ESP32C3-METER_HID扩展底板](https://oshwhub.com/bhr13151022/project_bidjtyiw)
## 代码较粗糙
## INA228库只支持Arduino环境，在工程的lib文件夹下

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
│   └── TFT_eSPI/            # TFT LCD 驱动（ST7789）
└── test/                    # 测试文件
    ├── t_lcd_menu.h/cpp     # LCD 菜单测试
    └── wave.cpp             # 波形测试
```
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
│       └── SW1 长按     进入菜单 / 确认
│
├── Task_INA22x (20ms)
│   └── INA228/INA226 数据采集 → 阈值计时更新
│
├── Task_UART_Command (10ms)
│   └── 串口命令解析
│       ├── brightness:<1-100>  设置亮度
│       ├── rotation:<0-3>      设置屏幕方向
│       ├── sample:<0-2>        设置采样率
│       ├── info                设备信息
│       ├── data                发送 USB CDC 数据包
│       ├── set_start=<mV>,<mA> 设置起始阈值
│       ├── set_end=<mV>,<mA>   设置结束阈值
│       ├── threshold           查看阈值配置
│       └── restart             重启设备
│
├── Task_Graph_Update (20ms)
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
| 单击 | 主界面: 切换应用 / 菜单: 上/减少 | 主界面: 切换应用 / 菜单: 下/增加 / 波形: 暂停 |
| **双击** | **全局: 切换采样率** | **全局: 切换屏幕方向** |
| 长按 | 返回主界面 / 菜单: 取消 | 进入系统信息 / 菜单: 确认 |

> 屏幕方向切换后，SW0/SW1 的左右功能自动交换，保持物理位置一致性。

### Toast 通知系统
- 双击切换采样率/方向时，屏幕底部显示半透明通知（持续 1.5 秒）
- 采样率: `Sample: Fast` / `Sample: Normal` / `Sample: Slow`
- 方向: `Rotation: UP` / `Rotation: DOWN`
- 通过 `pendingRotation` 延迟到帧间切换，避免 SPI 竞态导致花屏/反色
## 更新日志

V1.3.0

    1) 新增全局双击快捷键：
        - SW0 双击: 循环切换采样率 (Fast → Normal → Slow)
        - SW1 双击: 切换屏幕方向 (UP ↔ DOWN)
    2) 新增 Toast 通知系统：切换设置时屏幕底部显示半透明提示（1.5 秒）
    3) 修复屏幕方向切换时概率性花屏/反色（SPI 多任务竞态）
        - 方向切换延迟到帧间执行 (pendingRotation → ApplyPendingRotation)
    4) 重构项目文件结构：
        - 新增 globals.h / globals.cpp 集中管理全局变量
        - hal.h 精简为纯 API 接口 + 类型定义
        - 变量定义从各 .cpp 迁移至 globals.cpp 统一管理

V1.2.0

    1) 新增阈值计时功能：可设置起始/结束电压电流阈值，自动累计满足条件的时间段
    2) 阈值条件采用 AND 逻辑：电压与电流需同时满足才触发计时启停
    3) 新增 UART 命令：
        - set_start=<mV>,<mA>  设置起始阈值
        - set_end=<mV>,<mA>    设置结束阈值
        - threshold            查看当前阈值配置
    4) 主界面新增阈值计时显示（HH:MM:SS 格式），位于温度下方
    5) 阈值数据支持 NVS 持久化存储，断电不丢失
    6) Task_INA22x 每次采集后自动调用阈值计时更新
    7) 软件版本号升级至 v1.2.0

V1.1.18

    1) 曲线数据采集从UI绘制中解耦，新增独立FreeRTOS任务 Task_Graph_Update (30ms周期)
    2) 新增 HAL::Update_Graph_Data() 全局函数管理曲线采样与自动量程
    3) 曲线绘制增加除零保护，避免量程未初始化时除零崩溃
    4) 重构菜单按键逻辑：
        - SW0(左键)减少亮度、SW1(右键)增加亮度
        - 亮度步进从10%改为5%
        - 空闲模式下短按SW1即可进入选择，无需长按
    5) 新增旋转方向感知功能：方向1(Down)时左右按键功能自动交换
    6) 菜单Rotation显示优化：显示为"(1)Down" / "(3)UP"
    7) 菜单Exit项改为 "Exit Main(Back)"

V1.1.17

    1) 修复了屏幕方向倒转的情况下，电流方向箭头显示方向错误

V1.1.16

    1) 更新INA228库
    2) 设置ADC_RANGE 为40.96mV范围 最大电流8A

V1.1.15

    1) 新增了USB CDC数据结构体定义
    2) 优化了UART发送机制
    3) 同步更新上位机的曲线功能
    4) 优化了按键逻辑

V1.1.12

    1) 新增了INA228/INA226预留的标志位，可以进行更改标志进行不同的采样芯片支持 
    2) 在src\hal\hal.h头文件，#define INA228_EN 1
    3) 启用INA228 1 / 启用INA226 0
    4) 需注意！如果INA226版本则温度传感器使用MCU温度传感器，INA228则是使用内置温度传感器

## AI Coding 辅助说明
- 本项目在开发过程中使用了 AI Coding 辅助工具进行代码编写和调试。
