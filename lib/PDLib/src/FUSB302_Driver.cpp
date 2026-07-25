/**
 * @file    FUSB302_Driver.cpp
 * @brief   FUSB302 底层硬件驱动实现
 * 
 * 关键设计:
 *   1. 被动模式 (MODE_PASSIVE):
 *      - 不启用 AUTO_CRC — 这是避免干扰 PD 握手的关键!
 *      - 不配置 TXCC1/TXCC2 — 不在 CC 线上发送任何数据
 *      - 仅使能 RX 通路 — 只接收不发送
 *      - VBUS 检测保留 (用于判断连接状态)
 *   
 *   2. 主动模式 (MODE_ACTIVE):
 *      - 标准 UFP 配置: AUTO_CRC + TXCC + 自动重试
 *      - 完整的消息收发能力
 * 
 * 状态机:
 *   UNATTACHED ──(VBUS OK + CC检测到Rd)──▶ ATTACHED
 *   ATTACHED   ──(VBUS 消失)───────────▶ UNATTACHED
 */

#include <string.h>
#include "FUSB302_Driver.h"
#include "FUSB302_Reg.h"

// ============================================================================
// 辅助宏: 寄存器访问
// ============================================================================

/// 读写寄存器在 reg_cache 中的偏移 (下标 = 寄存器地址 - 0x01)
#define REG_IDX(reg)    ((reg) - FUSB302_RW_REGS_START)

/// 获取 reg_cache 中的值
#define REG_VAL(dev, reg)   ((dev)->reg_cache[REG_IDX(reg)])

/// 批量读取只读状态寄存器 (0x3C~0x42, 共7字节) 到本地数组
static inline FUSB302_Status _read_status_regs(FUSB302_Driver *dev, uint8_t *buf) {
    int ret = dev->i2c_read(dev->i2c_address, FUSB302_RO_REGS_START, buf, FUSB302_RO_REGS_COUNT);
    if (ret != 0) {
        dev->err_msg = "I2C read status regs failed";
        return FUSB302_ERR_I2C_READ;
    }
    return FUSB302_OK;
}

/// 单个寄存器读取
static inline FUSB302_Status _reg_read(FUSB302_Driver *dev, uint8_t addr, uint8_t *val) {
    int ret = dev->i2c_read(dev->i2c_address, addr, val, 1);
    if (ret != 0) {
        dev->err_msg = "I2C read reg failed";
        return FUSB302_ERR_I2C_READ;
    }
    return FUSB302_OK;
}

/// 单个寄存器写入
static inline FUSB302_Status _reg_write(FUSB302_Driver *dev, uint8_t addr, uint8_t val) {
    int ret = dev->i2c_write(dev->i2c_address, addr, &val, 1);
    if (ret != 0) {
        dev->err_msg = "I2C write reg failed";
        return FUSB302_ERR_I2C_WRITE;
    }
    // 同步更新 reg_cache (如果在缓存范围内)
    if (addr >= FUSB302_RW_REGS_START && addr < FUSB302_RW_REGS_START + FUSB302_RW_REGS_COUNT) {
        dev->reg_cache[REG_IDX(addr)] = val;
    }
    return FUSB302_OK;
}

/// 批量写入并同步 reg_cache
static inline FUSB302_Status _reg_write_batch(FUSB302_Driver *dev, uint8_t start, const uint8_t *vals, uint8_t count) {
    int ret = dev->i2c_write(dev->i2c_address, start, (uint8_t *)vals, count);
    if (ret != 0) {
        dev->err_msg = "I2C write batch failed";
        return FUSB302_ERR_I2C_WRITE;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (start + i >= FUSB302_RW_REGS_START && start + i < FUSB302_RW_REGS_START + FUSB302_RW_REGS_COUNT) {
            dev->reg_cache[REG_IDX(start + i)] = vals[i];
        }
    }
    return FUSB302_OK;
}

// ============================================================================
// CC 电平读取 (重复读取直到稳定)
// ============================================================================

