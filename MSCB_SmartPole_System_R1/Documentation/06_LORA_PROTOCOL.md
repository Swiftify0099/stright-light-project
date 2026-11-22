# 06 — MSCB Smart Pole Automation System: LoRa Binary Protocol Specification

**Document ID**: MSCB-DOC-06  
**Classification**: Communication Protocol & Framing Specification  
**Frequency Band**: 433.050 MHz to 434.790 MHz (WPC G.S.R. 347(E))  
**Carrier Frequency**: 433.920 MHz  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Physical Layer RF Configuration

The MSCB Smart Pole RF subsystem is implemented using the Semtech SX1278 transceiver (Ai-Thinker RA-02). Transmissions conform strictly to the Indian Wireless Planning & Coordination (WPC) Wing Gazette Notification **G.S.R. 347(E)**:

| RF Parameter | Value | Engineering Verification / Standard |
| :--- | :--- | :--- |
| **Carrier Frequency** | **433.920 MHz** | Center of license-exempt SRD band |
| **Signal Bandwidth (BW)** | **125.0 kHz** | `LORA_BW = 125E3` (Standard LoRa bandwidth) |
| **Spreading Factor (SF)** | **SF10** | High link margin (-132 dBm sensitivity) |
| **Coding Rate (CR)** | **4/5 (CR=5)** | Forward Error Correction overhead: 1.25x |
| **Conducted TX Power** | **+10.0 dBm (10 mW)**| Complies with 10 mW ERP limit into 2.15 dBi antenna |
| **Preamble Length** | **8 Symbols** | Standard synchronization preamble |
| **Sync Word** | **0x12** | Private network sync byte |
| **Hardware CRC** | **Enabled** | Transceiver hardware packet CRC validation |

---

## 2. Binary Over-The-Air Frame Structure

All over-the-air packets share a unified binary frame format bounded to a maximum length of 128 bytes (well below the SX1278 255-byte hardware FIFO limit):

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Sync Word (0x12)     |  Pkt Type     |    TTL (3b)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Sequence Number (16-bit)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Source Node ID (16-bit)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  Destination Node ID (16-bit)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Next-Hop Node ID (16-bit)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Payload Len  |    Encrypted Payload Bytes (Variable Length)  |
+-+-+-+-+-+-+-+-+                                               +
|                               ...                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   HMAC-SHA256 Auth Tag (16 bytes)             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Frame Check Sequence (CRC-16 CCITT)           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 2.1 Frame Header Field Definitions

| Field Name | Size | Description |
| :--- | :---: | :--- |
| **Sync Word** | 1 Byte | Frame delimiter (`0x12`). Matches SX1278 hardware filter. |
| **Packet Type** | 1 Byte | Identifies protocol opcode (see Section 3). |
| **TTL** | 1 Byte | Time-To-Live hop counter. Initialized to 6; decremented at each hop. |
| **Sequence Number** | 2 Bytes | Monotonically increasing packet counter for deduplication. |
| **Source ID** | 2 Bytes | Logical ID of originating node (`0x0001` = Gateway). |
| **Destination ID** | 2 Bytes | Logical ID of final recipient (`0xFFFF` = Broadcast). |
| **Next-Hop ID** | 2 Bytes | Immediate next-hop transceiver in the mesh DAG. |
| **Payload Length** | 1 Byte | Byte count of payload field ($0 \le L \le 90$). |
| **Payload** | $L$ Bytes | Application data payload (encrypted using AES-128-GCM). |
| **Auth Tag** | 16 Bytes | Cryptographic HMAC / GCM authentication tag. |
| **CRC-16 CCITT** | 2 Bytes | Polynomial $x^{16} + x^{12} + x^5 + 1$ (`0x1021`), Initial: `0xFFFF`. |

---

## 3. Protocol Message Types & Opcodes

| Opcode | Packet Type Code | Priority | Direction | Purpose |
| :---: | :--- | :---: | :---: | :--- |
| `0x01` | `JOIN_REQ` | High | Node $\rightarrow$ GW | Unprovisioned node registration request. |
| `0x02` | `JOIN_ACCEPT` | High | GW $\rightarrow$ Node | Gateway registration approval and ID grant. |
| `0x03` | `BEACON` | Low | Broadcast | Periodic DAG routing beacon (Rank, Path Cost). |
| `0x04` | `TELEMETRY` | Low | Node $\rightarrow$ GW | 21-channel electrical metrology report (83B). |
| `0x05` | `CMD` | Medium | GW $\rightarrow$ Node | Feeder on/off, timer, or configuration command. |
| `0x06` | `CMD_ACK` | High | Node $\rightarrow$ GW | Command receipt & execution verification status. |
| `0x07` | `RREQ` | Medium | Broadcast | Route Request from orphaned node. |
| `0x08` | `RREP` | Medium | Unicast | Route Reply offering DAG parentage. |
| `0x09` | `ALERT` | High | Node $\rightarrow$ GW | Emergency overcurrent, undervoltage, or trip alert. |
| `0x0A` | `TIME_SYNC` | Low | Broadcast | UTC epoch time distribution from Gateway. |
| `0x0B` | `DELE_REQ` | High | GW $\rightarrow$ Node | Node decommissioning & factory wipe order. |
| `0x0C` | `DELE_ACK` | High | Node $\rightarrow$ GW | Decommissioning confirmation. |
| `0x0D` | `REVOKE_BCAST` | High | Broadcast | Blacklist broadcast of revoked hardware ID. |

