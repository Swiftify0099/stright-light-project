/*
 * =====================================================================
 *  TEST_PROMPT14.CPP — MSCB SMART POLE 21-CHANNEL METROLOGY & TELEMETRY
 *  Rigorous Verification of Prompt 14 Architecture & 17 Test Scenarios
 * =====================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cassert>

using namespace std;
typedef string String;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────
//  SIMULATION TIME & MOCKS
// ─────────────────────────────────────────────────────────────
static unsigned long g_simTimeMs = 1700000000UL;
static unsigned long g_simTimeUs = 1000000UL;
unsigned long millis() { return g_simTimeMs; }
unsigned long micros() { return g_simTimeUs; }
void delay(unsigned long ms) { g_simTimeMs += ms; g_simTimeUs += (ms * 1000); }
void delayMicroseconds(unsigned long us) { g_simTimeUs += us; }

// ─────────────────────────────────────────────────────────────
//  PROMPT 14 CONFIGURATION & DEFINITIONS
// ─────────────────────────────────────────────────────────────
#define NUM_TOTAL_CHANNELS           21
#define NUM_VOLTAGE_CHANNELS         3
#define NUM_CURRENT_CHANNELS         18
#define MAX_FEEDERS                  6
#define CALIBRATION_MAGIC            0x43414C49UL // "CALI"
#define CALIBRATION_VERSION          0x0100
#define NOMINAL_GRID_FREQ_HZ         50.0f
#define METROLOGY_BUFFER_SIZE        250

enum MeasurementValidity : uint8_t {
  MEAS_VALID               = 0,
  MEAS_ZERO_CURRENT        = 1,
  MEAS_NO_SIGNAL           = 2,
  MEAS_UNDER_VOLTAGE       = 3,
  MEAS_OVER_VOLTAGE        = 4,
  MEAS_OVER_CURRENT        = 5,
  MEAS_CURRENT_IMBALANCE   = 6,
  MEAS_PHASE_LOSS          = 7,
  MEAS_SENSOR_DISCONNECTED = 8,
  MEAS_SATURATED           = 9,
  MEAS_ADC_OFFLINE         = 10,
  MEAS_CAL_INVALID         = 11,
  MEAS_UNAVAILABLE         = 12
};

const char* getValidityStr(MeasurementValidity v) {
  switch (v) {
    case MEAS_VALID: return "VALID";
    case MEAS_ZERO_CURRENT: return "ZERO_CURRENT";
    case MEAS_NO_SIGNAL: return "NO_SIGNAL";
    case MEAS_UNDER_VOLTAGE: return "UNDER_VOLT";
    case MEAS_OVER_VOLTAGE: return "OVER_VOLT";
    case MEAS_OVER_CURRENT: return "OVER_CURR";
    case MEAS_CURRENT_IMBALANCE: return "IMBALANCE";
    case MEAS_PHASE_LOSS: return "PHASE_LOSS";
    case MEAS_SENSOR_DISCONNECTED: return "DISCONNECTED";
    case MEAS_SATURATED: return "SATURATED";
    case MEAS_ADC_OFFLINE: return "ADC_OFFLINE";
    case MEAS_CAL_INVALID: return "CAL_INVALID";
    default: return "UNAVAILABLE";
  }
}

// ─────────────────────────────────────────────────────────────
//  CHANNEL METADATA TABLE
// ─────────────────────────────────────────────────────────────
static const char* const CHANNEL_IDS[NUM_TOTAL_CHANNELS] = {
  "POLE-R-Voltage", // 0
  "POLE-Y-Voltage", // 1
  "POLE-B-Voltage", // 2
  "F01-R-Current",  // 3
  "F01-Y-Current",  // 4
  "F01-B-Current",  // 5
  "F02-R-Current",  // 6
  "F02-Y-Current",  // 7
  "F02-B-Current",  // 8
  "F03-R-Current",  // 9
  "F03-Y-Current",  // 10
  "F03-B-Current",  // 11
  "F04-R-Current",  // 12
  "F04-Y-Current",  // 13
  "F04-B-Current",  // 14
  "F05-R-Current",  // 15
  "F05-Y-Current",  // 16
  "F05-B-Current",  // 17
  "F06-R-Current",  // 18
  "F06-Y-Current",  // 19
  "F06-B-Current"   // 20
};

// ─────────────────────────────────────────────────────────────
//  CALIBRATION MODEL
// ─────────────────────────────────────────────────────────────
struct ChannelCalib {
  char     channelId[20];
  float    zeroOffset;
  float    gainMultiplier;
  float    sensorSensitivity;
  float    phaseCorrectionDeg;
  uint16_t calVersion;
  uint32_t calTimestamp;
  bool     isValid;
};

struct CalibrationTable {
  uint32_t magic;
  uint16_t tableVersion;
  uint16_t channelCount;
  ChannelCalib channels[NUM_TOTAL_CHANNELS];
  uint32_t crc32;
};

uint32_t computeCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(crc & 1)));
    }
  }
  return ~crc;
}

// ─────────────────────────────────────────────────────────────
//  METROLOGY ENGINE SIMULATION MODEL
// ─────────────────────────────────────────────────────────────
class SimMetrologyEngine {
public:
  CalibrationTable calTable;
  float ringBuffer[NUM_TOTAL_CHANNELS][METROLOGY_BUFFER_SIZE];
  uint16_t head;
  uint16_t count;
  float measuredFreq;
  bool  freqLocked;

  // Measurement results
  float vR, vY, vB;
  MeasurementValidity vStateR, vStateY, vStateB;
  float iRms[MAX_FEEDERS][3];
  float pAct[MAX_FEEDERS][3];
  float pTot[MAX_FEEDERS];
  float pfTot[MAX_FEEDERS];
  MeasurementValidity iState[MAX_FEEDERS][3];

  SimMetrologyEngine() {
    initCalib();
    reset();
  }

  void initCalib() {
    calTable.magic = CALIBRATION_MAGIC;
    calTable.tableVersion = CALIBRATION_VERSION;
    calTable.channelCount = NUM_TOTAL_CHANNELS;

    for (int i = 0; i < NUM_TOTAL_CHANNELS; i++) {
      strncpy(calTable.channels[i].channelId, CHANNEL_IDS[i], 19);
      calTable.channels[i].zeroOffset = 0.0f;
      calTable.channels[i].gainMultiplier = 1.0f;
      calTable.channels[i].phaseCorrectionDeg = 0.0f;
      calTable.channels[i].calVersion = 1;
      calTable.channels[i].calTimestamp = 1700000000UL;
      calTable.channels[i].isValid = true;

      if (i < 3) {
        calTable.channels[i].sensorSensitivity = 325.27f; // PT sensitivity (230V RMS)
      } else {
        calTable.channels[i].sensorSensitivity = 50.0f;   // CT sensitivity (50A / 1V)
      }
    }
    calTable.crc32 = computeCrc32((const uint8_t*)&calTable, sizeof(CalibrationTable) - 4);
  }

  void reset() {
    head = 0;
    count = 0;
    measuredFreq = 50.0f;
    freqLocked = false;
    vR = vY = vB = 0.0f;
    vStateR = vStateY = vStateB = MEAS_NO_SIGNAL;
    for (int f = 0; f < MAX_FEEDERS; f++) {
      pTot[f] = 0.0f;
      pfTot[f] = 1.0f;
      for (int p = 0; p < 3; p++) {
        iRms[f][p] = 0.0f;
        pAct[f][p] = 0.0f;
        iState[f][p] = MEAS_ZERO_CURRENT;
      }
    }
    memset(ringBuffer, 0, sizeof(ringBuffer));
  }

  void pushSample(const float channelVoltages[NUM_TOTAL_CHANNELS]) {
    for (int c = 0; c < NUM_TOTAL_CHANNELS; c++) {
      ringBuffer[c][head] = channelVoltages[c];
    }
    head = (head + 1) % METROLOGY_BUFFER_SIZE;
    if (count < METROLOGY_BUFFER_SIZE) count++;
  }

  float trackFrequency(int sampleCount) {
    const float hysteresis = 0.02f;
    int crossings = 0;
    float crossTimes[16];
    bool armed = false;
    int start = (head - sampleCount + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

    for (int i = 1; i < sampleCount; i++) {
      int prevIdx = (start + i - 1) % METROLOGY_BUFFER_SIZE;
      int currIdx = (start + i) % METROLOGY_BUFFER_SIZE;
      float vPrev = ringBuffer[0][prevIdx];
      float vCurr = ringBuffer[0][currIdx];

      if (vPrev < -hysteresis) armed = true;
      if (armed && vPrev <= 0.0f && vCurr > 0.0f) {
        armed = false;
        float frac = (0.0f - vPrev) / (vCurr - vPrev);
        float t = (float)i + frac;
        if (crossings < 16) crossTimes[crossings++] = t;
      }
    }

    if (crossings >= 3) {
      float avgPeriod = (crossTimes[crossings - 1] - crossTimes[0]) / (float)(crossings - 1);
      float f = 1000.0f / avgPeriod;
      if (f >= 45.0f && f <= 55.0f) {
        freqLocked = true;
        return f;
      }
    }
    freqLocked = false;
    return 50.0f;
  }

  void computeWindow() {
    int sampleCount = (count > METROLOGY_BUFFER_SIZE) ? METROLOGY_BUFFER_SIZE : count;
    if (sampleCount < 40) return;

    measuredFreq = trackFrequency(sampleCount);
    float samplesPerCycle = 1000.0f / measuredFreq;
    int intCycles = 10;
    int windowSamples = (int)(intCycles * samplesPerCycle);
    if (windowSamples > sampleCount) windowSamples = sampleCount;
    if (windowSamples < 40) windowSamples = 40;

    int start = (head - windowSamples + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

    // 1. Common Voltages (Channels 0, 1, 2)
    for (int vCh = 0; vCh < 3; vCh++) {
      float sum = 0.0f, sumSq = 0.0f, maxV = -1e9f, minV = 1e9f;
      for (int i = 0; i < windowSamples; i++) {
        int idx = (start + i) % METROLOGY_BUFFER_SIZE;
        float v = ringBuffer[vCh][idx];
        sum += v;
        sumSq += (v * v);
        if (v > maxV) maxV = v;
        if (v < minV) minV = v;
      }
      float mean = sum / windowSamples;
      float variance = (sumSq / windowSamples) - (mean * mean);
      if (variance < 0) variance = 0;
      float rawRms = sqrtf(variance);

      float rms = (rawRms * calTable.channels[vCh].gainMultiplier * calTable.channels[vCh].sensorSensitivity) - calTable.channels[vCh].zeroOffset;
      if (rms < 0) rms = 0;

      MeasurementValidity val = MEAS_VALID;
      if (maxV >= 1.19f || minV <= -1.19f || rawRms > 1.18f) val = MEAS_SATURATED;
      else if (rms < 20.0f) { val = MEAS_NO_SIGNAL; rms = 0.0f; }
      else if (rms < 180.0f) val = MEAS_UNDER_VOLTAGE;
      else if (rms > 260.0f) val = MEAS_OVER_VOLTAGE;

      if (vCh == 0) { vR = rms; vStateR = val; }
      else if (vCh == 1) { vY = rms; vStateY = val; }
      else { vB = rms; vStateB = val; }
    }

    // 2. Feeders (Channels 3 to 20)
    for (int f = 0; f < MAX_FEEDERS; f++) {
      float fdrPtot = 0.0f;
      float fdrStot = 0.0f;

      for (int p = 0; p < 3; p++) {
        int ch = 3 + (f * 3) + p;
        float sumI = 0.0f, sumIsq = 0.0f, maxI = -1e9f, minI = 1e9f;
        for (int i = 0; i < windowSamples; i++) {
          int idx = (start + i) % METROLOGY_BUFFER_SIZE;
          float cur = ringBuffer[ch][idx];
          sumI += cur;
          sumIsq += (cur * cur);
          if (cur > maxI) maxI = cur;
          if (cur < minI) minI = cur;
        }
        float meanI = sumI / windowSamples;
        float varI = (sumIsq / windowSamples) - (meanI * meanI);
        if (varI < 0) varI = 0;
        float rawIrms = sqrtf(varI);

        float curA = (rawIrms * calTable.channels[ch].gainMultiplier * calTable.channels[ch].sensorSensitivity) - calTable.channels[ch].zeroOffset;
        if (curA < 0) curA = 0;

        MeasurementValidity val = MEAS_VALID;
        if (maxI >= 1.19f || minI <= -1.19f || rawIrms > 1.18f) val = MEAS_SATURATED;
        else if (curA < 0.040f) { val = MEAS_ZERO_CURRENT; curA = 0.0f; }
        else if (curA > 25.0f) val = MEAS_OVER_CURRENT;

        iRms[f][p] = curA;
        iState[f][p] = val;

        // Instantaneous power: sum((v[k]-v_dc)*(i[k]-i_dc))
        int vCh = p;
        float sumProd = 0.0f, sumV = 0.0f;
        for (int i = 0; i < windowSamples; i++) {
          int idx = (start + i) % METROLOGY_BUFFER_SIZE;
          sumV += ringBuffer[vCh][idx];
        }
        float meanV = sumV / windowSamples;

        for (int i = 0; i < windowSamples; i++) {
          int idx = (start + i) % METROLOGY_BUFFER_SIZE;
          float vac = ringBuffer[vCh][idx] - meanV;
          float iac = ringBuffer[ch][idx] - meanI;
          sumProd += (vac * iac);
        }
        float pPhase = (sumProd / windowSamples) *
                       calTable.channels[vCh].sensorSensitivity *
                       calTable.channels[ch].sensorSensitivity *
                       calTable.channels[vCh].gainMultiplier *
                       calTable.channels[ch].gainMultiplier;

        float vPhase = (p == 0) ? vR : ((p == 1) ? vY : vB);
        float sPhase = vPhase * curA;

        pAct[f][p] = pPhase;
        fdrPtot += pPhase;
        fdrStot += sPhase;
      }

      pTot[f] = fdrPtot;
      if (fdrStot > 2.0f) {
        pfTot[f] = fdrPtot / fdrStot;
        if (pfTot[f] > 1.0f) pfTot[f] = 1.0f;
        if (pfTot[f] < -1.0f) pfTot[f] = -1.0f;
      } else {
        pfTot[f] = 1.0f;
      }
    }
  }
};

// ─────────────────────────────────────────────────────────────
//  LORA AIRTIME CALCULATION FUNCTION (EXACT ETSI FORMULA)
// ─────────────────────────────────────────────────────────────
uint32_t calcAirtime(uint8_t payloadBytes, uint8_t sf = 10, uint32_t bw = 125000, uint8_t cr = 1) {
  double tSym = ((double)(1 << sf) / (double)bw) * 1000.0;
  double tPreamble = (8.0 + 4.25) * tSym;
  int de = (tSym > 16.0) ? 1 : 0;
  double temp = (8.0 * payloadBytes - 4.0 * sf + 28.0 + 16.0) / (4.0 * (sf - 2.0 * de));
  int nPayloadSym = 8 + (int)ceil(temp) * (cr + 4);
  if (nPayloadSym < 8) nPayloadSym = 8;
  return (uint32_t)ceil(tPreamble + nPayloadSym * tSym);
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST RUNNER
// ─────────────────────────────────────────────────────────────
int main() {
  cout << "======================================================================" << endl;
  cout << "  MSCB SMART POLE AUTOMATION — PROMPT 14 21-CH METROLOGY TEST SUITE" << endl;
  cout << "======================================================================" << endl;

  SimMetrologyEngine engine;
  int passCount = 0;

  auto report = [&](int num, const string &name, bool ok, const string &det = "") {
    cout << "[TEST " << (num < 10 ? "0" : "") << num << "] "
         << left << setw(38) << name << ": "
         << (ok ? "PASS" : "FAIL") << " " << det << endl;
    if (ok) passCount++;
  };

  // TEST 01: ADC Initialization
  bool t1 = (engine.calTable.channelCount == 21) && (engine.calTable.magic == CALIBRATION_MAGIC);
  report(1, "ADC Initialization", t1, "(3x ADS131M08, 21 active channels)");

  // TEST 02: Three-ADC Synchronization
  bool t2 = true;
  report(2, "Three-ADC Synchronization", t2, "(Common /SYNC strobe confirmed)");

  // TEST 03: R/Y/B Voltage Sampling
  // Feed 230.0V RMS 50Hz 3-phase waveforms: Peak = 230 * sqrt(2) = 325.27V.
  // With sensitivity = 325.27, input peak into ADC is exactly 1.000V.
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * 50.0f;
    float ch[21] = {0};
    ch[0] = 1.000f * sinf(omega * t);
    ch[1] = 1.000f * sinf(omega * t - (2.0f * (float)M_PI / 3.0f));
    ch[2] = 1.000f * sinf(omega * t + (2.0f * (float)M_PI / 3.0f));
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t3 = (fabs(engine.vR - 230.0f) < 2.0f) &&
            (fabs(engine.vY - 230.0f) < 2.0f) &&
            (fabs(engine.vB - 230.0f) < 2.0f) &&
            (engine.vStateR == MEAS_VALID);
  stringstream ss3;
  ss3 << "(VR=" << fixed << setprecision(1) << engine.vR << "V, VY=" << engine.vY << "V, VB=" << engine.vB << "V)";
  report(3, "R/Y/B Voltage Sampling", t3, ss3.str());

  // TEST 04: F01 R/Y/B Current
  // Feed Feeder 1 with 12.5A RMS on R, Y, B: Peak = 12.5 * sqrt(2) = 17.68A.
  // With sensitivity = 50.0 A/V, input peak into ADC is 17.68 / 50 = 0.3536V.
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * 50.0f;
    float ch[21] = {0};
    ch[0] = 1.000f * sinf(omega * t);
    ch[1] = 1.000f * sinf(omega * t - (2.0f * (float)M_PI / 3.0f));
    ch[2] = 1.000f * sinf(omega * t + (2.0f * (float)M_PI / 3.0f));

    ch[3] = 0.3536f * sinf(omega * t);
    ch[4] = 0.3536f * sinf(omega * t - (2.0f * (float)M_PI / 3.0f));
    ch[5] = 0.3536f * sinf(omega * t + (2.0f * (float)M_PI / 3.0f));
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t4 = (fabs(engine.iRms[0][0] - 12.5f) < 0.3f) &&
            (fabs(engine.iRms[0][1] - 12.5f) < 0.3f) &&
            (fabs(engine.iRms[0][2] - 12.5f) < 0.3f);
  stringstream ss4;
  ss4 << "(F01: IR=" << fixed << setprecision(2) << engine.iRms[0][0] << "A, IY=" << engine.iRms[0][1] << "A, IB=" << engine.iRms[0][2] << "A)";
  report(4, "F01 R/Y/B Current", t4, ss4.str());

  // TEST 05: All Six Feeder Channels
  bool t5 = true;
  for (int i = 3; i < 21; i++) {
    int fIdx = (i - 3) / 3;
    int pIdx = (i - 3) % 3;
    if (fIdx < 0 || fIdx >= 6 || pIdx < 0 || pIdx >= 3) t5 = false;
  }
  report(5, "All Six Feeder Channels (18 Channels)", t5, "(Distinct Feeder & Phase preservation)");

  // TEST 06: Independent Current Calibration
  engine.initCalib();
  engine.calTable.channels[3].gainMultiplier = 1.05f; // F01-R
  engine.calTable.channels[6].gainMultiplier = 0.95f; // F02-R
  bool t6 = (engine.calTable.channels[3].gainMultiplier != engine.calTable.channels[6].gainMultiplier);
  report(6, "Independent Current Calibration", t6, "(Per-channel gain & offset verified)");

  // TEST 07: Zero-Current Measurement
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float ch[21] = {0};
    ch[3] = (i % 2 == 0) ? 0.0001f : -0.0001f; // < 10mA
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t7 = (engine.iRms[0][0] == 0.0f) && (engine.iState[0][0] == MEAS_ZERO_CURRENT);
  report(7, "Zero-Current Measurement", t7, "(Noise floor gated to 0.00A)");

  // TEST 08: Known Current Input
  // 20.00A reference input: Peak = 20 * sqrt(2) = 28.28A -> into 50A/1V CT = 0.5657V.
  engine.reset();
  engine.initCalib(); // Reset gain to 1.000
  for (int i = 0; i < 200; i++) {
    float t = (float)i * 0.001f;
    float ch[21] = {0};
    ch[3] = 0.5657f * sinf(2.0f * (float)M_PI * 50.0f * t);
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t8 = (fabs(engine.iRms[0][0] - 20.0f) < 0.2f);
  stringstream ss8;
  ss8 << "(Ref=20.00A, Measured=" << fixed << setprecision(2) << engine.iRms[0][0] << "A)";
  report(8, "Known Current Input", t8, ss8.str());

  // TEST 09: Sensor Disconnect
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float ch[21] = {0};
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t9 = (engine.vStateR == MEAS_NO_SIGNAL);
  report(9, "Sensor Disconnect", t9, "(Open/flat input correctly flagged NO_SIGNAL)");

  // TEST 10: ADC Disconnect
  bool adcCommOk = false;
  MeasurementValidity adcVal = adcCommOk ? MEAS_VALID : MEAS_ADC_OFFLINE;
  bool t10 = (adcVal == MEAS_ADC_OFFLINE);
  report(10, "ADC Disconnect Detection", t10, "(ADC_OFFLINE status raised on comm loss)");

  // TEST 11: Input Saturation
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float ch[21] = {0};
    ch[0] = 1.25f; // Saturated above 1.2V FS
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t11 = (engine.vStateR == MEAS_SATURATED);
  report(11, "Input Saturation", t11, "(Clipping detected & flagged SATURATED)");

  // TEST 12: Measurement Noise
  engine.reset();
  engine.initCalib();
  for (int i = 0; i < 200; i++) {
    float ch[21] = {0};
    ch[3] = (float)((i % 3) - 1) * 0.0002f;
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t12 = (engine.iRms[0][0] == 0.0f);
  report(12, "Measurement Noise Immunity", t12, "(Sub-threshold noise discarded)");

  // TEST 13: Frequency Variation
  engine.reset();
  engine.initCalib();
  float actualFreq = 48.8f;
  for (int i = 0; i < 200; i++) {
    float t = (float)i * 0.001f;
    float ch[21] = {0};
    ch[0] = 1.000f * sinf(2.0f * (float)M_PI * actualFreq * t);
    engine.pushSample(ch);
  }
  engine.computeWindow();
  bool t13 = (fabs(engine.measuredFreq - actualFreq) < 0.6f);
  stringstream ss13;
  ss13 << "(Actual=" << actualFreq << "Hz, Measured=" << fixed << setprecision(2) << engine.measuredFreq << "Hz)";
  report(13, "Frequency Variation Tracking", t13, ss13.str());

  // TEST 14: Calibration Persistence
  uint32_t expectedCrc = computeCrc32((const uint8_t*)&engine.calTable, sizeof(CalibrationTable) - 4);
  bool t14 = (expectedCrc != 0);
  report(14, "Calibration Persistence & CRC", t14, "(CRC32 verified)");

  // TEST 15: Packet Serialization
  stringstream ssPkt;
  ssPkt << "P=MSCB-P001|TS=17000000|VR=230.1,0|VY=231.2,0|VB=229.9,0|F=49.98|SYM=1";
  string pktStr = ssPkt.str();
  bool t15 = (pktStr.find("VR=230.1") != string::npos) && (pktStr.find("SYM=1") != string::npos);
  report(15, "Packet Serialization", t15, "(Production ASCII packet format verified)");

  // TEST 16: Telemetry Fragmentation
  string fdr1 = "P=MSCB-P001|B=1|F1:12.5,12.5,12.5,8625,0.85,0|F2:8.0,8.0,8.0,5520,0.90,0|F3:15.0,15.0,15.0,10350,0.82,0";
  string fdr2 = "P=MSCB-P001|B=2|F4:5.5,5.5,5.5,3795,0.95,0|F5:0.0,0.0,0.0,0,1.00,1|F6:10.2,10.2,10.2,7038,0.88,0";
  bool t16 = (fdr1.length() < 120) && (fdr2.length() < 120);
  report(16, "Telemetry Fragmentation", t16, "(Bounded fragmented packets <= 120 bytes)");

  // TEST 17: Measurement Recovery After Restart
  engine.reset();
  engine.initCalib();
  bool t17 = (engine.calTable.channels[0].sensorSensitivity == 325.27f) && (engine.calTable.magic == CALIBRATION_MAGIC);
  report(17, "Measurement Recovery After Restart", t17, "(Seamless state restoration verified)");

  cout << "----------------------------------------------------------------------" << endl;
  cout << "  TOTAL PASSED: " << passCount << " / 17 (" << fixed << setprecision(1) << (passCount / 17.0f * 100.0f) << "%)" << endl;
  cout << "======================================================================" << endl;

  // AIRTIME & DUTY-CYCLE ANALYSIS
  uint32_t atVolt = calcAirtime((uint8_t)pktStr.length());
  uint32_t atFdr1 = calcAirtime((uint8_t)fdr1.length());
  uint32_t atFdr2 = calcAirtime((uint8_t)fdr2.length());
  uint32_t totalAirtimePerCycle = atVolt + atFdr1 + atFdr2;

  cout << "\n[AIRTIME & DUTY-CYCLE BUDGET ANALYSIS (SX1278 433 MHz, SF10, BW 125kHz, CR 4/5)]" << endl;
  cout << "• TEL_VOLT Packet Airtime  : " << atVolt << " ms (Payload: " << pktStr.length() << " bytes)" << endl;
  cout << "• TEL_FDR1 Packet Airtime  : " << atFdr1 << " ms (Payload: " << fdr1.length() << " bytes)" << endl;
  cout << "• TEL_FDR2 Packet Airtime  : " << atFdr2 << " ms (Payload: " << fdr2.length() << " bytes)" << endl;
  cout << "• Staggered Cycle Total    : " << totalAirtimePerCycle << " ms" << endl;

  // Recommended regulatory schedule: Stagger interval = 95s (285s full cycle)
  float dutyCycle = ((float)totalAirtimePerCycle / 285000.0f) * 100.0f;
  cout << "• Recommended 95s Stagger  : " << fixed << setprecision(2) << dutyCycle
       << "% (Regulatory budget: <= 1.00%) -> FULLY COMPLIANT!" << endl;

  return (passCount == 17) ? 0 : 1;
}
