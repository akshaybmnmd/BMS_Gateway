#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// --- Network & Hardware MACs ---
const std::string BMS1_MAC = "a5:c2:39:1d:e6:2e";
const std::string BMS2_MAC = "a5:c2:39:1d:e5:9b";

const char *const BMS_SERVICE_UUID = "FF00";
const char *const BMS_CHAR_NOTIFY_UUID = "FF01";
const char *const BMS_CHAR_WRITE_UUID = "FF02";

// --- Timing & Intervals ---
constexpr unsigned long READ_INTERVAL_MS = 10000;
constexpr unsigned long TIMEOUT_MS = 2000;
constexpr unsigned long COOLDOWN_MS = 200;
constexpr unsigned long SERIAL_TIMEOUT_MS = 5000;
constexpr unsigned long BMS_DELAY_MS = 500;
constexpr unsigned long STALE_TIMEOUT_MS = 5 * 60 * 1000; // 5 mins
constexpr unsigned long LOG_INTERVAL_MS = 5 * 60 * 1000;  // 5 mins

// --- Buffers & Sizes ---
constexpr size_t BMS_BUFFER_SIZE = 64;
constexpr size_t AC_PAYLOAD_BUFFER_SIZE = 384;

// --- Fan & Thermal Tuning ---
constexpr float FAN_START_TEMP = 30.0f;     // Ambient start
constexpr float FAN_FULL_TEMP = 50.0f;      // Ambient max
constexpr float ESP_FAN_START_TEMP = 50.0f; // ESP32 core start
constexpr float ESP_FAN_FULL_TEMP = 70.0f;  // ESP32 core max
constexpr int FAN_MIN_DUTY = 80;
constexpr int FAN_MAX_DUTY = 255;

struct BmsData
{
  uint8_t id;
  float voltage;
  float current;
  float lastCurrent;
  float power;
  float maxTemp;
  int soc;
  int lastSoC;
  bool isConnected;
  bool dataReady;
  uint8_t buffer[BMS_BUFFER_SIZE];
  size_t bufferIdx;
  unsigned long lastUpdateTime;
  float remainingCapacityAh;
  uint16_t cycleCount;
  bool chargeMosEnabled;
  bool dischargeMosEnabled;
};

enum SystemStatus
{
  STATUS_IDLE,
  STATUS_CHARGING,
  STATUS_DISCHARGING,
  STATUS_ERROR
};

enum GracePeriodStatus
{
  GRACE_NONE,   // BLE is connected, data is live
  GRACE_ACTIVE, // BLE dropped, operating safely on cached data (< 5 mins old)
  GRACE_EXPIRED // Timeout exceeded, data is dangerously stale
};

struct BmsMetrics
{
  float voltage;
  float current;
  float power;
  float maxTemp;
  float remainingCapacityAh;
  int soc;
  uint16_t cycleCount;
  bool isConnected;
  bool chargeMosEnabled;
  bool dischargeMosEnabled;
  unsigned long lastUpdateTime;
};

struct SystemMetrics
{
  float netCurrent;
  float currentDelta;
  float netPower;
  float powerDelta;
  int avgSoc;
  int socDelta;
  float minVoltage;
  float voltageDelta;
  float peakTemp;
  SystemStatus status;
  GracePeriodStatus graceStatus;
  float acVoltage;
  float acCurrent;
  float acVoltage2;
  float acCurrent2;
  float acPower2;
  float dcVoltage;
  float dcCurrent;
  float dcPower;
  float acPower;
  float envTemp;
  float espTemp;
  float envHum;
  float envPres;
  bool nano_connected;
  bool relayClosed;
  int fan_speed;
  BmsMetrics bms1;
  BmsMetrics bms2;
};

static const char *statusToString(SystemStatus status)
{
  switch (status)
  {
  case STATUS_IDLE:
    return "IDLE";
  case STATUS_CHARGING:
    return "CHARGING";
  case STATUS_DISCHARGING:
    return "DISCHARGING";
  default:
    return "ERROR";
  }
}

#endif
