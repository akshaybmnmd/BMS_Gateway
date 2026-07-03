#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

const std::string BMS1_MAC = "a5:c2:39:1d:e6:2e";
const std::string BMS2_MAC = "a5:c2:39:1d:e5:9b";

const char* const BMS_SERVICE_UUID = "FF00";
const char* const BMS_CHAR_NOTIFY_UUID = "FF01";
const char* const BMS_CHAR_WRITE_UUID = "FF02";

const unsigned long READ_INTERVAL_MS = 10000;
const unsigned long TIMEOUT_MS = 2000;
const unsigned long COOLDOWN_MS = 200;

struct BmsData {
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
  uint8_t buffer[64];
  size_t bufferIdx;
  unsigned long lastUpdateTime;
};

enum SystemStatus {
  STATUS_IDLE,
  STATUS_CHARGING,
  STATUS_DISCHARGING,
  STATUS_ERROR
};

enum GracePeriodStatus {
  GRACE_NONE,     // BLE is connected, data is live
  GRACE_ACTIVE,   // BLE dropped, operating safely on cached data (< 5 mins old)
  GRACE_EXPIRED   // Timeout exceeded, data is dangerously stale
};

struct SystemMetrics {
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
  float acPower;
};

static const char* statusToString(SystemStatus status) {
  switch (status) {
    case STATUS_IDLE: return "IDLE";
    case STATUS_CHARGING: return "CHARGING";
    case STATUS_DISCHARGING: return "DISCHARGING";
    default: return "ERROR";
  }
}

#endif