static FUSB302_Status _read_cc_level(FUSB302_Driver *dev, uint8_t *cc_val) {
    uint8_t status0, cc, cc_verify;
    
    if (_reg_read(dev, FUSB302_REG_STATUS0, &status0) != FUSB302_OK) return FUSB302_ERR_I2C_READ;
    cc = status0 & FUSB302_ST0_BC_LVL_MASK;
    
    // 重复读取5次确认稳定
    for (uint8_t i = 0; i < 5; i++) {
        dev->delay_ms(1);
        if (_reg_read(dev, FUSB302_REG_STATUS0, &status0) != FUSB302_OK) return FUSB302_ERR_I2C_READ;
        cc_verify = status0 & FUSB302_ST0_BC_LVL_MASK;
        if (cc != cc_verify) {
            dev->err_msg = "CC level unstable";
            return FUSB302_ERR_BUSY;
        }
    }
    *cc_val = cc;
    return FUSB302_OK;
}

// ============================================================================
// 消息接收: 从 FIFO 读取 SOP 消息
// ============================================================================

static FUSB302_Status _read_fifo_message(FUSB302_Driver *dev, FUSB302_Event *events) {
    uint8_t buf[3];
    uint8_t obj_count;
    
    // 读取前3字节: Token(丢弃) + Header[1:0]
    if (dev->i2c_read(dev->i2c_address, FUSB302_REG_FIFOS, buf, 3) != 0) {
        dev->err_msg = "FIFO read header failed";
        return FUSB302_ERR_I2C_READ;
    }
    
    dev->rx_header = ((uint16_t)buf[2] << 8) | buf[1];
    obj_count = (dev->rx_header >> 12) & 0x7;
    
    // 读取数据对象 + CRC (每对象4字节 + 4字节CRC)
    if (dev->i2c_read(dev->i2c_address, FUSB302_REG_FIFOS, dev->rx_buffer, obj_count * 4 + 4) != 0) {
        dev->err_msg = "FIFO read data failed";
        return FUSB302_ERR_I2C_READ;
    }
    
    if (events) *events |= FUSB302_EVT_RX_SOP;
    return FUSB302_OK;
}

// ============================================================================
// 状态处理: UNATTACHED — 检测连接, 测量CC电平
// ============================================================================

