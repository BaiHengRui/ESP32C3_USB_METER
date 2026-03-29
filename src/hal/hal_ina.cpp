#include "hal.h"

#ifdef INA228_EN
    #include "INA228.h"
    INA228 ina(Wire);
#else
    #include "INA226.h"
    INA226 ina(Wire);
#endif
// INA228 ina(Wire);

HAL::INA22x_Data INA;

uint8_t sample_mode = 0; //0: fast, 1: normal, 2: slow
// float shuntVoltage_mV;
// float busVoltage;
int32_t nowTime(0), lastTime(0);

bool HAL::INA22x_Init(){ 
    ina.begin(0x40);
    ina.calibrate(0.005f,8); //5m ohm 最大期望电流8A
    #ifdef INA228_EN
    //INA228特有功能，设置温度系数和启用温度补偿
        ina.setShuntTemperatureCoefficient(100); // 设置温度系数100ppm/*C
        ina.enableTemperatureCompensation(true);
    #endif
    HAL::INA22x_SetConfig(HAL::Sys_NVS_Valid("sample_mode", sample_mode, 2));
    HAL::LOG_INFO("INA Initialized.");
    return true;
}

void HAL::INA22x_GetData(INA22x_Data *data){
    nowTime = millis();
    // shuntVoltage_mV = ina.readShuntVoltage()/1000;
    // busVoltage = ina.readBusVoltage();
    // data->voltage = fabs(busVoltage + shuntVoltage_mV);
    data->voltage = fabs(ina.readBusVoltage());
    data->current = fabs(ina.readCurrent());
    data->power   = fabs(ina.readPower());
    //sample = avg * count
    data->current_direction = (ina.readCurrent() < 0) ? 1 : 0;
    #ifdef INA228_EN
    //INA228实现 内部累计功能，直接读取
        data->temperature = ina.readTemperature();
        data->charge_mAh = fabs(ina.readCharge_mAh());
        data->energy_mWh = ina.readEnergy_mWh();
    #else
    //INA226实现 没有硬件累计功能，时间积分计算
        data->temperature = ESP.getCpuTemperature();
        data->charge_mAh += (data->current * (nowTime - lastTime)) / (3600.0f * 1);
        data->energy_mWh += (data->power * (nowTime - lastTime)) / (3600.0f * 1);
    #endif
    data->charge_Ah = data->charge_mAh / 1000.0f;
    data->energy_Wh = data->energy_mWh / 1000.0f;
    data->device_id = ina.readDeviceID();
    lastTime = nowTime;
}

void HAL::INA22x_SetConfig(uint8_t sample_mode){
    #ifdef INA228_EN
    // INA228支持更高级的配置选项，可以根据需要进行调整
        switch (sample_mode)
        {    
        case 0:
            ina.configure(INA228_AVERAGES_64, INA228_CONV_TIME_280US, INA228_CONV_TIME_280US, INA228_CONV_TIME_280US, INA228_MODE_ALL_CONTINUOUS);
            break;
        case 1:
            ina.configure(INA228_AVERAGES_128, INA228_CONV_TIME_540US, INA228_CONV_TIME_540US, INA228_CONV_TIME_540US, INA228_MODE_ALL_CONTINUOUS);
            break;
        case 2:
            ina.configure(INA228_AVERAGES_256, INA228_CONV_TIME_1052US, INA228_CONV_TIME_1052US, INA228_CONV_TIME_1052US, INA228_MODE_ALL_CONTINUOUS);
            break;
        default:
            ina.configure(INA228_AVERAGES_64, INA228_CONV_TIME_280US, INA228_CONV_TIME_280US, INA228_CONV_TIME_280US, INA228_MODE_ALL_CONTINUOUS);
            break;
        }
    #else
    // INA226配置选项较为有限，主要是平均次数和转换时间
        switch (sample_mode)
        {    
        case 0:
            ina.configure(INA226_AVERAGES_16, INA226_CONV_TIME_140US, INA226_CONV_TIME_140US, INA226_MODE_CONTINUOUS);
            break;
        case 1:
            ina.configure(INA226_AVERAGES_64, INA226_CONV_TIME_560US, INA226_CONV_TIME_560US, INA226_MODE_CONTINUOUS);
            break;
        case 2:
            ina.configure(INA226_AVERAGES_128, INA226_CONV_TIME_1100US, INA226_CONV_TIME_1100US, INA226_MODE_CONTINUOUS);
            break;
        default:
            ina.configure(INA226_AVERAGES_16, INA226_CONV_TIME_140US, INA226_CONV_TIME_140US, INA226_MODE_CONTINUOUS);
            break;
        }
    #endif
}