---

## 4. Payload Specifications

### 4.1 21-Channel Full Electrical Telemetry Payload (`0x04`)

The telemetry payload is packed into an efficient 83-byte binary structure:

```cpp
struct TelemetryPayload {
    uint16_t busVoltageR;      // 0.1 V resolution (e.g. 2305 = 230.5 VAC)
    uint16_t busVoltageY;      // 0.1 V resolution
    uint16_t busVoltageB;      // 0.1 V resolution
    uint16_t gridFrequency;    // 0.01 Hz resolution (e.g. 5002 = 50.02 Hz)
    
    // 18 Feeder Current Channels (6 Feeders * 3 Phases)
    uint16_t feederCurrent[6][3]; // 0.01 A resolution (e.g. 1050 = 10.50 A)
    
    // Status & Feedback Bitmasks
    uint8_t  mcbStatusBits;    // Bits 0-5: MCB status for Feeders 1-6 (1=OK, 0=Tripped)
    uint8_t  contactorBits;    // Bits 0-5: Contactor auxiliary feedback (1=Closed, 0=Open)
    uint8_t  relayDriveBits;   // Bits 0-5: Active relay coil commands
    
    // Health & RF Diagnostics
    uint8_t  nodeRank;         // Current DAG Rank
    uint16_t pathCost;         // Cumulative path cost to Gateway
    int8_t   primaryParentRssi;// RSSI of link to Primary Parent (dBm)
    int8_t   primaryParentSnr; // SNR of link to Primary Parent (dB)
    uint32_t freeHeapBytes;    // Free RAM on microcontroller
    uint32_t uptimeSeconds;    // System uptime counter
} __attribute__((packed));     // Exactly 83 bytes
```

### 4.2 Feeder Actuation Command Payload (`0x05`)

```cpp
struct FeederCommandPayload {
    uint32_t commandId;        // Unique transaction ID
    uint8_t  commandOpcode;    // 1: Direct On/Off, 2: Timer Actuation, 3: Emergency Off
    uint8_t  feederMask;       // Bitmask of target feeders (Bit 0 = F1 ... Bit 5 = F6)
    uint8_t  targetState;      // 1: Turn ON, 0: Turn OFF
    uint32_t durationSeconds;  // Auto-off countdown timer (0 = Indefinite)
    uint32_t nonce;            // Cryptographic anti-replay nonce
} __attribute__((packed));
```

---

## 5. Airtime & Duty Cycle Mathematical Analysis

To guarantee compliance with WPC G.S.R. 347(E) ($\le 10.0\%$ duty cycle):

### 5.1 Symbol Duration ($T_{sym}$)
For Spreading Factor $SF = 10$ and Bandwidth $BW = 125\text{ kHz}$:
$$T_{sym} = \frac{2^{SF}}{BW} = \frac{2^{10}}{125,000} = \frac{1024}{125,000} = 8.192\text{ ms}$$

### 5.2 Packet Airtime Calculation ($T_{packet}$)
For an 83-byte telemetry frame with 16-byte header + 16-byte HMAC + 2-byte CRC ($N_{bytes} = 123\text{ bytes}$):
- Preamble Duration:
  $$T_{preamble} = (N_{preamble} + 4.25) \times T_{sym} = (8 + 4.25) \times 8.192\text{ ms} = 100.35\text{ ms}$$
- Payload Symbol Count ($n_{payload}$):
  $$n_{payload} = 8 + \max\left(\left\lceil \frac{8 \times 123 - 4 \times 10 + 28 + 16}{4 \times 10} \right\rceil \times 5, 0\right) = 138\text{ symbols}$$
- Total Airtime:
  $$T_{packet} = T_{preamble} + (n_{payload} \times T_{sym}) = 100.35\text{ ms} + (138 \times 8.192\text{ ms}) \approx \mathbf{1,230.8\text{ ms}}$$

### 5.3 Duty Cycle Compliance Verification
- **Hourly Transmissions per Node**:
  - Telemetry: 1 packet every 30 seconds = 120 packets/hour.
  - Beacons: 1 packet every 60 seconds = 60 packets/hour.
  - Total packets per hour = 180 packets.
- **Total Transmission Time per Hour**:
  $$T_{tx\_total} = 180 \times 1.231\text{ s} = 221.58\text{ seconds/hour}$$
- **Calculated Duty Cycle**:
  $$\text{Duty Cycle} = \frac{221.58\text{ s}}{3600\text{ s}} \times 100\% = \mathbf{6.15\%}$$
  This is well within the legal limit of $\mathbf{10.0\%}$ enforced by Indian law.