static FUSB302_Status _state_unattached(FUSB302_Driver *dev, FUSB302_Event *events) {
    uint8_t status0;
    
    // 检查 VBUS
    if (_reg_read(dev, FUSB302_REG_STATUS0, &status0) != FUSB302_OK) return FUSB302_ERR_I2C_READ;
    if (!(status0 & FUSB302_ST0_VBUSOK)) {
        dev->vbus_present = false;
        return FUSB302_OK;  // VBUS 未就绪, 继续等待
    }
    dev->vbus_present = true;
    
    // 使能内部电路: 带隙 + 接收器 + 测量 + 振荡器
    uint8_t pwr = FUSB302_PWR_BANDGAP | FUSB302_PWR_RECEIVER | FUSB302_PWR_MEASURE | FUSB302_PWR_INT_OSC;
    _reg_write(dev, FUSB302_REG_POWER, pwr);
    dev->delay_ms(1);
    
    // 被动 vs 主动模式: 都使用 PDWN 下拉确保接收电路正常工作
    //   关键差异仅在 SWITCHES1: 被动模式无 AUTO_CRC (不回复GoodCRC), 主动模式有
    //   被动模式也保留 PDWN 的原因: FUSB302 接收器需要 CC 引脚有正确的 DC 偏置
    uint8_t sw0_meas_cc1 = FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC1;
    uint8_t sw0_meas_cc2 = FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC2;
    
    uint8_t sw1_default = FUSB302_SW1_SPECREV0;
    uint8_t meas_default = FUSB302_MEAS_MDAC_DEFAULT;
    
    // --- 测量 CC1 ---
    uint8_t cfg1[3] = { sw0_meas_cc1, sw1_default, meas_default };
    _reg_write_batch(dev, FUSB302_REG_SWITCHES0, cfg1, 3);
    dev->delay_ms(1);
    
    while (_read_cc_level(dev, &dev->cc1_level) != FUSB302_OK) {
        dev->delay_ms(1);
    }
    
    // --- 测量 CC2 ---
    _reg_write(dev, FUSB302_REG_SWITCHES0, sw0_meas_cc2);
    dev->delay_ms(1);
    
    while (_read_cc_level(dev, &dev->cc2_level) != FUSB302_OK) {
        dev->delay_ms(1);
    }
    
    // --- 清空中断 ---
    uint8_t dummy[2];
    dev->i2c_read(dev->i2c_address, FUSB302_REG_INTERRUPTA, dummy, 2);
    dev->inta = 0;
    dev->intb = 0;
    
    // --- 配置通信 CC 引脚 ---
    // 关键差异: 被动模式 vs 主动模式
    if (dev->cc1_level > FUSB302_CC_VRA) {
        dev->active_cc = FUSB302_CC_CC1;
        
        if (dev->mode == FUSB302_MODE_PASSIVE) {
            // 被动/桥接模式: PDWN保留下拉(接收器偏置), 但禁用AUTO_CRC(不干扰握手)!
            // 与主动模式的关键区别: 无 AUTO_CRC, 无 TXCC — 只看不答
            uint8_t passive_cfg[3] = {
                FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC1,
                FUSB302_SW1_SPECREV0,            // SW1: 仅SPECREV, 无AUTO_CRC!无TX!
                FUSB302_MEAS_MDAC_DEFAULT
            };
            _reg_write_batch(dev, FUSB302_REG_SWITCHES0, passive_cfg, 3);
        } else {
            // 主动模式: 下拉 + AUTO_CRC + TX
            uint8_t active_cfg[3] = {
                FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC1,
                FUSB302_SW1_SPECREV0 | FUSB302_SW1_AUTO_CRC | FUSB302_SW1_TXCC1,
                FUSB302_MEAS_MDAC_DEFAULT
            };
            _reg_write_batch(dev, FUSB302_REG_SWITCHES0, active_cfg, 3);
        }
    } else if (dev->cc2_level > FUSB302_CC_VRA) {
        dev->active_cc = FUSB302_CC_CC2;
        
        if (dev->mode == FUSB302_MODE_PASSIVE) {
            uint8_t passive_cfg[3] = {
                FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC2,  // PDWN保留, 无AUTO_CRC
                FUSB302_SW1_SPECREV0,
                FUSB302_MEAS_MDAC_DEFAULT
            };
            _reg_write_batch(dev, FUSB302_REG_SWITCHES0, passive_cfg, 3);
        } else {
            uint8_t active_cfg[3] = {
                FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | FUSB302_SW0_MEAS_CC2,
                FUSB302_SW1_SPECREV0 | FUSB302_SW1_AUTO_CRC | FUSB302_SW1_TXCC2,
                FUSB302_MEAS_MDAC_DEFAULT
            };
            _reg_write_batch(dev, FUSB302_REG_SWITCHES0, active_cfg, 3);
        }
    } else {
        // 两个 CC 都无效
        dev->active_cc = FUSB302_CC_NONE;
        uint8_t idle_cfg[3] = {
            FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2,  // 保持下拉, 等待下次检测
            FUSB302_SW1_SPECREV0,
            FUSB302_MEAS_MDAC_DEFAULT
        };
        _reg_write_batch(dev, FUSB302_REG_SWITCHES0, idle_cfg, 3);
    }
    
    // 状态切换
    dev->state = FUSB302_STATE_ATTACHED;
    if (events) *events |= FUSB302_EVT_ATTACHED;
    
    return FUSB302_OK;
}

// ============================================================================
// 状态处理: ATTACHED — 处理中断, 接收消息
// ============================================================================

