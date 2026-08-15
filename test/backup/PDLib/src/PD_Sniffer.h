/**
 * @file    PD_Sniffer.h
 * @brief   PD 总线被动监听器 — 纯嗅探模式, 绝不干扰 PD 握手
 * 
 * 设计目标:
 *   - 完全被动: 不发送任何 PD 消息, 不响应 GoodCRC
 *   - 实时监听: 捕获 Source Capabilities, Request, PS_RDY, PPS_Status 等关键消息
 *   - PPS 追踪: 自动解析 PPS 请求和状态, 实时更新输出电压/电流
 *   - 事件驱动: 提供回调机制, 也可轮询
 *   - API 简洁: begin() / update() / get*() 三步即可使用
 * 
 * 工作原理:
 *   1. FUSB302 配置为 MODE_PASSIVE (AUTO_CRC=OFF)
 *   2. 内部状态机跟踪 PD 协议交互序列
 *   3. 收到 Source_Cap → 缓存所有 PDO, 识别 PPS 能力
 *   4. 收到 Request → 解析当前请求的电压/电流/PPS参数
 *   5. 收到 PS_RDY → 确认供电就绪
 *   6. 收到 PPS_Status → 更新实时输出参数
 * 
 * 用法:
 * @code
 *   PD_Sniffer sniffer;
 *   
 *   void setup() {
 *       sniffer.begin(INT_PIN);  // INT_PIN: FUSB302 中断引脚
 *   }
 *   
 *   void loop() {
 *       sniffer.update();
 *       
 *       float v = sniffer.getVoltage();
 *       float i = sniffer.getCurrent();
 *       bool pps = sniffer.isPPS();
 *       uint8_t pos = sniffer.getSelectedPosition();
 *   }
 * @endcode
 */

#ifndef PD_SNIFFER_H
#define PD_SNIFFER_H

#include <stdint.h>
#include <stdbool.h>

#include "FUSB302_Driver.h"
#include "PD_Parser.h"

#ifdef __cplusplus

// ============================================================================
// 一、配置常量
// ============================================================================

/// 最大缓存的 Source PDO 数量
#define PD_SNIFFER_MAX_PDO         7

/// 回调事件类型
typedef enum {
    PD_SNIFF_EVT_ATTACHED       = 0,    ///< 检测到 PD Source 连接
    PD_SNIFF_EVT_DETACHED       = 1,    ///< PD Source 断开
    PD_SNIFF_EVT_SOURCE_CAP     = 2,    ///< 收到 Source Capabilities (含PDO列表)
    PD_SNIFF_EVT_REQUEST        = 3,    ///< 收到 Request 消息
    PD_SNIFF_EVT_PS_RDY         = 4,    ///< 供电就绪
    PD_SNIFF_EVT_REJECT         = 5,    ///< 请求被拒绝
    PD_SNIFF_EVT_PPS_STATUS     = 6,    ///< 收到 PPS_Status
    PD_SNIFF_EVT_HARD_RESET     = 7,    ///< Hard Reset
    PD_SNIFF_EVT_COUNT          = 8,
} PD_SnifferEvent;

/// 电源状态枚举
typedef enum {
    PD_POWER_NONE   = 0,    ///< 未确定 (初始状态)
    PD_POWER_FIXED  = 1,    ///< Fixed PDO 供电
    PD_POWER_PPS    = 2,    ///< PPS 供电
} PD_PowerStatus;

// 前向声明
class PD_Sniffer;

/// 事件回调函数类型
typedef void (*PD_SnifferCallback)(PD_Sniffer *sniffer, PD_SnifferEvent event, void *userData);

// ============================================================================
// 二、PD_Sniffer 类
// ============================================================================

class PD_Sniffer {
public:
    // --- 构造与初始化 ---

    PD_Sniffer();

    /**
     * @brief 初始化被动监听器
     * @param intPin   FUSB302 INT 引脚号 (用于检测中断, ESP32: GPIO编号)
     * @param i2cAddr  FUSB302 I2C 地址 (默认 0x22)
     * @return true=初始化成功, false=设备未找到
     */
    bool begin(uint8_t intPin, uint8_t i2cAddr = 0x22);

    /**
     * @brief 设置事件回调 (可选)
     * @param cb       回调函数
     * @param userData 用户数据指针 (回调时原样传回)
     */
    void onEvent(PD_SnifferCallback cb, void *userData = nullptr);

    /**
     * @brief 主循环更新 (需要在 loop() 中高频调用, 建议 >= 100Hz)
     * 
     * 检查 INT 引脚, 如有中断则读取 FUSB302 事件并解析 PD 消息
     */
    void update();

    // --- 电源状态查询 ---

    /// 获取当前输出电压 (V)
    float getVoltage();

    /// 获取当前输出电流 (A)
    float getCurrent();

    /// 获取当前功率 (W)
    float getPower();

    /// 获取电源状态 (PD_POWER_NONE / PD_POWER_FIXED / PD_POWER_PPS)
    PD_PowerStatus getPowerStatus();

    /// 是否 PPS 供电
    bool isPPS();

    /// 是否已连接 (检测到 CC)
    bool isConnected();

    /// 当前选择的 PDO 位置 (1~7)
    uint8_t getSelectedPosition();

