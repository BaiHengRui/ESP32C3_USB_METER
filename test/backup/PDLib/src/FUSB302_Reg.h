/**
 * @file    FUSB302_Reg.h
 * @brief   FUSB302 寄存器地址与位掩码定义
 * 
 * 基于 Onsemi FUSB302 数据手册, 将所有寄存器地址和位定义集中管理,
 * 方便底层驱动和上层协议解析使用。
 * 
 * 参考: FUSB302 Datasheet Rev.5, 2017
 */

#ifndef FUSB302_REG_H
#define FUSB302_REG_H

#include <stdint.h>

// ============================================================================
// 一、I2C 设备地址
// ============================================================================
#define FUSB302_I2C_ADDR            0x22    ///< FUSB302 默认 I2C 地址 (7-bit)

// ============================================================================
// 二、寄存器地址 (共 19 个寄存器)
// ============================================================================
#define FUSB302_REG_DEVICE_ID       0x01    ///< 设备ID / 版本号
#define FUSB302_REG_SWITCHES0       0x02    ///< CC引脚开关控制
#define FUSB302_REG_SWITCHES1       0x03    ///< 模式/CRC/CC选择
#define FUSB302_REG_MEASURE         0x04    ///< CC 测量基准电压
#define FUSB302_REG_SLICE           0x05    ///< 比较器切片控制
#define FUSB302_REG_CONTROL0        0x06    ///< TX控制 / 中断屏蔽
#define FUSB302_REG_CONTROL1        0x07    ///< RX控制 / SOP检测
#define FUSB302_REG_CONTROL2        0x08    ///< 工作模式 (UFP/DFP/DRP)
#define FUSB302_REG_CONTROL3        0x09    ///< 自动重试 / Hard Reset
#define FUSB302_REG_MASK            0x0A    ///< 主中断屏蔽
#define FUSB302_REG_POWER           0x0B    ///< 电源控制
#define FUSB302_REG_RESET           0x0C    ///< 复位控制
#define FUSB302_REG_OCPREG          0x0D    ///< OCP 阈值
#define FUSB302_REG_MASKA           0x0E    ///< 中断A屏蔽
#define FUSB302_REG_MASKB           0x0F    ///< 中断B屏蔽
#define FUSB302_REG_CONTROL4        0x10    ///< 额外控制
#define FUSB302_REG_STATUS0A        0x3C    ///< 状态0A (只读)
#define FUSB302_REG_STATUS1A        0x3D    ///< 状态1A (只读)
#define FUSB302_REG_INTERRUPTA      0x3E    ///< 中断A (只读, 读后自动清除)
#define FUSB302_REG_INTERRUPTB      0x3F    ///< 中断B (只读, 读后自动清除)
#define FUSB302_REG_STATUS0         0x40    ///< 状态0 (只读)
#define FUSB302_REG_STATUS1         0x41    ///< 状态1 (只读)
#define FUSB302_REG_INTERRUPT       0x42    ///< 主中断状态 (只读)
#define FUSB302_REG_FIFOS           0x43    ///< FIFO 读写端口

// 读写寄存器块 (0x01~0x0C, 共12字节, 用于批量读写)
#define FUSB302_RW_REGS_START       0x01
#define FUSB302_RW_REGS_COUNT       12

// 只读状态寄存器块 (0x3C~0x42, 共7字节, 用于批量读取)
#define FUSB302_RO_REGS_START       0x3C
#define FUSB302_RO_REGS_COUNT       7

// ============================================================================
// 三、各寄存器位域定义
// ============================================================================

// --- DEVICE_ID (0x01) ---
#define FUSB302_DEVICE_ID_MASK      0xF0    ///< 高4位: 设备版本ID (应为 0b1000~0b1011)
#define FUSB302_DEVICE_REV_MASK     0x0F    ///< 低4位: 修订版本

// --- SWITCHES0 (0x02) ---
#define FUSB302_SW0_PU_EN2          (1<<7)  ///< CC2 上拉电流源使能
#define FUSB302_SW0_PU_EN1          (1<<6)  ///< CC1 上拉电流源使能
#define FUSB302_SW0_VCONN_CC2       (1<<5)  ///< CC2 VCONN 供电
#define FUSB302_SW0_VCONN_CC1       (1<<4)  ///< CC1 VCONN 供电
#define FUSB302_SW0_MEAS_CC2        (1<<3)  ///< 测量 CC2 电压
#define FUSB302_SW0_MEAS_CC1        (1<<2)  ///< 测量 CC1 电压
#define FUSB302_SW0_PDWN2           (1<<1)  ///< CC2 下拉电阻 (Rd)
#define FUSB302_SW0_PDWN1           (1<<0)  ///< CC1 下拉电阻 (Rd)