static FUSB302_Status _state_attached(FUSB302_Driver *dev, FUSB302_Event *events) {
    // 批量读取只读状态寄存器 (0x3C~0x42, 7字节)
    uint8_t ro[7];  // [0]=0x3C STATUS0A, [1]=0x3D STATUS1A, [2]=0x3E INTA, [3]=0x3F INTB, [4]=0x40 STATUS0, [5]=0x41 STATUS1, [6]=0x42 INTERRUPT
    if (_read_status_regs(dev, ro) != FUSB302_OK) return FUSB302_ERR_I2C_READ;
    
    uint8_t status0a = ro[0];
    uint8_t status1a = ro[1];
    uint8_t inta     = ro[2];
    uint8_t intb     = ro[3];
    uint8_t status0  = ro[4];
    uint8_t status1  = ro[5];
    
    // 累积中断
    dev->inta |= inta;
    dev->intb |= intb;
    
    // --- VBUS 断开检测 (仅在 vbus_sense 使能时) ---
    // PPS 电压可能 <4V (FUSB302 VBUS 阈值), 此时需关闭 VBUS 检测防止误触发
    if (dev->vbus_sense && !(status0 & FUSB302_ST0_VBUSOK)) {
        dev->vbus_present = false;
        // 复位到未连接状态 (保留下拉)
        uint8_t sw0 = FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2;
        uint8_t sw1 = FUSB302_SW1_SPECREV0;
        uint8_t meas = FUSB302_MEAS_MDAC_DEFAULT;
        uint8_t cfg[3] = { sw0, sw1, meas };
        _reg_write_batch(dev, FUSB302_REG_SWITCHES0, cfg, 3);
        
        uint8_t pwr = FUSB302_PWR_BANDGAP | FUSB302_PWR_RECEIVER | FUSB302_PWR_MEASURE;
        _reg_write(dev, FUSB302_REG_POWER, pwr);
        
        dev->state = FUSB302_STATE_UNATTACHED;
        dev->active_cc = FUSB302_CC_NONE;
        dev->cc1_level = 0;
        dev->cc2_level = 0;
        if (events) *events |= FUSB302_EVT_DETACHED;
        return FUSB302_OK;
    }
    
    // --- Hard Reset 处理 ---
    if (status0a & FUSB302_STA0A_HARDRST) {
        _reg_write(dev, FUSB302_REG_RESET, FUSB302_RESET_PD_RESET);
        if (events) *events |= FUSB302_EVT_HARD_RESET;
    }
    
    // --- GoodCRC 已发送 (仅主动模式会产生) ---
    if (dev->intb & FUSB302_INTB_GCRCSENT) {
        dev->intb &= ~FUSB302_INTB_GCRCSENT;
        if (events) *events |= FUSB302_EVT_GOOD_CRC_SENT;
    }
    
    // --- RX FIFO 有数据 ---
    if (!(status1 & FUSB302_ST1_RX_EMPTY)) {
        FUSB302_Status ret = _read_fifo_message(dev, events);
        if (ret != FUSB302_OK) {
            // 读取失败, 刷新 RX FIFO
            uint8_t ctrl1 = REG_VAL(dev, FUSB302_REG_CONTROL1) | FUSB302_CTRL1_RX_FLUSH;
            _reg_write(dev, FUSB302_REG_CONTROL1, ctrl1);
        }
    }
    
    return FUSB302_OK;
}

// ============================================================================
// 公开 API 实现
// ============================================================================

