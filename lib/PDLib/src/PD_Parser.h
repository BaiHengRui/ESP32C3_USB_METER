/**
 * @file    PD_Parser.h
 * @brief   USB PD 协议消息解析器 — 纯函数, 无状态, 平台无关
 * 
 * 提供 USB PD 消息的解析功能, 包括:
 *   - 消息头解析 (Message Type, Specification Revision, MessageID, NumObjects)
 *   - Source Capabilities PDO 解析 (Fixed/Battery/Variable/Augmented)
 *   - Request 消息解析 (提取请求的电压/电流/位置)
 *   - PPS_Status 消息解析 (提取实时输出电压/电流/标志)
 *   - Sink Capabilities PDO 解析
 * 
 * 所有函数均为纯函数, 不持有任何状态, 可安全地在中断中调用。
 * 
 * 参考: USB PD R3.0 V2.0 + ECNs
 */

#ifndef PD_PARSER_H
#define PD_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 一、PD 常量定义
// ============================================================================

/// PD 消息类型 (Data Message)
#define PD_DATA_SOURCE_CAP          0x01    ///< Source Capabilities
#define PD_DATA_REQUEST             0x02    ///< Request
#define PD_DATA_BIST                0x03    ///< BIST
#define PD_DATA_SINK_CAP            0x04    ///< Sink Capabilities
#define PD_DATA_BATTERY_STATUS      0x05    ///< Battery Status
#define PD_DATA_ALERT               0x06    ///< Alert
#define PD_DATA_VENDOR_DEFINED      0x0F    ///< Vendor Defined Message (VDM)

/// PD 消息类型 (Control Message)
#define PD_CTRL_GOOD_CRC            0x01    ///< GoodCRC
#define PD_CTRL_GOTO_MIN            0x02    ///< GotoMin
#define PD_CTRL_ACCEPT              0x03    ///< Accept
#define PD_CTRL_REJECT              0x04    ///< Reject
#define PD_CTRL_PING                0x05    ///< Ping
#define PD_CTRL_PS_RDY              0x06    ///< Power Supply Ready
#define PD_CTRL_GET_SOURCE_CAP      0x07    ///< Get Source Cap
#define PD_CTRL_GET_SINK_CAP        0x08    ///< Get Sink Cap
#define PD_CTRL_DR_SWAP             0x09    ///< Data Role Swap
#define PD_CTRL_PR_SWAP             0x0A    ///< Power Role Swap
#define PD_CTRL_VCONN_SWAP          0x0B    ///< VCONN Swap
#define PD_CTRL_WAIT                0x0C    ///< Wait
#define PD_CTRL_SOFT_RESET          0x0D    ///< Soft Reset
#define PD_CTRL_NOT_SUPPORTED       0x10    ///< Not Supported
#define PD_CTRL_GET_PPS_STATUS      0x14    ///< Get PPS Status (PD3.0)

/// PD 消息类型 (Extended Message)
#define PD_EXT_PPS_STATUS           0x0C    ///< PPS Status (PD3.0)

/// PDO 类型 (Bits 31..30)
#define PD_PDO_TYPE_FIXED           0       ///< Fixed Supply
#define PD_PDO_TYPE_BATTERY         1       ///< Battery
#define PD_PDO_TYPE_VARIABLE        2       ///< Variable Supply
#define PD_PDO_TYPE_AUGMENTED       3       ///< Augmented Power Data Object (PPS)

/// PPS 标志 (PTF: PPS Trigger Flag)
#define PPS_PTF_NOT_SUPPORT         0
#define PPS_PTF_NORMAL              1
#define PPS_PTF_WARNING             2
#define PPS_PTF_OVER_TEMP           3

/// PPS 标志 (OMF: Operating Mode Flag)
#define PPS_OMF_VOLTAGE             0       ///< 电压模式
#define PPS_OMF_CURRENT_LIMIT       1       ///< 电流限制模式

// ============================================================================
// 二、数据结构
// ============================================================================

