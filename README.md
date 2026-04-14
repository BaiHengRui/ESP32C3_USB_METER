# 此项目是ESP32C3-METER电流表项目的软件部分
## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/esp32c3-zhuo-mian-qi-xiang-zhan)
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