FUSB302_Status fusb302_init(FUSB302_Driver *dev, uint8_t i2c_addr,
                            fusb302_i2c_read_t read_fn,
                            fusb302_i2c_write_t write_fn,
                            fusb302_delay_ms_t delay_fn) {
    if (!dev) return FUSB302_ERR_PARAM;
    if (!read_fn || !write_fn || !delay_fn) return FUSB302_ERR_PARAM;
    
    // 清零设备结构体
    memset(dev, 0, sizeof(FUSB302_Driver));
    dev->i2c_address = i2c_addr;
    dev->i2c_read = read_fn;
    dev->i2c_write = write_fn;
    dev->delay_ms = delay_fn;
    dev->mode = FUSB302_MODE_PASSIVE;  // 默认被动模式 (最安全)
    dev->vbus_sense = true;            // 默认使能 VBUS 检测
    dev->state = FUSB302_STATE_UNATTACHED;
    
    // 读取设备ID
    uint8_t dev_id;
    if (_reg_read(dev, FUSB302_REG_DEVICE_ID, &dev_id) != FUSB302_OK) {
        dev->err_msg = "Device not found on I2C bus";
        return FUSB302_ERR_I2C_READ;
    }
    if (!(dev_id & FUSB302_DEVICE_ID_VALID_MASK)) {
        dev->err_msg = "Invalid device ID (not FUSB302)";
        return FUSB302_ERR_DEVICE_ID;
    }
    
    // 软件复位, 恢复默认值
    _reg_write(dev, FUSB302_REG_RESET, FUSB302_RESET_SW_RES);
    
    // 批量读取所有读写寄存器进缓存
    dev->i2c_read(dev->i2c_address, FUSB302_RW_REGS_START, dev->reg_cache, FUSB302_RW_REGS_COUNT);
    
    // --- 基础配置 (被动和主动模式共用) ---
    
    // SWITCHES0: CC1/CC2 都下拉 (保证接收器 DC 偏置正确)
    REG_VAL(dev, FUSB302_REG_SWITCHES0) = FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2;
    // SWITCHES1: PD规范版本 (不启用 AUTO_CRC/TX, 等检测到连接后根据模式决定)
    REG_VAL(dev, FUSB302_REG_SWITCHES1) = FUSB302_SW1_SPECREV0;
    // MEASURE: 默认基准电压
    REG_VAL(dev, FUSB302_REG_MEASURE) = FUSB302_MEAS_MDAC_DEFAULT;
    uint8_t init_cfg[3] = {
        REG_VAL(dev, FUSB302_REG_SWITCHES0),
        REG_VAL(dev, FUSB302_REG_SWITCHES1),
        REG_VAL(dev, FUSB302_REG_MEASURE)
    };
    _reg_write_batch(dev, FUSB302_REG_SWITCHES0, init_cfg, 3);
    
    // CONTROL0: 不屏蔽中断, 自动前导码
    REG_VAL(dev, FUSB302_REG_CONTROL0) &= ~FUSB302_CTRL0_INT_MASK;
    REG_VAL(dev, FUSB302_REG_CONTROL0) |= FUSB302_CTRL0_AUTO_PRE;
    _reg_write(dev, FUSB302_REG_CONTROL0, REG_VAL(dev, FUSB302_REG_CONTROL0));
    
    // CONTROL1: 使能 SOP 接收
    REG_VAL(dev, FUSB302_REG_CONTROL1) |= FUSB302_CTRL1_ENSOP1;
    _reg_write(dev, FUSB302_REG_CONTROL1, REG_VAL(dev, FUSB302_REG_CONTROL1));
    
    // CONTROL2: UFP 模式
    REG_VAL(dev, FUSB302_REG_CONTROL2) &= ~FUSB302_CTRL2_MODE_MASK;
    REG_VAL(dev, FUSB302_REG_CONTROL2) |= FUSB302_CTRL2_MODE_UFP;
    _reg_write(dev, FUSB302_REG_CONTROL2, REG_VAL(dev, FUSB302_REG_CONTROL2));
    
    // CONTROL3: 自动重试3次
    REG_VAL(dev, FUSB302_REG_CONTROL3) &= ~FUSB302_CTRL3_N_RETRIES_MASK;
    REG_VAL(dev, FUSB302_REG_CONTROL3) |= FUSB302_CTRL3_N_RETRIES(3) | FUSB302_CTRL3_AUTO_RETRY;
    _reg_write(dev, FUSB302_REG_CONTROL3, REG_VAL(dev, FUSB302_REG_CONTROL3));
    
    // MASK: 只使能我们关心的中断
    REG_VAL(dev, FUSB302_REG_MASK) = 0xFF;
    REG_VAL(dev, FUSB302_REG_MASK) &= ~(FUSB302_MASK_VBUSOK | FUSB302_MASK_CRC_CHK);
    _reg_write(dev, FUSB302_REG_MASK, REG_VAL(dev, FUSB302_REG_MASK));
    
    // MASKA
    REG_VAL(dev, FUSB302_REG_MASKA) = 0xFF;
    REG_VAL(dev, FUSB302_REG_MASKA) &= ~(FUSB302_MASKA_RETRYFAIL | FUSB302_MASKA_HARDSENT | FUSB302_MASKA_TXSENT | FUSB302_MASKA_HARDRST);
    _reg_write(dev, FUSB302_REG_MASKA, REG_VAL(dev, FUSB302_REG_MASKA));
    
    // MASKB: 默认屏蔽 GCRCSENT (被动模式不需要)
    REG_VAL(dev, FUSB302_REG_MASKB) = 0xFF;
    _reg_write(dev, FUSB302_REG_MASKB, REG_VAL(dev, FUSB302_REG_MASKB));
    
    // POWER: 开启基本电路 (先用省电模式, 等检测到 VBUS 后再全开)
    REG_VAL(dev, FUSB302_REG_POWER) = FUSB302_PWR_BANDGAP | FUSB302_PWR_RECEIVER | FUSB302_PWR_MEASURE;
    _reg_write(dev, FUSB302_REG_POWER, REG_VAL(dev, FUSB302_REG_POWER));
    
    dev->err_msg = "";
    return FUSB302_OK;
}

