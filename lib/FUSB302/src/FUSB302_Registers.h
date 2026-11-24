/**
 * FUSB302 register map, bit fields and TX/RX FIFO tokens.
 *
 * References:
 *  - ON Semiconductor FUSB302B datasheet (register tables 16..42)
 *  - Linux drivers/usb/typec/tcpm/fusb302_reg.h
 *  - kws-x1-standalone-rs fusb302-pd/src/register.rs
 *
 * Licensed under the MIT License. See LICENSE.
 */
#pragma once

#include <stdint.h>

namespace fusb302 {

/* ------------------------------------------------------------------------- */
/* I2C address                                                               */
/* ------------------------------------------------------------------------- */

/// 7-bit address of FUSB302BMPX / FUSB302BVMPX / FUSB302BUCX.
/// Variants FUSB302B01/B10/B11 use 0x23..0x25.
static const uint8_t DEFAULT_I2C_ADDRESS = 0x22;

/* ------------------------------------------------------------------------- */
/* Register addresses                                                        */
/* ------------------------------------------------------------------------- */

enum Register : uint8_t {
    REG_DEVICE_ID   = 0x01,
    REG_SWITCHES0   = 0x02,
    REG_SWITCHES1   = 0x03,
    REG_MEASURE     = 0x04,
    REG_SLICE       = 0x05,
    REG_CONTROL0    = 0x06,
    REG_CONTROL1    = 0x07,
    REG_CONTROL2    = 0x08,
    REG_CONTROL3    = 0x09,
    REG_MASK        = 0x0A,
    REG_POWER       = 0x0B,
    REG_RESET       = 0x0C,
    REG_MASKA       = 0x0E,
    REG_MASKB       = 0x0F,
    REG_STATUS0A    = 0x3C,
    REG_STATUS1A    = 0x3D,
    REG_INTERRUPTA  = 0x3E,
    REG_INTERRUPTB  = 0x3F,
    REG_STATUS0     = 0x40,
    REG_STATUS1     = 0x41,
    REG_INTERRUPT   = 0x42,
    REG_FIFOS       = 0x43
};

/* ------------------------------------------------------------------------- */
/* SWITCHES0 (0x02)                                                          */
/* ------------------------------------------------------------------------- */

enum Switches0 : uint8_t {
    SW0_CC2_PU_EN  = 0x80,  ///< Pull-up (Rp) on CC2
    SW0_CC1_PU_EN  = 0x40,  ///< Pull-up (Rp) on CC1
    SW0_VCONN_CC2  = 0x20,  ///< VCONN switch on CC2
    SW0_VCONN_CC1  = 0x10,  ///< VCONN switch on CC1
    SW0_MEAS_CC2   = 0x08,  ///< Measure block monitors CC2
    SW0_MEAS_CC1   = 0x04,  ///< Measure block monitors CC1
    SW0_PDWN_CC2   = 0x02,  ///< Pull-down (Rd) on CC2
    SW0_PDWN_CC1   = 0x01   ///< Pull-down (Rd) on CC1
};

/* ------------------------------------------------------------------------- */
/* SWITCHES1 (0x03)                                                          */
/* ------------------------------------------------------------------------- */

enum Switches1 : uint8_t {
    SW1_POWER_ROLE  = 0x80,  ///< Power role (1 = source, 0 = sink)
    SW1_SPECREV_1   = 0x40,  ///< Specification revision bit 1
    SW1_SPECREV_0   = 0x20,  ///< Specification revision bit 0 (1 = PD 2.0)
    SW1_DATA_ROLE   = 0x10,  ///< Data role (1 = DFP/host, 0 = UFP/device)
    SW1_AUTO_CRC    = 0x04,  ///< Automatically answer good frames with GoodCRC
    SW1_TXCC2_EN    = 0x02,  ///< BMC transmitter on CC2
    SW1_TXCC1_EN    = 0x01   ///< BMC transmitter on CC1
};

/// SPECREV[1:0] = 01b selects PD 2.0 for automatic GoodCRC.
static const uint8_t SW1_SPECREV_PD2 = SW1_SPECREV_0;

/* ------------------------------------------------------------------------- */
/* MEASURE (0x04)                                                            */
/* ------------------------------------------------------------------------- */

enum Measure : uint8_t {
    MEAS_MDAC_MASK = 0x3F,  ///< MDAC[5:0], comparator threshold
    MEAS_VBUS      = 0x40   ///< Measure VBUS instead of CC
};

/// MDAC = 0x31 -> ~2.1 V (reset value). Suitable CC comparator threshold.
static const uint8_t MEASURE_DEFAULT = 0x31;

/* ------------------------------------------------------------------------- */
/* CONTROL0 (0x06)                                                           */
/* ------------------------------------------------------------------------- */

enum Control0 : uint8_t {
    C0_TX_FLUSH        = 0x40,
    C0_INT_MASK        = 0x20,
    C0_HOST_CUR_MASK   = 0x0C,
    C0_HOST_CUR_DEF    = 0x04,  ///< Default host current
    C0_HOST_CUR_MED    = 0x08,
    C0_HOST_CUR_HIGH   = 0x0C,
    C0_TX_START        = 0x01
};

/* ------------------------------------------------------------------------- */
/* CONTROL1 (0x07)                                                           */
/* ------------------------------------------------------------------------- */

enum Control1 : uint8_t {
    C1_ENSOP2DB   = 0x40,
    C1_ENSOP1DB   = 0x20,
    C1_BIST_MODE2 = 0x10,
    C1_RX_FLUSH   = 0x04,
    C1_ENSOP2     = 0x02,
    C1_ENSOP1     = 0x01
};

/* ------------------------------------------------------------------------- */
/* CONTROL2 (0x08)                                                           */
/* ------------------------------------------------------------------------- */

enum Control2 : uint8_t {
    C2_MODE_MASK = 0x06,
    C2_MODE_DFP  = 0x06,
    C2_MODE_UFP  = 0x04,
    C2_MODE_DRP  = 0x02,
    C2_MODE_NONE = 0x00,
    C2_TOGGLE    = 0x01
};

/* ------------------------------------------------------------------------- */
/* CONTROL3 (0x09)                                                           */
/* ------------------------------------------------------------------------- */

enum Control3 : uint8_t {
    C3_SEND_HARDRESET   = 0x40,
    C3_BIST_TMODE       = 0x20,
    C3_AUTO_HARDRESET   = 0x10,
    C3_AUTO_SOFTRESET   = 0x08,
    C3_N_RETRIES_MASK   = 0x06,
    C3_N_RETRIES_1      = 0x02,
    C3_N_RETRIES_2      = 0x04,
    C3_N_RETRIES_3      = 0x06,
    C3_AUTO_RETRY       = 0x01
};

inline uint8_t c3Retries(uint8_t retries) {
    return (uint8_t)((retries & 0x03) << 1);
}

/* ------------------------------------------------------------------------- */
/* MASK / MASKA / MASKB                                                      */
/* ------------------------------------------------------------------------- */

enum MaskBits : uint8_t {
    MASK_VBUSOK    = 0x80,
    MASK_ACTIVITY  = 0x40,
    MASK_COMP_CHNG = 0x20,
    MASK_CRC_CHK   = 0x10,
    MASK_ALERT     = 0x08,
    MASK_WAKE      = 0x04,
    MASK_COLLISION = 0x02,
    MASK_BC_LVL    = 0x01
};

enum MasksA : uint8_t {
    MASKA_OCP_TEMP  = 0x80,
    MASKA_TOGDONE   = 0x40,
    MASKA_SOFTFAIL  = 0x20,
    MASKA_RETRYFAIL = 0x10,
    MASKA_HARDSENT  = 0x08,
    MASKA_TXSENT    = 0x04,
    MASKA_SOFTRST   = 0x02,
    MASKA_HARDRST   = 0x01
};

enum MasksB : uint8_t {
    MASKB_GCRCSENT = 0x01
};

/* ------------------------------------------------------------------------- */
/* POWER (0x0B)                                                              */
/* ------------------------------------------------------------------------- */

enum Power : uint8_t {
    PWR_OSCILLATOR   = 0x08,
    PWR_MEASURE      = 0x04,
    PWR_RECEIVER     = 0x02,
    PWR_BANDGAP_WAKE = 0x01,
    PWR_ALL          = 0x0F
};

/* ------------------------------------------------------------------------- */
/* RESET (0x0C)                                                              */
/* ------------------------------------------------------------------------- */

enum Reset : uint8_t {
    RESET_SW_RESET = 0x01,
    RESET_PD_RESET = 0x02
};

/* ------------------------------------------------------------------------- */
/* STATUS0 (0x40)                                                            */
/* ------------------------------------------------------------------------- */

enum Status0 : uint8_t {
    ST0_VBUSOK   = 0x80,
    ST0_ACTIVITY = 0x40,
    ST0_COMP     = 0x20,
    ST0_CRC_CHK  = 0x10,
    ST0_ALERT    = 0x08,
    ST0_WAKE     = 0x04,
    ST0_BC_LVL   = 0x03
};

/* ------------------------------------------------------------------------- */
/* STATUS1 (0x41)                                                            */
/* ------------------------------------------------------------------------- */

enum Status1 : uint8_t {
    ST1_RXSOP2   = 0x80,
    ST1_RXSOP1   = 0x40,
    ST1_RX_EMPTY = 0x20,
    ST1_RX_FULL  = 0x10,
    ST1_TX_EMPTY = 0x08,
    ST1_TX_FULL  = 0x04,
    ST1_OVRTEMP  = 0x02,
    ST1_OCP      = 0x01,

