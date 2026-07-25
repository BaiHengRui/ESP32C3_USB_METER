/**
 * @file    PD_Sink.h
 * @brief   USB PD Sink (UFP) 主动控制器 — 可发起 PD 协商和 PPS 请求
 * 
 * 提供完整的 PD Sink 功能:
 *   - PD 协商: 自动获取 Source Cap, 选择最优 PDO
 *   - PPS 控制: 动态调节电压/电流
 *   - 电源策略: 多种电源选择策略 (最高电压/最大电流/最大功率)
 *   - 状态回调: 连接/断开/供电就绪/PPS更新 等事件通知
 * 
 * 用法:
 * @code
 *   PD_Sink sink;
 *   
 *   void setup() {
 *       sink.begin(INT_PIN);
 *       sink.setPowerOption(PD_OPTION_MAX_POWER);
 *   }
 *   
 *   void loop() {
 *       sink.update();
 *       
 *       if (sink.isReady()) {
 *           float v = sink.getVoltage();
 *           float i = sink.getCurrent();
 *       }
 *       
 *       // PPS 调压
 *       if (sink.isPPS()) {
 *           sink.setPPS(10.0f, 2.0f);  // 10V 2A
 *       }
 *   }
 * @endcode
 */

#ifndef PD_SINK_H
#define PD_SINK_H

#include <stdint.h>
#include <stdbool.h>
#include "FUSB302_Driver.h"
#include "PD_Parser.h"

#ifdef __cplusplus

/// 最大 PDO 缓存数
#define PD_SINK_MAX_PDO         7

/// 电源选择策略
typedef enum {
    PD_OPTION_MAX_5V        = 0,    ///< 最高5V
    PD_OPTION_MAX_9V        = 1,    ///< 最高9V
    PD_OPTION_MAX_12V       = 2,    ///< 最高12V
    PD_OPTION_MAX_15V       = 3,    ///< 最高15V
    PD_OPTION_MAX_20V       = 4,    ///< 最高20V
    PD_OPTION_MAX_VOLTAGE   = 5,    ///< 最大可用电压
    PD_OPTION_MAX_CURRENT   = 6,    ///< 最大可用电流
    PD_OPTION_MAX_POWER     = 7,    ///< 最大可用功率
} PD_PowerOption;

/// PD Sink 状态
typedef enum {
    PD_SINK_DISCONNECTED    = 0,    ///< 未连接
    PD_SINK_CONNECTED       = 1,    ///< 已连接, 等待 Source Cap
    PD_SINK_NEGOTIATING     = 2,    ///< 协商中
    PD_SINK_READY           = 3,    ///< 供电就绪
    PD_SINK_PPS_ACTIVE      = 4,    ///< PPS 活跃
    PD_SINK_ERROR           = 5,    ///< 错误
} PD_SinkState;

/// 事件回调类型
typedef enum {
    PD_SINK_EVT_CONNECTED       = 0,
    PD_SINK_EVT_DISCONNECTED    = 1,
    PD_SINK_EVT_SOURCE_CAP      = 2,
    PD_SINK_EVT_POWER_READY     = 3,
    PD_SINK_EVT_PPS_READY       = 4,
    PD_SINK_EVT_REJECTED        = 5,
    PD_SINK_EVT_ERROR           = 6,
} PD_SinkEvent;

// 前向声明
class PD_Sink;

/// 回调类型
typedef void (*PD_SinkCallback)(PD_Sink *sink, PD_SinkEvent event, void *userData);

class PD_Sink {
public:
    PD_Sink();

    /**
     * @brief 初始化 PD Sink
     * @param intPin  FUSB302 INT 引脚号
     * @param i2cAddr FUSB302 I2C 地址
     * @return true=成功
     */
    bool begin(uint8_t intPin, uint8_t i2cAddr = 0x22);

