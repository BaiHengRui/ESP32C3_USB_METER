# 此项目是ESP32C3-METER电流表项目的软件部分
## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/esp32c3-zhuo-mian-qi-xiang-zhan)
## 代码较粗糙，但是能用
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
