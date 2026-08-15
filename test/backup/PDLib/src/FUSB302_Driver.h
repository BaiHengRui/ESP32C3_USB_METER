/**
 * @file    FUSB302_Driver.h
 * @brief   FUSB302 底层硬件驱动 — 支持被动监听(Passive)和主动(Active)两种工作模式
 * 
 * 设计要点:
 *   - 纯 C 接口, 不依赖 Arduino/平台特定API
 *   - 通过回调函数指针注入 I2C 和延时实现, 实现平台无关
 *   - 支持两种工作模式:
 *     * MODE_PASSIVE: 纯监听, 不发送任何消息, 不响应 GoodCRC — 用于 PD 桥接/嗅探
 *     * MODE_ACTIVE:  正常 PD 通信, 自动 GoodCRC, 可收发消息 — 用于 PD Sink/Source
 *   - 清晰的错误码和事件系统
 * 
 * 用法示例 (被动监听):
 * @code
 *   FUSB302_Driver dev;
 *   fusb302_init(&dev, FUSB302_I2C_ADDR, my_i2c_read, my_i2c_write, my_delay_ms);
 *   fusb302_set_mode(&dev, FUSB302_MODE_PASSIVE);
 *   
 *   void loop() {
 *       FUSB302_Event events;
 *       fusb302_poll(&dev, &events);
 *       if (events & FUSB302_EVT_RX_SOP) {
 *           uint16_t header; uint32_t data[7];
 *           fusb302_read_message(&dev, &header, data);
 *           // 解析 PD 消息...
 *       }
 *   }
 * @endcode
 */

#ifndef FUSB302_DRIVER_H
#define FUSB302_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 一、类型定义
// ============================================================================

/// I2C 读回调: 从 reg_addr 读取 count 字节到 data
typedef int (*fusb302_i2c_read_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t count);

/// I2C 写回调: 向 reg_addr 写入 count 字节
typedef int (*fusb302_i2c_write_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t count);

/// 延时回调: 延时 t 毫秒
typedef int (*fusb302_delay_ms_t)(uint32_t t);

/// 返回值类型: 0=成功, 非0=错误
typedef uint8_t FUSB302_Status;

/// 工作模式枚举
typedef enum {
    FUSB302_MODE_PASSIVE = 0,   ///< 被动监听模式: 不发送, 不响应 GoodCRC, 不干扰 PD 总线
    FUSB302_MODE_ACTIVE  = 1    ///< 主动模式: 正常 PD 通信, 自动 GoodCRC 响应
} FUSB302_Mode;

/// PD 事件标志位 (使用 uint8_t 位掩码, 避免 C++ enum 位运算类型问题)
typedef uint8_t FUSB302_Event;

#define FUSB302_EVT_NONE            ((FUSB302_Event)0)
#define FUSB302_EVT_ATTACHED        ((FUSB302_Event)(1 << 0))  ///< CC 连接建立 (Source 被检测到)
#define FUSB302_EVT_DETACHED        ((FUSB302_Event)(1 << 1))  ///< CC 连接断开
#define FUSB302_EVT_RX_SOP          ((FUSB302_Event)(1 << 2))  ///< 收到 SOP 消息
#define FUSB302_EVT_GOOD_CRC_SENT   ((FUSB302_Event)(1 << 3))  ///< GoodCRC 已发送
#define FUSB302_EVT_HARD_RESET      ((FUSB302_Event)(1 << 4))  ///< 收到 Hard Reset

/// 错误码定义
enum {
    FUSB302_OK                  = 0,
    FUSB302_ERR_PARAM           = 1,    ///< 参数无效
    FUSB302_ERR_DEVICE_ID       = 2,    ///< 设备ID 不匹配
    FUSB302_ERR_I2C_READ        = 3,    ///< I2C 读取失败
    FUSB302_ERR_I2C_WRITE       = 4,    ///< I2C 写入失败
    FUSB302_ERR_BUSY            = 5,    ///< 设备忙 (CC 读数不稳定等)
    FUSB302_ERR_NOT_INIT        = 6,    ///< 设备未初始化
};