// --- SWITCHES1 (0x03) ---
#define FUSB302_SW1_POWERROLE       (1<<7)  ///< 电源角色 (0=Sink, 1=Source)
#define FUSB302_SW1_SPECREV1        (1<<6)  ///< PD 规范版本 [1:0]
#define FUSB302_SW1_SPECREV0        (1<<5)
#define FUSB302_SW1_DATAROLE        (1<<4)  ///< 数据角色 (0=UFP, 1=DFP)
#define FUSB302_SW1_AUTO_CRC        (1<<2)  ///< ⚠️ 自动GoodCRC: 桥接模式必须禁用以避免干扰!
#define FUSB302_SW1_TXCC2           (1<<1)  ///< 在 CC2 上发送
#define FUSB302_SW1_TXCC1           (1<<0)  ///< 在 CC1 上发送

// --- MEASURE (0x04) ---
#define FUSB302_MEAS_VBUS           (1<<6)  ///< 测量 VBUS 电压
#define FUSB302_MEAS_MDAC_MASK      0x3F    ///< CC比较器基准电压 (0~63, 每步 42mV)
#define FUSB302_MEAS_MDAC_DEFAULT   49      ///< 默认值 ~2.06V (适合 Rd 检测)

// --- CONTROL0 (0x06) ---
#define FUSB302_CTRL0_TX_FLUSH      (1<<6)  ///< 刷新 TX FIFO
#define FUSB302_CTRL0_INT_MASK      (1<<5)  ///< 中断输出屏蔽 (0=使能INT引脚)
#define FUSB302_CTRL0_HOST_CUR_MASK (3<<2)  ///< 广播电流能力 [3:2]
#define FUSB302_CTRL0_HOST_CUR_3A0  (3<<2)
#define FUSB302_CTRL0_HOST_CUR_1A5  (2<<2)
#define FUSB302_CTRL0_HOST_CUR_USB  (1<<2)
#define FUSB302_CTRL0_AUTO_PRE      (1<<1)  ///< 自动前导码使能
#define FUSB302_CTRL0_TX_START      (1<<0)  ///< 启动发送

// --- CONTROL1 (0x07) ---
#define FUSB302_CTRL1_ENSOP2DB      (1<<6)  ///< SOP2_Debug 检测使能
#define FUSB302_CTRL1_ENSOP1DB      (1<<5)  ///< SOP1_Debug 检测使能
#define FUSB302_CTRL1_BIST_MODE2    (1<<4)  ///< BIST 模式2
#define FUSB302_CTRL1_RX_FLUSH      (1<<2)  ///< 刷新 RX FIFO
#define FUSB302_CTRL1_ENSOP2        (1<<1)  ///< 检测 SOP' 和 SOP'' 消息
#define FUSB302_CTRL1_ENSOP1        (1<<0)  ///< 检测 SOP 消息

// --- CONTROL2 (0x08) ---
#define FUSB302_CTRL2_WAKE_EN       (1<<3)  ///< 唤醒使能
#define FUSB302_CTRL2_MODE_MASK     (3<<1)  ///< 模式选择 [2:1]
#define FUSB302_CTRL2_MODE_DFP      (3<<1)  ///< 下行端口 (Source/Host)
#define FUSB302_CTRL2_MODE_UFP      (2<<1)  ///< 上行端口 (Sink/Device)
#define FUSB302_CTRL2_MODE_DRP      (1<<1)  ///< 双角色端口
#define FUSB302_CTRL2_TOGGLE        (1<<0)  ///< DRP 模式自动切换

// --- CONTROL3 (0x09) ---
#define FUSB302_CTRL3_SEND_HARDRST  (1<<6)  ///< 发送 Hard Reset
#define FUSB302_CTRL3_BIST_TMODE    (1<<5)  ///< BIST 测试模式 (仅 FUSB302B)
#define FUSB302_CTRL3_AUTO_HARDRST  (1<<4)  ///< 自动 Hard Reset
#define FUSB302_CTRL3_AUTO_SOFTRST  (1<<3)  ///< 自动 Soft Reset
#define FUSB302_CTRL3_N_RETRIES(n)  ((n)<<1)///< 自动重试次数 (0~3)
#define FUSB302_CTRL3_N_RETRIES_MASK (3<<1)
#define FUSB302_CTRL3_AUTO_RETRY    (1<<0)  ///< 自动重试使能