    /**
     * @brief 初始化并启用 PPS
     * @param intPin       FUSB302 INT 引脚号
     * @param ppsVoltage   初始 PPS 电压 (V)
     * @param ppsCurrent   初始 PPS 电流 (A)
     * @param option       电源策略
     */
    bool beginPPS(uint8_t intPin, float ppsVoltage, float ppsCurrent,
                  PD_PowerOption option = PD_OPTION_MAX_POWER);

    /// 设置事件回调
    void onEvent(PD_SinkCallback cb, void *userData = nullptr);

    /// 设置电源选择策略
    void setPowerOption(PD_PowerOption option);

    /// 主循环更新 (需在 loop() 中高频调用)
    void update();

    // --- 状态查询 ---

    /// 获取当前状态
    PD_SinkState getState();

    /// 供电是否就绪
    bool isReady();

    /// 是否 PPS 模式
    bool isPPS();

    /// 是否已连接
    bool isConnected();

    /// 获取当前电压 (V)
    float getVoltage();

    /// 获取当前电流 (A)
    float getCurrent();

    // --- PPS 控制 ---

    /**
     * @brief 设置 PPS 电压/电流
     * @param voltage 目标电压 (V)
     * @param current 目标电流 (A)
     * @return true=参数有效且已发送请求
     */
    bool setPPS(float voltage, float current);

    // --- Source Cap 查询 ---

    /// 获取源 PDO 数量
    uint8_t getSourcePDOCount();
    
    /// 获取指定 PDO 信息
    bool getSourcePDO(uint8_t index, PD_PDOInfo *info);

    /// 当前选中的 PDO 位置 (1~7)
    uint8_t getSelectedPosition();

    /// 是否支持 PPS
    bool isPPSCapable();

    // --- 底层访问 ---
    FUSB302_Driver * getDriver() { return &_dev; }

    // --- VBUS ---
    void enableVBUS(bool enable);

private:
    FUSB302_Driver _dev;
    uint8_t _intPin;
    bool _initialized;

    PD_SinkCallback _callback;
    void * _callbackData;

    // 电源策略
    PD_PowerOption _powerOption;

    // 状态机
    PD_SinkState _state;
    uint8_t _messageID;

    // Source Cap
    uint32_t _sourcePDOs[PD_SINK_MAX_PDO];
    uint8_t  _sourcePDOCount;
    uint8_t  _selectedPosition;
    bool     _ppsCapable;
    uint8_t  _ppsPDOIndex;

    // 当前供电
    uint16_t _voltage;      // mV
    uint16_t _current;      // mA
    bool     _isPPS;

    // PPS 目标
    uint16_t _ppsTargetVoltage;   // 20mV 单位
    uint8_t  _ppsTargetCurrent;   // 50mA 单位

    // 定时器
    uint32_t _lastPollTime;
    uint32_t _waitSrcCapStart;
    uint32_t _waitPSRDYStart;
    uint32_t _lastPPSRequest;
    uint8_t  _srcCapRetries;
    bool     _waitingSrcCap;
    bool     _waitingPSRDY;
    bool     _needRequest;

    // CC
    uint8_t _ccPin;
    uint8_t _cc1Level, _cc2Level;

    // 统计
    uint32_t _startTime;

    // I2C 静态回调
    static int _i2cRead(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count);
    static int _i2cWrite(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count);
    static int _delayMs(uint32_t ms);

    // 内部方法
    void _handleConnected();
    void _handleDisconnected();
    void _handleRX(uint16_t header, uint32_t *data);
    void _handleSourceCap(uint16_t header, uint32_t *data);
    void _handlePSRDY();
    void _handleReject();
    void _handleAccept();
    void _handlePPSStatus(uint32_t *data);
    void _evaluateAndSelect();
    void _sendRequest();
    void _sendGetSourceCap();
    void _fireEvent(PD_SinkEvent event);
};

#endif // __cplusplus
#endif // PD_SINK_H