/// CC 电平值 (对应 BC_LVL 编码)
typedef enum {
    FUSB302_CC_VRA      = 0,    ///< <200mV    — 无连接
    FUSB302_CC_VRD_USB  = 1,    ///< 200~660mV — 默认 USB 能力
    FUSB302_CC_VRD_1A5  = 2,    ///< 660~1.23V — 1.5A 能力
    FUSB302_CC_VRD_3A0  = 3,    ///< >1.23V    — 3.0A 能力
} FUSB302_CCLevel;

/// 通信 CC 引脚
typedef enum {
    FUSB302_CC_NONE = 0,        ///< 未确定
    FUSB302_CC_CC1  = 1,        ///< 使用 CC1
    FUSB302_CC_CC2  = 2,        ///< 使用 CC2
} FUSB302_CCPin;

/// 设备状态机
typedef enum {
    FUSB302_STATE_UNATTACHED = 0,   ///< 未连接
    FUSB302_STATE_ATTACHED   = 1,   ///< 已连接
} FUSB302_State;

// ============================================================================
// 二、设备结构体
// ============================================================================

/// FUSB302 设备上下文 (用户分配, 通过指针传递)
typedef struct {
    /* --- 用户配置 --- */
    uint8_t             i2c_address;       ///< I2C 设备地址 (默认 0x22)
    fusb302_i2c_read_t  i2c_read;          ///< I2C 读回调
    fusb302_i2c_write_t i2c_write;         ///< I2C 写回调
    fusb302_delay_ms_t  delay_ms;          ///< 延时回调
    FUSB302_Mode        mode;              ///< 工作模式 (PASSIVE/ACTIVE)

    /* --- 内部状态 (用户不应直接访问) --- */
    uint8_t             reg_cache[12];     ///< 读写寄存器缓存 (0x01~0x0C)
    uint8_t             rx_buffer[32];     ///< RX FIFO 数据缓存
    uint16_t            rx_header;         ///< 最近收到的消息头
    uint8_t             cc1_level;         ///< CC1 检测电平 (0~3)
    uint8_t             cc2_level;         ///< CC2 检测电平 (0~3)
    FUSB302_CCPin       active_cc;         ///< 当前活动的 CC 引脚
    FUSB302_State       state;             ///< 设备状态
    bool                vbus_present;      ///< VBUS 是否有效
    bool                vbus_sense;        ///< VBUS 检测使能 (PPS<4V时需关闭)
    const char *        err_msg;           ///< 最后一次错误消息

    /* --- 中断累积 --- */
    uint8_t             inta;              ///< 中断A 累积
    uint8_t             intb;              ///< 中断B 累积
} FUSB302_Driver;

// ============================================================================
// 三、API 函数
// ============================================================================

/**
 * @brief 初始化 FUSB302 设备
 * @param dev       设备上下文指针
 * @param i2c_addr  I2C 地址 (默认 FUSB302_I2C_ADDR = 0x22)
 * @param read_fn   I2C 读回调
 * @param write_fn  I2C 写回调
 * @param delay_fn  毫秒延时回调
 * @return FUSB302_OK 成功, 其他值表示错误
 */
FUSB302_Status fusb302_init(FUSB302_Driver *dev, uint8_t i2c_addr,
                            fusb302_i2c_read_t read_fn,
                            fusb302_i2c_write_t write_fn,
                            fusb302_delay_ms_t delay_fn);

/**
 * @brief 设置工作模式
 * 
 * 被动模式: 不发送任何消息, 不响应 GoodCRC, 适合 PD 嗅探/桥接
 * 主动模式: 正常 PD 通信, 自动 GoodCRC, 适合 PD Sink/Source
 * 
 * @param dev  设备上下文
 * @param mode 工作模式
 * @note 必须在 fusb302_init() 之后调用, 建议在连接建立前设置
 */
