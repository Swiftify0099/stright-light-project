/*
 * =====================================================================
 *  RADIOCONFIG.H — SX1278 LORA RF & REGULATORY CONFIGURATION
 *  Smart Street Light / Farmer Power Distribution System
 *
 *  HARDWARE:
 *  - Gateway: ESP32-S3-WROOM-N16R8 + SX1278 RA-02 433 MHz
 *  - Secondary Node: ESP32-WROOM-32 + SX1278 RA-02 433 MHz
 *
 *  REGULATORY COMPLIANCE:
 *  - Authority: Wireless Planning & Coordination (WPC) Wing, DoT, India
 *  - Notification: G.S.R. 347(E) dated 18 May 2022
 *    (Use of Low Power Radio Frequency Devices in 433.05 to 434.79 MHz
 *     Exemption from Licensing Requirement Rules, 2022)
 * =====================================================================
 */

#ifndef RADIO_CONFIG_H
#define RADIO_CONFIG_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
//  1. INDIAN WPC REGULATORY BOUNDARIES & COMPLIANCE (G.S.R. 347(E))
// ─────────────────────────────────────────────────────────────
#define WPC_RULE_NAME                   "WPC G.S.R. 347(E) (2022)"
#define WPC_MIN_FREQ_HZ                 433050000UL   // 433.050 MHz lower band edge
#define WPC_MAX_FREQ_HZ                 434790000UL   // 434.790 MHz upper band edge
#define WPC_MAX_ERP_MW                  10.0f         // 10 mW maximum Effective Radiated Power
#define WPC_MAX_ERP_DBM                 10.0f         // +10.0 dBm ERP limit (10*log10(10))
#define WPC_MAX_DUTY_CYCLE_PERCENT      10.0f         // <= 10.0% duty cycle restriction

// Antenna & System Losses (for ERP Calculation)
// ERP (dBm) = Conducted Power (dBm) + Antenna Gain (dBi) - Cable Loss (dB) - 2.15 dB
// For a standard 2.15 dBi quarter/half-wave whip antenna: ERP == Conducted Power
#define ANTENNA_GAIN_DBI                2.15f         // Standard dipole / matched whip gain
#define CABLE_LOSS_DB                   0.00f         // PCB direct mount loss
#define MAX_LEGAL_TX_POWER_DBM          10            // Maximum legal conducted power into 2.15 dBi ant

// Regulatory Safety Interlock
// Set to false for Listen-Only / Receive-Only Diagnostic builds
#define LORA_REGULATORY_TX_ENABLE       true

// ─────────────────────────────────────────────────────────────
//  2. LORA MODULATION PARAMETERS (SX1278 @ 433 MHz)
// ─────────────────────────────────────────────────────────────
// MUST fall strictly within [433.050 MHz + BW/2, 434.790 MHz - BW/2]
// For BW = 125 kHz: [433.1125 MHz, 434.7275 MHz]
// 433.920 MHz is the standard, fully compliant ISM center frequency.
// 433.000 MHz is STRICTLY REJECTED as out-of-band (< 433.050 MHz).
#define LORA_DEFAULT_FREQ               433920000UL   // 433.920 MHz Carrier Frequency
#define LORA_DEFAULT_SF                 10            // Spreading Factor 10 (Robust link budget)
#define LORA_DEFAULT_BW                 125000UL      // 125.0 kHz Signal Bandwidth
#define LORA_DEFAULT_CR                 5             // Coding Rate 4/5 (CR=5 in Sandeep Mistry API)
#define LORA_DEFAULT_TXPOW              10            // 10 dBm conducted (10 mW ERP compliant)
#define LORA_SYNC_WORD                  0x12          // Private Network Sync Word
#define LORA_CRC_ENABLED                true          // Hardware CRC payload protection
#define LORA_PREAMBLE_LEN               8             // Preamble symbol length

// ─────────────────────────────────────────────────────────────
//  3. VERIFIED PIN MAPPINGS (From Prompt 06 & ESP32-S3 / ESP32)
// ─────────────────────────────────────────────────────────────
#ifndef LORA_SS
  #define LORA_SS                       5             // Chip Select (NSS)
#endif

#ifndef LORA_RST
  #define LORA_RST                      14            // Hardware Reset