    ST1_FIFOS_AND_FAULTS = ST1_RX_EMPTY | ST1_RX_FULL | ST1_TX_EMPTY |
                           ST1_TX_FULL | ST1_OVRTEMP | ST1_OCP,
    ST1_IDLE = ST1_RX_EMPTY | ST1_TX_EMPTY
};

/* ------------------------------------------------------------------------- */
/* STATUS1A (0x3D)                                                           */
/* ------------------------------------------------------------------------- */

enum Status1A : uint8_t {
    ST1A_TOGSS_MASK   = 0x38,
    ST1A_TOGSS_SHIFT  = 3,
    ST1A_RXSOP2DB     = 0x04,
    ST1A_RXSOP1DB     = 0x02,
    ST1A_RXSOP        = 0x01
};

/* ------------------------------------------------------------------------- */
/* INTERRUPTA (0x3E)                                                         */
/* ------------------------------------------------------------------------- */

enum InterruptA : uint8_t {
    IRQA_OCP_TEMP  = 0x80,
    IRQA_TOGDONE   = 0x40,
    IRQA_SOFTFAIL  = 0x20,
    IRQA_RETRYFAIL = 0x10,
    IRQA_HARDSENT  = 0x08,
    IRQA_TXSENT    = 0x04,
    IRQA_SOFTRST   = 0x02,
    IRQA_HARDRST   = 0x01
};

/* ------------------------------------------------------------------------- */
/* INTERRUPTB (0x3F)                                                         */
/* ------------------------------------------------------------------------- */

enum InterruptB : uint8_t {
    IRQB_GCRCSENT = 0x01
};

/* ------------------------------------------------------------------------- */
/* INTERRUPT (0x42)                                                          */
/* ------------------------------------------------------------------------- */

enum Interrupt : uint8_t {
    IRQ_VBUSOK    = 0x80,
    IRQ_ACTIVITY  = 0x40,
    IRQ_COMP_CHNG = 0x20,
    IRQ_CRC_CHK   = 0x10,
    IRQ_ALERT     = 0x08,
    IRQ_WAKE      = 0x04,
    IRQ_COLLISION = 0x02,
    IRQ_BC_LVL    = 0x01
};

/* ------------------------------------------------------------------------- */
/* TX FIFO tokens (not USB PD K-codes)                                       */
/* ------------------------------------------------------------------------- */

enum TxToken : uint8_t {
    TX_TOKEN_TXON    = 0xA1,
    TX_TOKEN_SYNC1   = 0x12,
    TX_TOKEN_SYNC2   = 0x13,
    TX_TOKEN_SYNC3   = 0x1B,
    TX_TOKEN_RST1    = 0x15,
    TX_TOKEN_RST2    = 0x16,
    TX_TOKEN_PACKSYM = 0x80,  ///< OR with the number of following bytes
    TX_TOKEN_JAMCRC  = 0xFF,
    TX_TOKEN_EOP     = 0x14,
    TX_TOKEN_TXOFF   = 0xFE
};

/* ------------------------------------------------------------------------- */
/* RX FIFO SOP* token address codes                                          */
/* ------------------------------------------------------------------------- */

enum RxSopToken : uint8_t {
    RX_TOKEN_SOP        = 0xE0,  // bits 7:5 = 111
    RX_TOKEN_SOP_PRIME  = 0xC0,  // bits 7:5 = 110
    RX_TOKEN_SOP_DPRIME = 0xA0,  // bits 7:5 = 101
    RX_TOKEN_SOP_DEBUG  = 0x80,  // bits 7:5 = 100
    RX_TOKEN_SOP_DEBUG2 = 0x60,  // bits 7:5 = 011
    RX_TOKEN_MASK       = 0xE0
};

/* ------------------------------------------------------------------------- */
/* Limits                                                                    */
/* ------------------------------------------------------------------------- */

static const uint8_t MAX_DATA_OBJECTS  = 7;
static const uint8_t MAX_MESSAGE_BYTES = 2 + MAX_DATA_OBJECTS * 4;  // 30
static const uint8_t MAX_FRAME_BYTES   = 1 + MAX_MESSAGE_BYTES + 4; // 35 incl token + CRC
static const uint8_t RX_FIFO_SIZE      = 40;

} // namespace fusb302