    // --- Source Capabilities 查询 ---

    /// 获取源 PDO 数量
    uint8_t getSourcePDOCount();

    /**
     * @brief 获取指定索引的 PDO 信息
     * @param index 0~6 (如超过实际数量则返回false)
     * @param info  输出: PDO 解析结果
     * @return true=成功
     */
    bool getSourcePDO(uint8_t index, PD_PDOInfo *info);

    /**
     * @brief 获取源 PDO 原始值
     * @param index 0~6
     * @return 32位原始 PDO 值, 无效则返回0
     */
    uint32_t getSourcePDORaw(uint8_t index);

    /// 检查 Source 是否支持 PPS
    bool isSourcePPSCapable();

    /// 获取 PPS 电压范围: 最小 (V)
    float getPPSMinVoltage();

    /// 获取 PPS 电压范围: 最大 (V)
    float getPPSMaxVoltage();

    /// 获取 PPS 最大电流 (A)
    float getPPSMaxCurrent();

    // --- CC 引脚状态 ---

    /// 获取当前 CC 引脚 (0=无, 1=CC1, 2=CC2)
    uint8_t getCCPin();

    /// 获取 CC1 电平 (0~3)
    uint8_t getCC1Level();

    /// 获取 CC2 电平 (0~3)
    uint8_t getCC2Level();

    // --- 统计信息 ---

    /// 总数据包计数
    uint32_t getPacketCount();

    /// GoodCRC 计数 (在被动模式下应为0或极少)
    uint32_t getGoodCRCCount();

    /// Reject 消息计数
    uint32_t getRejectCount();

    /// 最近消息类型名称
    const char * getLastMessageName();

    /// 运行时间 (ms)
    uint32_t getUptime();

    // --- PPS 实时状态 (来自 PPS_Status) ---

    /// PPS 实时输出电压 (V, 来自 PPS_Status 消息)
    float getPPSOutputVoltage();

    /// PPS 实时输出电流 (A, 来自 PPS_Status 消息)
    float getPPSOutputCurrent();

    /// PPS 温度标志 (0=不支持, 1=正常, 2=警告, 3=过温)
    uint8_t getPPSTempFlag();

    // --- 重置 ---

    /// 重置所有统计和状态 (保持连接)
    void resetStats();

    // --- 底层访问 (高级用户) ---

    /// 获取 FUSB302 驱动指针
    FUSB302_Driver * getDriver() { return &_dev; }

    /**
     * @brief 使能/禁用 VBUS 电压检测
     * 
     * FUSB302 硬件 VBUS 阈值固定 ~4V。PPS 电压降到 4V 以下时,
     * 必须关闭 VBUS 检测, 否则芯片误认为 VBUS 断开导致状态重置。
     * 
     * 默认: Sniffer 初始化时自动关闭 (PPS 兼容)
     * 
     * @param enable true=使能, false=禁用
     */
    void enableVBUSSense(bool enable);

private:
    // FUSB302 设备
    FUSB302_Driver _dev;
    uint8_t _intPin;
    bool _initialized;

    // 回调
    PD_SnifferCallback _callback;
    void * _callbackData;

    // CC 状态
    bool   _connected;
    uint8_t _ccPin;
    uint8_t _cc1Level;
    uint8_t _cc2Level;

    // Source Capabilities
    uint32_t _sourcePDOs[PD_SNIFFER_MAX_PDO];
    uint8_t  _sourcePDOCount;
    bool     _sourcePPSCapable;
    uint8_t  _ppsPDOIndex;          // PPS APDO 在 PDO 列表中的索引
    uint16_t _ppsMinVoltage;        // PPS 最小电压 (50mV)
    uint16_t _ppsMaxVoltage;        // PPS 最大电压 (50mV)
    uint16_t _ppsMaxCurrent;        // PPS 最大电流 (50mA)

    // 电源状态
    PD_PowerStatus _powerStatus;
    uint16_t _voltage;              // 统一用 mV 存储
    uint16_t _current;              // 统一用 mA 存储
    uint8_t  _selectedPosition;

    // PPS 实时状态 (来自 PPS_Status)
    uint16_t _ppsOutputVoltage;     // 20mV 单位
    uint8_t  _ppsOutputCurrent;     // 50mA 单位
    uint8_t  _ppsTempFlag;

    // 统计
    uint32_t _packetCount;
    uint32_t _goodCRCCount;
    uint32_t _rejectCount;
    uint32_t _startTime;
    uint16_t _lastHeader;

    // I2C 回调 (静态)
    static int _i2cRead(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count);
    static int _i2cWrite(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count);
    static int _delayMs(uint32_t ms);

    // 内部方法
    void _handleAttached();
    void _handleDetached();
    void _handleRXMessage(uint16_t header, uint32_t *data);
    void _handleSourceCap(uint16_t header, uint32_t *data);
    void _handleRequest(uint16_t header, uint32_t *data);
    void _handlePSRDY();
    void _handlePPSStatus(uint32_t *data);
    void _fireEvent(PD_SnifferEvent event);
    void _updatePowerFromRequest(const PD_RequestInfo *req);
};

#endif // __cplusplus
#endif // PD_SNIFFER_H
