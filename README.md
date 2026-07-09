# 此项目是ESP32C3-METER电流表项目的软件部分
## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/esp32c3-zhuo-mian-qi-xiang-zhan)
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
├── main.cpp                 # FreeRTOS任务入口
├── hal/                     # 硬件抽象层
│   ├── hal.h/cpp            # 系统初始化
│   ├── hal_button.cpp       # 按钮事件处理
│   ├── hal_ina.cpp          # INA228监测
│   ├── hal_lcd.h/cpp        # LCD显示驱动
│   ├── hal_uart.cpp         # UART命令接口
│   └── lcd_menu.h/cpp       # 菜单系统
├── ui/                      # UI界面
│   ├── ui.h
│   └── ui.cpp               # 主界面，曲线图，设置
└── assets/                  # 字库，图片资源
    ├── fonts/
    └── imgs/
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
系统启动
│
├── Task_INA22x (20ms周期)
│   └── INA228/INA226 采集数据
│
├── Task_UART_Command (10ms周期)
│   └── 串口命令解析
│       ├── brightness:<1-100>  设置亮度
│       ├── rotation:<0-3>      设置屏幕方向
│       ├── sample:<0-2>        设置采样率
│       ├── info               设备信息
│       ├── data               发送数据包
│       └── restart            重启设备
│
├── Task_APP_Run (40ms周期)
│   └── UI渲染
│       ├── MAIN        主界面(电压/电流/功率/能量/温度显示)
│       ├── WAVEGRAPH   波形曲线图
│       ├── MENU        设置菜单
│       └── SYSTEM_INFO 系统信息
│
├── Task_Graph_Update (30ms周期)
│   └── 曲线数据采集
│       ├── 采样电压/电流写入环形缓冲区
│       ├── 自动量程
│       └── 暂停/继续冻结当前值
│
└── Task_Button_Click (10ms周期)
    └── 按键事件处理
        ├── SW1短按   切换应用
        ├── SW2短按   波形暂停/继续
        ├── SW1长按   返回主界面
        └── SW2长按   进入系统信息/菜单选择模式
```
## 更新日志


V1.1.12

    1) 新增了INA228/INA226预留的标志位，可以进行更改标志进行不同的采样芯片支持 
    2) 在src\hal\hal.h头文件，#define INA228_EN 1
    3) 启用INA228 1 / 启用INA226 0
    4) 需注意！如果INA226版本则温度传感器使用MCU温度传感器，INA228则是使用内置温度传感器

V1.1.15

    1) 新增了USB CDC数据结构体定义
    2) 优化了UART发送机制
    3) 同步更新上位机的曲线功能
    4) 优化了按键逻辑

V1.1.16

    1) 更新INA228库
    2) 设置ADC_RANGE 为40.96mV范围 最大电流8A

V1.1.17

    1) 修复了屏幕方向倒转的情况下，电流方向箭头显示方向错误

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
## AI Coding 辅助说明
- 本项目在开发过程中使用了 AI Coding 辅助工具进行代码编写和调试。