// --- MASK (0x0A) ---  主中断屏蔽 (1=屏蔽/禁用中断)
#define FUSB302_MASK_VBUSOK         (1<<7)  ///< VBUS 电压 OK
#define FUSB302_MASK_ACTIVITY       (1<<6)  ///< CC 线活动
#define FUSB302_MASK_COMP_CHNG      (1<<5)  ///< 比较器输出变化
#define FUSB302_MASK_CRC_CHK        (1<<4)  ///< CRC 校验完成
#define FUSB302_MASK_ALERT          (1<<3)  ///< 告警
#define FUSB302_MASK_WAKE           (1<<2)  ///< 唤醒
#define FUSB302_MASK_COLLISION      (1<<1)  ///< 碰撞检测
#define FUSB302_MASK_BC_LVL         (1<<0)  ///< BC 电平检测

// --- POWER (0x0B) ---
#define FUSB302_PWR_INT_OSC         (1<<3)  ///< 内部振荡器使能
#define FUSB302_PWR_MEASURE         (1<<2)  ///< 测量模块供电
#define FUSB302_PWR_RECEIVER        (1<<1)  ///< 接收器供电
#define FUSB302_PWR_BANDGAP         (1<<0)  ///< 带隙基准和唤醒电路

// --- RESET (0x0C) ---
#define FUSB302_RESET_PD_RESET      (1<<1)  ///< PD 协议层复位
#define FUSB302_RESET_SW_RES        (1<<0)  ///< 软件复位 (恢复默认值)

// --- MASKA (0x0E) ---  中断A屏蔽
#define FUSB302_MASKA_OCP_TEMP      (1<<7)  ///< 过温/过流
#define FUSB302_MASKA_TOGDONE       (1<<6)  ///< DRP 切换完成
#define FUSB302_MASKA_SOFTFAIL      (1<<5)  ///< Soft Reset 失败
#define FUSB302_MASKA_RETRYFAIL     (1<<4)  ///< 重试失败
#define FUSB302_MASKA_HARDSENT      (1<<3)  ///< Hard Reset 已发送
#define FUSB302_MASKA_TXSENT        (1<<2)  ///< 消息发送完成
#define FUSB302_MASKA_SOFTRST       (1<<1)  ///< 收到 Soft Reset
#define FUSB302_MASKA_HARDRST       (1<<0)  ///< 收到 Hard Reset

// --- MASKB (0x0F) ---  中断B屏蔽
#define FUSB302_MASKB_GCRCSENT      (1<<0)  ///< GoodCRC 已发送

// --- STATUS0A (0x3C) ---
#define FUSB302_STA0A_SOFTFAIL      (1<<5)  ///< Soft Reset 失败
#define FUSB302_STA0A_RETRYFAIL     (1<<4)  ///< 重试失败
#define FUSB302_STA0A_POWER3        (1<<3)  ///< 3.0A 能力
#define FUSB302_STA0A_POWER2        (1<<2)  ///< 1.5A 能力
#define FUSB302_STA0A_SOFTRST       (1<<1)  ///< 收到 Soft Reset
#define FUSB302_STA0A_HARDRST       (1<<0)  ///< 收到 Hard Reset

// --- STATUS1A (0x3D) ---
#define FUSB302_STA1A_TOGSS_MASK    (7<<3)  ///< DRP 切换状态 [5:3]
#define FUSB302_STA1A_TOGSS_SNK1    (5<<3)  ///< DRP 停在 Sink CC1
#define FUSB302_STA1A_TOGSS_SNK2    (6<<3)  ///< DRP 停在 Sink CC2
#define FUSB302_STA1A_RXSOP2DB      (1<<2)  ///< 检测到 SOP2_Debug
#define FUSB302_STA1A_RXSOP1DB      (1<<1)  ///< 检测到 SOP1_Debug
#define FUSB302_STA1A_RXSOP         (1<<0)  ///< 检测到 SOP

// --- INTERRUPTA (0x3E) ---
#define FUSB302_INTA_OCP_TEMP       (1<<7)
#define FUSB302_INTA_TOGDONE        (1<<6)
#define FUSB302_INTA_SOFTFAIL       (1<<5)
#define FUSB302_INTA_RETRYFAIL      (1<<4)
#define FUSB302_INTA_HARDSENT       (1<<3)
#define FUSB302_INTA_TXSENT         (1<<2)
#define FUSB302_INTA_SOFTRST        (1<<1)
#define FUSB302_INTA_HARDRST        (1<<0)

