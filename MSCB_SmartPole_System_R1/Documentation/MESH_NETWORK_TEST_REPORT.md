# MSCB Smart Pole & Feeder Automation System: Mesh Network Test Report

**Document ID**: MSCB-QA-MESH-01  
**Classification**: LoRa Mesh Routing & Topology Resilience Verification  
**System Title**: MSCB Smart Pole & Feeder Automation System (System_R1 / V10)  
**Lead Mesh Architect**: Senior LoRa Networking & Mesh Protocol Engineer  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Mesh Protocol Architecture Summary

The MSCB mesh protocol is a deterministic, Gateway-rooted Directed Acyclic Graph (DAG) designed for rural linear and tree distributions:
1. **Root Gateway**: Anchor node assigned fixed **Rank 0**; periodically transmits network synchronization beacons.
2. **Node Rank Rule**: A node $i$ selecting parent $P$ computes its rank as:
   $$\text{Rank}_i = \text{Rank}_P + 1$$
3. **Link Quality (LQ) Metric Formulation**:
   $$\text{LQ} = 0.40 \times \text{RSSI}_{norm} + 0.30 \times \text{SNR}_{norm} + 0.30 \times \text{PDR}$$
   where:
   - $\text{RSSI}_{norm} = \text{clamp}\left(\frac{\text{RSSI} + 120}{60}, 0, 1\right)$
   - $\text{SNR}_{norm} = \text{clamp}\left(\frac{\text{SNR} + 15}{25}, 0, 1\right)$
   - $\text{PDR} = \frac{\text{Packets Received}}{\text{Packets Expected}}$
4. **Path Cost Function**: $\text{Cost}_{path} = \text{Cost}_P + \left(100 - \text{LQ}\right)$. Lower cost strictly preferred.
5. **Two-Parent Redundancy**: Each node tracks a **Primary Parent** and a **Backup Parent** (with path diversity to avoid single-point upstream failures).

---

## 2. Comprehensive 14 Failure Recovery Scenarios (Prompt 10 Section 10)

All 14 failure scenarios were systematically verified using the automated `test_mesh_routing.exe` test engine:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            14 FAILURE SCENARIOS VERIFICATION MATRIX                         │
├─────┬───────────────────────────────────────┬──────────────────────────────┬────────────────┤
│Case │Failure Scenario                       │Observed System Behavior      │Test Result     │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│01   │Primary Parent Goes Offline            │Switched to Backup Parent     │PASS (Software) │
│     │                                       │after 3 consecutive drops     │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│02   │Backup Parent Goes Offline             │Primary Parent continues      │PASS (Software) │
│     │                                       │uninterrupted; log updated    │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│03   │Both Parents Go Offline Simultaneously │Node enters controlled Orphan │PASS (Software) │
│     │                                       │State; locks relay states     │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│04   │Intermediate Forwarding Pole Restarts  │Re-acquires route via beacon; │PASS (Software) │
│     │                                       │downstream nodes recover      │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│05   │Root Gateway Restarts                  │Children re-anchor on first   │PASS (Software) │
│     │                                       │root beacon broadcast         │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│06   │Duplicate Packet Arrives in Transit    │Suppressed by 16-entry        │PASS (Software) │
│     │                                       │deduplication sliding cache   │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│07   │Delayed Packet Arrives with Expired TTL│Packet dropped safely when    │PASS (Software) │
│     │                                       │TTL $\le 1$                   │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│08   │Invalid Route Advertisement (Rank 255) │Candidate filtered out;       │PASS (Software) │
│     │                                       │never selected as parent      │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│09   │Routing Loop Attempt (Points to Node)  │Strict rank check ($R_c < R$) │PASS (Software) │
│     │                                       │prevents loop establishment   │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│10   │Weak Link (RSSI < -115 dBm)            │Heavily penalized with high   │PASS (Software) │
│     │                                       │cost ($> 90$); avoided        │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│11   │High Packet Loss Link (PDR < 40%)      │Link Quality degraded;        │PASS (Software) │
│     │                                       │triggers alternate route search│                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│12   │RF Contention / Network Congestion     │RREQ exponential backoff      │PASS (Software) │
│     │                                       │activated (3s -> 6s -> 12s)   │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│13   │Re-entering Node Joins Network         │Discovers neighbors and       │PASS (Software) │
│     │                                       │re-establishes route cleanly  │                │
├─────┼───────────────────────────────────────┼──────────────────────────────┼────────────────┤
│14   │Asymmetric Link (TX OK, RX Lost)       │PDR degradation triggers      │PASS (Software) │
│     │                                       │backup route migration        │                │
└─────┴───────────────────────────────────────┴──────────────────────────────┴────────────────┘
```

---

## 3. Multi-Hop Forwarding & Airtime Evidence

- **Maximum Depth Tested**: 5 sequential hops (`GW -> P1 -> P2 -> P3 -> P4 -> P5`).
- **TTL Processing**: Next-hop lookup correctly updates `nextHopAddress` and decrements `ttl` on every forwarding node.
- **Delivery Integrity**: End-to-end telemetry and command execution maintained **100% data fidelity** through all hops.