/// 消息头解析结果
typedef struct {
    uint8_t  msg_type;          ///< 消息类型 (低5位)
    uint8_t  spec_rev;          ///< PD 规范版本 (0=1.0, 1=2.0, 2=3.0)
    uint8_t  message_id;        ///< 消息ID (0~7)
    uint8_t  num_objects;       ///< 数据对象数量 (0~7, 扩展消息表示总数据长度/4)
    bool     is_extended;       ///< 是否为扩展消息
    bool     is_data_msg;       ///< 是否为 Data Message (有数据对象)
    bool     is_ctrl_msg;       ///< 是否为 Control Message (无数据对象)
} PD_HeaderInfo;

/// PDO 解析结果 (统一表示所有类型)
typedef struct {
    uint8_t  pdo_type;          ///< PDO 类型 (0=Fixed, 1=Battery, 2=Variable, 3=Augmented/PPS)
    
    // 电压范围 (50mV 单位, PPS用100mV单位但内部转换为50mV)
    uint16_t min_voltage;       ///< 最小电压 (50mV 单位)
    uint16_t max_voltage;       ///< 最大电压 (50mV 单位)
    
    // 电流/功率
    uint16_t max_current;       ///< 最大电流 (10mA 单位, PPS为50mA)
    uint16_t max_power;         ///< 最大功率 (250mW 单位, 仅Battery类型)
    
    // PPS 专用字段
    bool     is_pps;            ///< 是否为 PPS APDO
    bool     pps_limited;       ///< PPS 供电有限 (需2级启动)
    
    // 其他标志
    bool     usb_comm_capable;  ///< USB 通信能力
    bool     dual_role_data;    ///< 双角色数据
    bool     unconstrained;     ///< 无约束功率
    
} PD_PDOInfo;

/// Request 消息解析结果
typedef struct {
    uint8_t  object_position;   ///< 请求的 PDO 位置 (1~7)
    bool     giveback;          ///< GiveBack 标志
    bool     cap_mismatch;      ///< 能力不匹配
    bool     usb_comm;          ///< USB 通信
    bool     no_suspend;        ///< 不支持挂起
    bool     unchunked_ext;     ///< 支持非分块扩展消息
    
    // 工作点
    bool     is_pps;            ///< 是否为 PPS 请求
    uint16_t operating_voltage; ///< 工作电压 (Fixed: 50mV单位, PPS: 20mV单位)
    uint16_t operating_current; ///< 工作电流 (Fixed: 10mA单位, PPS: 50mA单位)
} PD_RequestInfo;

/// PPS_Status 解析结果
typedef struct {
    uint16_t output_voltage;    ///< 实时输出电压 (20mV 单位), 0xFFFF = 不支持
    uint8_t  output_current;    ///< 实时输出电流 (50mA 单位), 0xFF = 不支持
    uint8_t  ptf;               ///< PPS Trigger Flag (0~3)
    uint8_t  omf;               ///< Operating Mode Flag (0=电压模式, 1=电流限制)
} PD_PPSStatus;

// ============================================================================
// 三、API 函数
// ============================================================================

/**
 * @brief 解析 PD 消息头
 * @param header  16位消息头
 * @param info    输出: 解析结果
 */
void pd_parse_header(uint16_t header, PD_HeaderInfo *info);

/**
 * @brief 解析单个 PDO (Power Data Object)
 * @param pdo_raw 32位原始 PDO 值
 * @param info    输出: 解析结果
 */
void pd_parse_pdo(uint32_t pdo_raw, PD_PDOInfo *info);

/**
 * @brief 解析 Request 消息的数据对象 (默认按 Fixed PDO 格式)
 * @param request_pdo  Request 消息的32位数据对象
 * @param info         输出: 解析结果
 * @note 默认按 Fixed/Variable 格式解析。若是 PPS APDO 请求, 请用 pd_parse_request_pps()
 */
void pd_parse_request(uint32_t request_pdo, PD_RequestInfo *info);

/**
 * @brief 按 PPS APDO 格式解析 Request 消息
 * @param request_pdo  Request 消息的32位数据对象
 * @param info         输出: 解析结果
 * @note 仅在确认目标 PDO 类型为 PD_PDO_TYPE_AUGMENTED 时调用
 */
void pd_parse_request_pps(uint32_t request_pdo, PD_RequestInfo *info);

