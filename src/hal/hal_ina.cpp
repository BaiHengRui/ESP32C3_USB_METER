#include "hal.h"
#include "INA228.h"
INA228 ina(Wire);

HAL::INA22x_Data INA;

uint8_t sample_mode = 0; //0: fast, 1: normal, 2: slow
// float shuntVoltage_mV;
// float busVoltage;
int32_t nowTime(0), lastTime(0);

bool HAL::INA22x_Init(){ 
    ina.begin(0x40);
    ina.calibrate(0.005f,8); //5m ohm 最大期望电流8A
    ina.setShuntTemperatureCoefficient(100); // 设置温度系数100ppm/*C
    ina.enableTemperatureCompensation(true);
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
    data->temperature = ina.readTemperature();
    data->charge_mAh = fabs(ina.readCharge_mAh());
    data->energy_mWh = ina.readEnergy_mWh();
    // data->charge_mAh += (data->current * (nowTime - lastTime)) / (3600.0f * 1);
    // data->energy_mWh += (data->power * (nowTime - lastTime)) / (3600.0f * 1);
    data->charge_Ah = data->charge_mAh / 1000.0f;
    data->energy_Wh = data->energy_mWh / 1000.0f;
    data->device_id = ina.readDeviceID();
    lastTime = nowTime;
}

void HAL::INA22x_SetConfig(uint8_t sample_mode){
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
}