void fusb302_set_mode(FUSB302_Driver *dev, FUSB302_Mode mode);

/**
 * @brief 轮询处理 FUSB302 事件 (在每个主循环中调用)
 * 
 * 内部根据设备状态 (UNATTACHED/ATTACHED) 自动切换处理逻辑:
 *   - UNATTACHED: 检测 CC 引脚上的 VBUS 和 Rd 电平
 *   - ATTACHED:   读取中断, 接收消息等
 * 
 * @param dev    设备上下文
 * @param events 输出: 本次触发的事件标志位组合
 * @return FUSB302_OK 成功
 */
FUSB302_Status fusb302_poll(FUSB302_Driver *dev, FUSB302_Event *events);

/**
 * @brief 读取最近收到的 PD 消息
 * @param dev    设备上下文
 * @param header 输出: 16位消息头
 * @param data   输出: 数据对象数组 (至少 7*4 字节)
 * @note 必须在收到 FUSB302_EVT_RX_SOP 事件后调用
 */
void fusb302_read_message(FUSB302_Driver *dev, uint16_t *header, uint32_t *data);

/**
 * @brief 发送 SOP 消息 (仅主动模式有效)
 * @param dev       设备上下文
 * @param header    16位消息头
 * @param data      数据对象数组 (可为 NULL)
 * @param obj_count 数据对象数量 (0~7)
 * @return FUSB302_OK 成功
 */
FUSB302_Status fusb302_send_message(FUSB302_Driver *dev, uint16_t header,
                                    const uint32_t *data, uint8_t obj_count);

/**
 * @brief 发送 Hard Reset (仅主动模式)
 */
FUSB302_Status fusb302_send_hard_reset(FUSB302_Driver *dev);

/**
 * @brief 获取 CC 引脚状态
 * @param dev  设备上下文
 * @param cc1  输出: CC1 电平 (0~3)
 * @param cc2  输出: CC2 电平 (0~3)
 */
void fusb302_get_cc_levels(FUSB302_Driver *dev, uint8_t *cc1, uint8_t *cc2);

/**
 * @brief 获取活动的 CC 引脚
 * @return FUSB302_CC_NONE / FUSB302_CC_CC1 / FUSB302_CC_CC2
 */
FUSB302_CCPin fusb302_get_active_cc(FUSB302_Driver *dev);

/**
 * @brief 读取设备ID (版本和修订)
 * @param dev     设备上下文
 * @param version 输出: 版本ID (0~7)
 * @param rev     输出: 修订ID (0~15)
 */
FUSB302_Status fusb302_get_device_id(FUSB302_Driver *dev, uint8_t *version, uint8_t *rev);

/**
 * @brief 获取 VBUS 状态
 * @return true=VBUS 有效 (>4V), false=VBUS 无效
 */
bool fusb302_get_vbus(FUSB302_Driver *dev);

/**
 * @brief 获取最后一次错误消息
 */
const char * fusb302_get_error(FUSB302_Driver *dev);

/**
 * @brief PD 协议层软复位
 */
void fusb302_pd_reset(FUSB302_Driver *dev);

/**
 * @brief 使能/禁用 VBUS 检测
 * 
 * FUSB302 硬件 VBUS 阈值固定为 ~4V。当 PPS 电压 <4V 时必须关闭 VBUS 检测,
 * 否则芯片误判 VBUS 断开, 触发 DETACHED 事件导致状态重置。
 * 
 * @param dev    设备上下文
 * @param enable true=使能检测, false=禁用 (PPS低电压场景)
 */
void fusb302_enable_vbus_sense(FUSB302_Driver *dev, bool enable);

#ifdef __cplusplus
}
#endif

#endif // FUSB302_DRIVER_H