/**
 * @brief 解析 PPS_Status 消息
 * @param data   扩展消息数据 (至少2个32位字)
 * @param status 输出: PPS 状态
 */
void pd_parse_pps_status(const uint32_t *data, PD_PPSStatus *status);

/**
 * @brief 从 PDO 获取电压范围的文本描述
 * @param pdo   已解析的 PDO 信息
 * @param buf   输出缓冲区
 * @param size  缓冲区大小
 * @return 写入的字符数
 */
int pd_pdo_to_string(const PD_PDOInfo *pdo, char *buf, int size);

/**
 * @brief 从 Request 获取工作点的文本描述
 * @param req   已解析的 Request 信息
 * @param buf   输出缓冲区
 * @param size  缓冲区大小
 * @return 写入的字符数
 */
int pd_request_to_string(const PD_RequestInfo *req, char *buf, int size);

/**
 * @brief 获取消息类型名称字符串
 * @param header 消息头
 * @return 消息类型名称 (如 "Source_Cap", "Request", "PS_RDY" 等)
 */
const char * pd_get_message_name(uint16_t header);

/**
 * @brief 判断消息是否为 Source Capabilities
 */
static inline bool pd_is_source_cap(uint16_t header) {
    return !(header & 0x8000) && ((header & 0x1F) == PD_DATA_SOURCE_CAP) && ((header >> 12) & 0x7);
}

/**
 * @brief 判断消息是否为 Request
 */
static inline bool pd_is_request(uint16_t header) {
    return !(header & 0x8000) && ((header & 0x1F) == PD_DATA_REQUEST) && ((header >> 12) & 0x7);
}

/**
 * @brief 判断消息是否为 PS_RDY
 */
static inline bool pd_is_ps_rdy(uint16_t header) {
    return !(header & 0x8000) && ((header & 0x1F) == PD_CTRL_PS_RDY) && !((header >> 12) & 0x7);
}

/**
 * @brief 判断消息是否为 PPS_Status (扩展消息)
 */
static inline bool pd_is_pps_status(uint16_t header) {
    return (header & 0x8000) && ((header & 0x1F) == PD_EXT_PPS_STATUS);
}

/**
 * @brief 判断消息是否为 Accept
 */
static inline bool pd_is_accept(uint16_t header) {
    return !(header & 0x8000) && ((header & 0x1F) == PD_CTRL_ACCEPT) && !((header >> 12) & 0x7);
}

/**
 * @brief 判断消息是否为 Reject
 */
static inline bool pd_is_reject(uint16_t header) {
    return !(header & 0x8000) && ((header & 0x1F) == PD_CTRL_REJECT) && !((header >> 12) & 0x7);
}

/**
 * @brief 电压单位转换: 50mV → 伏特 (float)
 */
static inline float pd_50mv_to_volts(uint16_t val) { return val * 0.05f; }

/**
 * @brief 电压单位转换: 20mV (PPS) → 伏特 (float)
 */
static inline float pd_20mv_to_volts(uint16_t val) { return val * 0.02f; }

/**
 * @brief 电流单位转换: 10mA → 安培 (float)
 */
static inline float pd_10ma_to_amps(uint16_t val) { return val * 0.01f; }

/**
 * @brief 电流单位转换: 50mA (PPS) → 安培 (float)
 */
static inline float pd_50ma_to_amps(uint16_t val) { return val * 0.05f; }

/**
 * @brief 构造 PD 消息头
 * @param type      消息类型
 * @param obj_count 数据对象数量
 * @param msg_id    消息ID (0~7)
 * @param extended  是否为扩展消息
 * @return 16位消息头
 */
static inline uint16_t pd_make_header(uint8_t type, uint8_t obj_count, uint8_t msg_id, bool extended) {
    return ((uint16_t)type) |
           ((uint16_t)(obj_count & 0x7) << 12) |
           ((uint16_t)(msg_id & 0x7) << 9) |
           (extended ? 0x8000 : 0) |
           ((uint16_t)0x2 << 6);  // Spec Rev 2.0
}

#ifdef __cplusplus
}
#endif

#endif // PD_PARSER_H