#endif

#ifndef LORA_DIO0
  #define LORA_DIO0                     2             // Packet Ready / TX Done Interrupt
#endif

#ifndef LORA_SCK
  #define LORA_SCK                      18            // SPI Clock
#endif

#ifndef LORA_MISO
  #define LORA_MISO                     19            // SPI MISO
#endif

#ifndef LORA_MOSI
  #define LORA_MOSI                     23            // SPI MOSI
#endif

#define LORA_SPI_SPEED_HZ               8000000UL     // 8 MHz SPI clock (SX1278 supports <= 10 MHz)

// ─────────────────────────────────────────────────────────────
//  4. CHANNEL ACCESS & COLLISION AVOIDANCE (LBT / CSMA)
// ─────────────────────────────────────────────────────────────
#define CARRIER_SENSE_RSSI_THRES_DBM    -95           // Channel considered busy if RSSI > -95 dBm
#define CARRIER_SENSE_SAMPLE_MS         15            // Time to listen before transmit
#define CARRIER_SENSE_MAX_RETRIES       5             // Maximum busy channel backoffs before abort
#define CARRIER_SENSE_MIN_BACKOFF_MS    30            // Minimum randomized backoff
#define CARRIER_SENSE_MAX_BACKOFF_MS    180           // Maximum randomized backoff

// ─────────────────────────────────────────────────────────────
//  5. DUTY-CYCLE TRACKING BUDGET
// ─────────────────────────────────────────────────────────────
#define DUTY_CYCLE_WINDOW_MS            60000UL       // 60-second rolling observation window
#define DUTY_CYCLE_MAX_AIRTIME_MS       6000UL        // Max 6,000 ms (10%) airtime per 60s window

// ─────────────────────────────────────────────────────────────
//  6. PROTOCOL & QUEUE SIZING
// ─────────────────────────────────────────────────────────────
#define RADIO_TX_QUEUE_SIZE             8             // Maximum queued outgoing packets
#define RADIO_RX_QUEUE_SIZE             8             // Maximum queued incoming packets
#define RADIO_MAX_PAYLOAD_SIZE          64            // Maximum application payload bytes
#define RADIO_MAX_PACKET_SIZE           96            // Maximum framed packet size
#define RADIO_DEFAULT_ACK_TIMEOUT_MS    3000UL        // 3.0-second ACK timeout
#define RADIO_DEFAULT_MAX_RETRIES       3             // Number of transmission retries

// Node Identifiers
#define GATEWAY_NODE_ID                 "GATEWAY"
#define BROADCAST_NODE_ID               "BROADCAST"
#define DEFAULT_POLE_NODE_ID            "POLE_01"

// ─────────────────────────────────────────────────────────────
//  7. REGULATORY COMPLIANCE VALIDATOR HELPER
// ─────────────────────────────────────────────────────────────
static inline bool isFrequencyWpcCompliant(uint32_t freqHz, uint32_t bwHz) {
    uint32_t halfBw = bwHz / 2;
    if (freqHz < (WPC_MIN_FREQ_HZ + halfBw)) return false;
    if (freqHz > (WPC_MAX_FREQ_HZ - halfBw)) return false;
    return true;
}

static inline float calculateErpDbm(int conductedPowerDbm, float antGainDbi = ANTENNA_GAIN_DBI, float cableLossDb = CABLE_LOSS_DB) {
    // ERP = EIRP - 2.15 dB = (P_tx + G_ant - L_cable) - 2.15 dB
    return (float)conductedPowerDbm + antGainDbi - cableLossDb - 2.15f;
}

static inline int clampLegalTxPower(int requestedDbm, float antGainDbi = ANTENNA_GAIN_DBI, float cableLossDb = CABLE_LOSS_DB) {
    // Determine maximum conducted power that produces <= WPC_MAX_ERP_DBM
    float maxConducted = WPC_MAX_ERP_DBM - antGainDbi + cableLossDb + 2.15f;
    int legalCap = (int)floor(maxConducted);
    if (requestedDbm > legalCap) {
        return legalCap;
    }
    if (requestedDbm < 2) {
        return 2; // SX1278 PA_BOOST minimum power
    }
    return requestedDbm;
}

#endif // RADIO_CONFIG_H