void fusb302_set_mode(FUSB302_Driver *dev, FUSB302_Mode mode) {
    if (!dev) return;
    
    dev->mode = mode;
    
    if (mode == FUSB302_MODE_PASSIVE) {
        // 被动模式: 确保 MASKB 屏蔽 GCRCSENT (虽然被动模式下不会产生, 但双重保险)
        REG_VAL(dev, FUSB302_REG_MASKB) |= FUSB302_MASKB_GCRCSENT;
        _reg_write(dev, FUSB302_REG_MASKB, REG_VAL(dev, FUSB302_REG_MASKB));
    } else {
        // 主动模式: 使能 GCRCSENT 中断
        REG_VAL(dev, FUSB302_REG_MASKB) &= ~FUSB302_MASKB_GCRCSENT;
        _reg_write(dev, FUSB302_REG_MASKB, REG_VAL(dev, FUSB302_REG_MASKB));
    }
}

FUSB302_Status fusb302_poll(FUSB302_Driver *dev, FUSB302_Event *events) {
    if (!dev) return FUSB302_ERR_PARAM;
    
    if (events) *events = FUSB302_EVT_NONE;
    
    // 根据状态分发
    switch (dev->state) {
        case FUSB302_STATE_UNATTACHED:
            return _state_unattached(dev, events);
        case FUSB302_STATE_ATTACHED:
            return _state_attached(dev, events);
        default:
            dev->state = FUSB302_STATE_UNATTACHED;
            return FUSB302_OK;
    }
}

void fusb302_read_message(FUSB302_Driver *dev, uint16_t *header, uint32_t *data) {
    if (!dev) return;
    
    if (header) *header = dev->rx_header;
    if (data) {
        uint8_t obj_count = (dev->rx_header >> 12) & 0x7;
        memcpy(data, dev->rx_buffer, obj_count * 4);
    }
}