// --- INTERRUPTB (0x3F) ---
#define FUSB302_INTB_GCRCSENT       (1<<0)

// --- STATUS0 (0x40) ---
#define FUSB302_ST0_VBUSOK          (1<<7)  ///< VBUS > 阈值 (默认4V)
#define FUSB302_ST0_ACTIVITY        (1<<6)  ///< CC 线上有 BMC 活动
#define FUSB302_ST0_COMP            (1<<5)  ///< CC 比较器输出 (1=高于基准)
#define FUSB302_ST0_CRC_CHK         (1<<4)  ///< CRC 校验结果 (1=通过)
#define FUSB302_ST0_ALERT           (1<<3)  ///< 告警状态
#define FUSB302_ST0_WAKE            (1<<2)  ///< 唤醒状态
#define FUSB302_ST0_BC_LVL_MASK     (3<<0)  ///< BC 电平 [1:0]

/// BC 电平编码
#define FUSB302_BC_LVL_VRA          0x00    ///< <200mV    (vRa - 无连接)
#define FUSB302_BC_LVL_VRD_USB      0x01    ///< 200~660mV (vRd-USB)
#define FUSB302_BC_LVL_VRD_1A5      0x02    ///< 660~1.23V (vRd-1.5A)
#define FUSB302_BC_LVL_VRD_3A0      0x03    ///< >1.23V    (vRd-3.0A)

// --- STATUS1 (0x41) ---
#define FUSB302_ST1_RXSOP2          (1<<7)  ///< 收到 SOP' 或 SOP''
#define FUSB302_ST1_RXSOP1          (1<<6)  ///< 收到 SOP
#define FUSB302_ST1_RX_EMPTY        (1<<5)  ///< RX FIFO 空
#define FUSB302_ST1_RX_FULL         (1<<4)  ///< RX FIFO 满
#define FUSB302_ST1_TX_EMPTY        (1<<3)  ///< TX FIFO 空
#define FUSB302_ST1_TX_FULL         (1<<2)  ///< TX FIFO 满
#define FUSB302_ST1_OVRTEMP         (1<<1)  ///< 过温
#define FUSB302_ST1_OCP             (1<<0)  ///< 过流

// --- INTERRUPT (0x42) ---
#define FUSB302_INT_VBUSOK          (1<<7)
#define FUSB302_INT_ACTIVITY        (1<<6)
#define FUSB302_INT_COMP_CHNG       (1<<5)
#define FUSB302_INT_CRC_CHK         (1<<4)
#define FUSB302_INT_ALERT           (1<<3)
#define FUSB302_INT_WAKE            (1<<2)
#define FUSB302_INT_COLLISION       (1<<1)
#define FUSB302_INT_BC_LVL          (1<<0)

// ============================================================================
// 四、TX Token 定义 (用于构造 PD 消息帧)
// ============================================================================
#define FUSB302_TX_TOKEN_TXON       0xA1    ///< 使能发送器
#define FUSB302_TX_TOKEN_SOP1       0x12    ///< SOP 序列字节1
#define FUSB302_TX_TOKEN_SOP2       0x13    ///< SOP 序列字节2
#define FUSB302_TX_TOKEN_SOP3       0x1B    ///< SOP 序列字节3
#define FUSB302_TX_TOKEN_RESET1     0x15    ///< Hard Reset 序列字节1
#define FUSB302_TX_TOKEN_RESET2     0x16    ///< Hard Reset 序列字节2
#define FUSB302_TX_TOKEN_PACKSYM    0x80    ///< 数据包符号 (OR 上长度)
#define FUSB302_TX_TOKEN_JAM_CRC    0xFF    ///< Jam CRC (让接收方 CRC 校验失败)
#define FUSB302_TX_TOKEN_EOP        0x14    ///< 包结束
#define FUSB302_TX_TOKEN_TXOFF      0xFE    ///< 关闭发送器

// ============================================================================
// 五、设备版本校验
// ============================================================================
#define FUSB302_DEVICE_ID_VALID_MASK 0x80   ///< 设备ID 的 bit7 必须为1
#define FUSB302_REV_A               0x00    ///< FUSB302 版本A
#define FUSB302_REV_B               0x01    ///< FUSB302B

#endif // FUSB302_REG_H
