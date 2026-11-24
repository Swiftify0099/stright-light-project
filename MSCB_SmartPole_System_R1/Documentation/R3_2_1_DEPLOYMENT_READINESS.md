# MSCB SMART POLE SYSTEM — R3.2.1 DEPLOYMENT READINESS & HARDWARE TRUTH AUDIT

**Date:** November 24, 2026  
**System:** MSCB Smart Pole Automated Mesh & Telemetry Subsystem  
**Release Target:** R3.2.1  
**Base Commit:** `d6def238c7d56f5bcbf6963b4e6bd6a5584a72d2`  
**Current Branch:** `r3.2.1-critical-mesh-truth-repair`

---

## 1. Production Deployment Readiness Assessment

| Subsystem | Readiness Level | Verified Operating Condition | Restrictions / Safe Operating Envelopes |
|---|---|---|---|
| **Core Mesh Routing** | **READY (Level 4)** | Multi-hop forwarding, self-loop drop, sequence/hop limits, monotonic cycle avoidance | Up to 6 hops max. Up to 12 direct tracked children per node. |
| **Gateway Alert Coordinator** | **READY (Level 4)** | Bounded 16-event circular alert queue, cascade failure detection, cooldown deduplication | Cooldown configurable (10-60s). Up to 50 active poles. |
| **Telegram Admin UI** | **READY (Level 4)** | Mobile-compliant pagination (strict <= 6 buttons per keyboard), allowlist authorization | Authorized admin chat IDs only. |
| **Metrology Firmware Engine** | **BENCH READY** | 21-channel SPI sampling, 3-chip DRDY synchronization, NaN on disconnected sensor | High-voltage 230V/415V mains input: **STRICTLY PROHIBITED**. |
| **Relay Coil Actuator Driver** | **SAFETY LOCKED** | Active-low coil logic, physical GPIO 39 safety jumper, 5-minute auto-expiry, emergency trip | `MSCB_RELAYS_LOCKED_DISABLED = 1` preserved in firmware. |

---

## 2. Hardware Truth Verification Matrix

In compliance with the **Real Hardware Truth Policy**, features that require physical environmental stimulation, live three-phase grid connections, or physical RF propagation over kilometer distances that cannot be physically validated in the isolated local bench environment are explicitly classified below:

| Feature / Hardware Interface | Implementation Status | Test Environment Verification | Production Field Readiness |
|---|---|---|---|
| **Mesh Self-Loop & Cycle Rejection** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Wire Frame Child Node Tracking** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Telegram Menu JSON Structure (<= 6)** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Gateway Direct vs Cascade Offline** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Physical Jumper Auth (GPIO 39)** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Boot Pin Config Fallback (CRC32)** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **Truthful Auxiliary Feedback Masking** | IMPLEMENTED | **VERIFIED (Native Runner)** | Field Deployable |
| **SX1278 RF Long-Distance Propagation (1-5 km)** | IMPLEMENTED | **NOT TESTED** (Requires field range survey) | Pending Field Survey |
| **230V/415V Three-Phase AC Mains Metrology** | IMPLEMENTED | **NOT TESTED** (Mains high voltage strictly prohibited) | Requires Certified HV Lab |
| **Heavy Industrial Contactor 415V Arcing** | IMPLEMENTED | **NOT TESTED** (Requires high-power contactor testbench) | Requires Certified HV Lab |
| **ADS131M08 Physical SPI Hardware Ring** | IMPLEMENTED | **NOT TESTED** (Requires physical SPI ADC PCB) | Pending Physical PCB Hookup |
| **MCP23S17 Physical I/O Expander Ring** | IMPLEMENTED | **NOT TESTED** (Requires physical MCP23S17 PCB) | Pending Physical PCB Hookup |

> [!IMPORTANT]
> **SAFETY DIRECTIVE: 230V / 415V MAINS PROHIBITION**  
> Under no circumstances should 230V single-phase or 415V three-phase AC mains be connected to the ESP32 developer boards or actuator modules during bench development or testing. All physical testing must be performed exclusively using isolated, current-limited low-voltage DC supplies (5V / 3.3V, max 1.0A).

---

## 3. Standard Operating Procedure: Low-Voltage Isolated Bench Testing

To commission a Field Pole node in a controlled bench setting:

1. **Power Supply Setup:**
   * Supply the ESP32 via regulated +5.0V DC (USB or isolated bench power supply).
   * Ensure common ground between ESP32 and logic analyzer/test bench.
2. **Physical Commissioning Jumper (GPIO 39):**
   * To authorize relay commissioning, short `POLE_PHYSICAL_AUTH_PIN` (GPIO 39) to GND using a physical jumper wire.
   * Firmware debounces this input over 3 consecutive cycles (300ms).
   * Note: The commissioning session has a strict 5-minute auto-expiry window (`POLE_PHYSICAL_AUTH_TIMEOUT_MS = 300,000 ms`).
3. **Emergency Disconnect Verification:**
   * Removing the jumper from GPIO 39 while any relay is energized immediately triggers `emergencyShutdownAll("PHYSICAL_AUTH_LOST")`, unlatching all 6 relay coil pins to HIGH (Safe OFF) in under 10 milliseconds.
4. **Firmware Safety Profile:**
   * In field production builds, `MSCB_RELAYS_LOCKED_DISABLED = 1` permanently prevents remote command relay closure unless an authorized technician is physically present at the pole with the hardware jumper inserted.