FUSB302_Status fusb302_send_message(FUSB302_Driver *dev, uint16_t header,
                                    const uint32_t *data, uint8_t obj_count) {
    if (!dev) return FUSB302_ERR_PARAM;
    if (dev->mode != FUSB302_MODE_ACTIVE) {
        dev->err_msg = "Cannot send in passive mode";
        return FUSB302_ERR_PARAM;
    }
    if (obj_count > 7) obj_count = 7;
    
    // 构造 TX FIFO 数据
    uint8_t buf[40];
    uint8_t *p = buf;
    
    // SOP 序列 (3字节)
    *p++ = FUSB302_TX_TOKEN_SOP1;
    *p++ = FUSB302_TX_TOKEN_SOP1;
    *p++ = FUSB302_TX_TOKEN_SOP1;
    *p++ = FUSB302_TX_TOKEN_SOP2;
    
    // PACKSYM + 数据长度
    *p++ = FUSB302_TX_TOKEN_PACKSYM | ((obj_count << 2) + 2);
    
    // Header (低字节在前)
    *p++ = header & 0xFF;
    *p++ = (header >> 8) & 0xFF;
    
    // 数据对象
    if (data) {
        for (uint8_t i = 0; i < obj_count; i++) {
            uint32_t d = data[i];
            *p++ = d & 0xFF;
            *p++ = (d >> 8) & 0xFF;
            *p++ = (d >> 16) & 0xFF;
            *p++ = (d >> 24) & 0xFF;
        }
    }
    
    // CRC 和结束符
    *p++ = FUSB302_TX_TOKEN_JAM_CRC;
    *p++ = FUSB302_TX_TOKEN_EOP;
    *p++ = FUSB302_TX_TOKEN_TXOFF;
    *p++ = FUSB302_TX_TOKEN_TXON;
    
    // 写入 FIFO
    int ret = dev->i2c_write(dev->i2c_address, FUSB302_REG_FIFOS, buf, p - buf);
    if (ret != 0) {
        dev->err_msg = "TX FIFO write failed";
        return FUSB302_ERR_I2C_WRITE;
    }
    
    dev->delay_ms(1);
    return FUSB302_OK;
}

FUSB302_Status fusb302_send_hard_reset(FUSB302_Driver *dev) {
    if (!dev) return FUSB302_ERR_PARAM;
    if (dev->mode != FUSB302_MODE_ACTIVE) return FUSB302_ERR_PARAM;
    
    uint8_t ctrl3 = REG_VAL(dev, FUSB302_REG_CONTROL3) | FUSB302_CTRL3_SEND_HARDRST;
    _reg_write(dev, FUSB302_REG_CONTROL3, ctrl3);
    dev->delay_ms(5);
    _reg_write(dev, FUSB302_REG_RESET, FUSB302_RESET_PD_RESET);
    return FUSB302_OK;
}

void fusb302_get_cc_levels(FUSB302_Driver *dev, uint8_t *cc1, uint8_t *cc2) {
    if (!dev) return;
    if (cc1) *cc1 = dev->cc1_level;
    if (cc2) *cc2 = dev->cc2_level;
}

FUSB302_CCPin fusb302_get_active_cc(FUSB302_Driver *dev) {
    if (!dev) return FUSB302_CC_NONE;
    return dev->active_cc;
}

FUSB302_Status fusb302_get_device_id(FUSB302_Driver *dev, uint8_t *version, uint8_t *rev) {
    if (!dev) return FUSB302_ERR_PARAM;
    
    uint8_t dev_id = REG_VAL(dev, FUSB302_REG_DEVICE_ID);
    if (!(dev_id & FUSB302_DEVICE_ID_VALID_MASK)) return FUSB302_ERR_DEVICE_ID;
    
    if (version) *version = (dev_id >> 4) & 0x07;
    if (rev)     *rev     = dev_id & 0x0F;
    return FUSB302_OK;
}

bool fusb302_get_vbus(FUSB302_Driver *dev) {
    if (!dev) return false;
    return dev->vbus_present;
}

const char * fusb302_get_error(FUSB302_Driver *dev) {
    if (!dev) return "NULL device";
    return dev->err_msg;
}

void fusb302_pd_reset(FUSB302_Driver *dev) {
    if (!dev) return;
    _reg_write(dev, FUSB302_REG_RESET, FUSB302_RESET_PD_RESET);
}

void fusb302_enable_vbus_sense(FUSB302_Driver *dev, bool enable) {
    if (!dev) return;
    
    if (dev->vbus_sense != enable) {
        if (enable) {
            // 使能 VBUSOK 中断 (清 MASK 位)
            REG_VAL(dev, FUSB302_REG_MASK) &= ~FUSB302_MASK_VBUSOK;
        } else {
            // 禁用 VBUSOK 中断 (置 MASK 位) — PPS<4V 时使用
            REG_VAL(dev, FUSB302_REG_MASK) |= FUSB302_MASK_VBUSOK;
        }
        _reg_write(dev, FUSB302_REG_MASK, REG_VAL(dev, FUSB302_REG_MASK));
        dev->vbus_sense = enable;
    }
}
