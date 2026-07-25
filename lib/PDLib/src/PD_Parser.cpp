/**
 * @file    PD_Parser.cpp
 * @brief   USB PD 协议消息解析器实现
 */

#include <string.h>
#include <stdio.h>
#include "PD_Parser.h"

// ============================================================================
// 消息头解析
// ============================================================================

void pd_parse_header(uint16_t header, PD_HeaderInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(PD_HeaderInfo));
    
    info->msg_type    = header & 0x1F;
    info->spec_rev    = (header >> 6) & 0x3;
    info->message_id  = (header >> 9) & 0x7;
    info->num_objects = (header >> 12) & 0x7;
    info->is_extended = (header >> 15) & 0x1;
    info->is_data_msg = (!info->is_extended && info->num_objects > 0);
    info->is_ctrl_msg = (!info->is_extended && info->num_objects == 0);
}

// ============================================================================
// PDO 解析
// ============================================================================

void pd_parse_pdo(uint32_t pdo_raw, PD_PDOInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(PD_PDOInfo));
    
    info->pdo_type = (pdo_raw >> 30) & 0x3;
    
    switch (info->pdo_type) {
        case PD_PDO_TYPE_FIXED:
            // Reference: USB PD 6.4.1.2.3 Source Fixed Supply PDO
            info->max_current     = (pdo_raw >>  0) & 0x3FF;   // 10mA units
            info->max_voltage     = (pdo_raw >> 10) & 0x3FF;   // 50mV units
            info->min_voltage     = 0;                          // Fixed = 没有最小值
            info->usb_comm_capable = (pdo_raw >> 25) & 1;
            info->dual_role_data   = (pdo_raw >> 26) & 1;
            info->unconstrained    = (pdo_raw >> 27) & 1;
            break;
            
        case PD_PDO_TYPE_BATTERY:
            // Reference: USB PD 6.4.1.2.5 Battery Supply PDO
            info->max_power       = (pdo_raw >>  0) & 0x3FF;   // 250mW units
            info->min_voltage     = (pdo_raw >> 10) & 0x3FF;   // 50mV units
            info->max_voltage     = (pdo_raw >> 20) & 0x3FF;   // 50mV units
            break;
            
        case PD_PDO_TYPE_VARIABLE:
            // Reference: USB PD 6.4.1.2.4 Variable Supply PDO
            info->max_current     = (pdo_raw >>  0) & 0x3FF;   // 10mA units
            info->min_voltage     = (pdo_raw >> 10) & 0x3FF;   // 50mV units
            info->max_voltage     = (pdo_raw >> 20) & 0x3FF;   // 50mV units
            break;
            
        case PD_PDO_TYPE_AUGMENTED:
            // Reference: USB PD 6.4.1.3.4 PPS APDO
            info->is_pps          = true;
            info->max_current     = (pdo_raw >>  0) & 0x7F;    // 50mA units
            info->min_voltage     = ((pdo_raw >> 8)  & 0xFF) * 2;  // 100mV → 50mV
            info->max_voltage     = ((pdo_raw >> 17) & 0xFF) * 2;  // 100mV → 50mV
            info->pps_limited     = (pdo_raw >> 27) & 1;
            break;
    }
}

// ============================================================================
// Request 消息解析
// ============================================================================

/**
 * @brief 解析 Request 消息 (默认按 Fixed PDO 格式解析)
 * 
 * Request 消息的编码格式取决于请求的 PDO 类型:
 *   - Fixed/Variable/Battery: bit9-0=OperatingCurrent(10mA), bit19-10=MaxCurrent(10mA)
 *   - PPS APDO:              bit6-0=OperatingCurrent(50mA), bit19-9=OutputVoltage(20mV)
 * 
 * 本函数默认按 Fixed 格式解析。调用者应根据目标 PDO 的 type 来判断:
 *   若 targetPDO_type == PD_PDO_TYPE_AUGMENTED → 调用 pd_parse_request_pps()
 */
void pd_parse_request(uint32_t request_pdo, PD_RequestInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(PD_RequestInfo));
    
    info->object_position = (request_pdo >> 28) & 0x7;
    info->giveback        = (request_pdo >> 27) & 1;
    info->cap_mismatch    = (request_pdo >> 26) & 1;
    info->usb_comm        = (request_pdo >> 25) & 1;
    info->no_suspend      = (request_pdo >> 24) & 1;
    info->unchunked_ext   = (request_pdo >> 23) & 1;
    
    // 默认按 Fixed PDO 格式解析: 50mV 电压 / 10mA 电流
    info->is_pps = false;
    info->operating_voltage = 0;                              // Fixed请求不含电压, 电压从PDO获取
    info->operating_current = (request_pdo >> 0) & 0x3FF;     // bit9-0: 10mA 单位
}

/**
 * @brief 按 PPS APDO 格式解析 Request 消息
 * 
 * PPS Request 编码:
 *   bit6-0:   Operating Current (50mA 单位)
 *   bit19-9:  Output Voltage (20mV 单位)
 *   bit30-28: Object Position
 * 
 * @note 仅在确认目标 PDO 为 PD_PDO_TYPE_AUGMENTED 时调用此函数
 */
void pd_parse_request_pps(uint32_t request_pdo, PD_RequestInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(PD_RequestInfo));
    
    info->object_position = (request_pdo >> 28) & 0x7;
    info->usb_comm        = (request_pdo >> 25) & 1;
    info->unchunked_ext   = (request_pdo >> 23) & 1;
    
    info->is_pps = true;
    info->operating_voltage = (request_pdo >> 9)  & 0x7FF;    // bit19-9: 20mV 单位
    info->operating_current = (request_pdo >> 0)  & 0x7F;     // bit6-0:  50mA 单位
}

