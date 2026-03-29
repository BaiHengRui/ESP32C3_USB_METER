# 此项目是ESP32C3-METER电流表项目的软件部分
## 硬件详见 [立创开源平台](https://oshwhub.com/bhr13151022/esp32c3-zhuo-mian-qi-xiang-zhan)
## 代码较粗糙，但是能用
## INA228库只支持Arduino环境，在工程的lib文件夹下

### 默认设置
    亮度 50
    方向 3
    采样 0

## 更新日志
    26/03/29
    1) 新增了INA228/INA226预留的标志位，可以进行更改标志进行不同的采样芯片支持 
    2) 在src\hal\hal.h头文件，#define INA228_EN 1
    3) 启用INA228 1 / 启用INA226 0
    4) 需注意！如果INA226版本则温度传感器使用MCU温度传感器，INA228则是使用内置温度传感器