// ============================================================================
// PPS_Status 消息解析
// ============================================================================

void pd_parse_pps_status(const uint32_t *data, PD_PPSStatus *status) {
    if (!data || !status) return;
    memset(status, 0, sizeof(PD_PPSStatus));
    
    // PPS_Status 是扩展消息, data[0] 的低16位是扩展消息头, 高16位是第一个数据字节
    // 即: data[0] = (ExtHeader[15:0] << 0) | (PPSDB[0] << 16) | (PPSDB[1] << 24)
    //     data[1] = (PPSDB[2] << 0)  | (PPSDB[3] << 8)
    
    uint8_t ppsdb[4];
    ppsdb[0] = (data[0] >> 16) & 0xFF;
    ppsdb[1] = (data[0] >> 24) & 0xFF;
    ppsdb[2] = (data[1] >>  0) & 0xFF;
    ppsdb[3] = (data[1] >>  8) & 0xFF;
    
    // Reference: USB PD 6.5.10 PPS_Status Message
    status->output_voltage = ((uint16_t)ppsdb[1] << 8) | ppsdb[0];  // 20mV units
    status->output_current = ppsdb[2];                                // 50mA units
    status->ptf = (ppsdb[3] >> 1) & 0x3;                             // Bit 1-2
    status->omf = (ppsdb[3] >> 3) & 0x1;                             // Bit 3
}

// ============================================================================
// 文本转换
// ============================================================================

int pd_pdo_to_string(const PD_PDOInfo *pdo, char *buf, int size) {
    if (!pdo || !buf || size <= 0) return 0;
    
    const char *type_names[] = {"FIX", "BAT", "VAR", "PPS"};
    const char *type = (pdo->pdo_type < 4) ? type_names[pdo->pdo_type] : "???";
    
    if (pdo->is_pps) {
        // PPS: 电压范围 (100mV) + 最大电流 (50mA)
        return snprintf(buf, size, "[PPS] %d.%02dV-%d.%02dV %d.%02dA",
                        pdo->min_voltage / 20, (pdo->min_voltage * 5) % 100,
                        pdo->max_voltage / 20, (pdo->max_voltage * 5) % 100,
                        pdo->max_current / 20, (pdo->max_current * 5) % 100);
    } else if (pdo->pdo_type == PD_PDO_TYPE_FIXED) {
        return snprintf(buf, size, "[%s] %d.%02dV %d.%02dA",
                        type,
                        pdo->max_voltage / 20, (pdo->max_voltage * 5) % 100,
                        pdo->max_current / 100, pdo->max_current % 100);
    } else if (pdo->pdo_type == PD_PDO_TYPE_BATTERY) {
        return snprintf(buf, size, "[%s] %d.%02dV-%d.%02dV %d.%02dW",
                        type,
                        pdo->min_voltage / 20, (pdo->min_voltage * 5) % 100,
                        pdo->max_voltage / 20, (pdo->max_voltage * 5) % 100,
                        pdo->max_power / 4, (pdo->max_power * 25) % 100);
    } else {
        return snprintf(buf, size, "[%s] %d.%02dV-%d.%02dV %d.%02dA",
                        type,
                        pdo->min_voltage / 20, (pdo->min_voltage * 5) % 100,
                        pdo->max_voltage / 20, (pdo->max_voltage * 5) % 100,
                        pdo->max_current / 100, pdo->max_current % 100);
    }
}

int pd_request_to_string(const PD_RequestInfo *req, char *buf, int size) {
    if (!req || !buf || size <= 0) return 0;
    
    if (req->is_pps) {
        return snprintf(buf, size, "Req PPS Pos#%d: %d.%02dV %d.%02dA",
                        req->object_position,
                        req->operating_voltage / 50, (req->operating_voltage * 2) % 100,
                        req->operating_current / 20, (req->operating_current * 5) % 100);
    } else {
        return snprintf(buf, size, "Req FIX Pos#%d: %d.%02dV %d.%02dA",
                        req->object_position,
                        req->operating_voltage / 20, (req->operating_voltage * 5) % 100,
                        req->operating_current / 100, req->operating_current % 100);
    }
}

const char * pd_get_message_name(uint16_t header) {
    PD_HeaderInfo info;
    pd_parse_header(header, &info);
    
    if (info.is_extended) {
        static const char *ext_names[] = {
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, "PPS_Status", NULL, NULL, "Sink_Cap_Ext"
        };
        if (info.msg_type < sizeof(ext_names)/sizeof(ext_names[0]) && ext_names[info.msg_type]) {
            return ext_names[info.msg_type];
        }
        return "ExtMsg";
    }
    
    if (info.is_data_msg || info.num_objects > 0) {
        static const char *data_names[] = {
            NULL, "Source_Cap", "Request", "BIST", "Sink_Cap",
            "Battery_Status", "Alert", "Get_CI", "Enter_USB",
            NULL, NULL, NULL, NULL, NULL, NULL, "VDM"
        };
        if (info.msg_type < sizeof(data_names)/sizeof(data_names[0]) && data_names[info.msg_type]) {
            return data_names[info.msg_type];
        }
        return "DataMsg";
    }
    
    static const char *ctrl_names[] = {
        NULL, "GoodCRC", "GotoMin", "Accept", "Reject", "Ping",
        "PS_RDY", "Get_Src_Cap", "Get_Sink_Cap", "DR_Swap", "PR_Swap",
        "VCONN_Swap", "Wait", "Soft_Rst", NULL, NULL,
        "Not_Supported", NULL, NULL, NULL, "Get_PPS_Status"
    };
    if (info.msg_type < sizeof(ctrl_names)/sizeof(ctrl_names[0]) && ctrl_names[info.msg_type]) {
        return ctrl_names[info.msg_type];
    }
    return "CtrlMsg";
}
