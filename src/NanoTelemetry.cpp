#include "NanoTelemetry.h"
#include <ArduinoJson.h>
#include "Config.h"

// Use the same metrics mutex to protect sensorData updates
extern SemaphoreHandle_t metricsMutex;

SensorTelemetry sensorData = {};
unsigned long lastSerialRx = 0;
const int NANO_RX_PIN = 16;
const int NANO_TX_PIN = 17;
const int NANO_BAUD = 9600;

// Non-blocking reset scheduler state
static bool nanoResetPending = false;
static unsigned long nanoResetReleaseAt = 0;

static void _scheduleNanoResetRelease(unsigned long when)
{
  nanoResetPending = true;
  nanoResetReleaseAt = when;
}

void clearNanoTelemetry();

void setupNanoTelemetry()
{
  pinMode(NANO_RST_PIN, OUTPUT);
  digitalWrite(NANO_RST_PIN, HIGH);

  Serial2.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  // Lower serial timeout to avoid blocking reads
  Serial2.setTimeout(10);

  lastSerialRx = millis();
}

void resetNano()
{
  Serial.println("[SYSTEM] Triggering hardware reset on Nano (non-blocking)...");

  // Drive reset low and schedule release without blocking
  digitalWrite(NANO_RST_PIN, LOW);
  // schedule a release after 50 ms
  static unsigned long releaseTime = 0;
  releaseTime = millis() + 50;

  // Store pending release time in static variables accessible to the processing loop
  // We'll use module-level statics declared below to track the pending reset
  (void)releaseTime; // releaseTime mirrored into module state by setting below

  // Set module-level pending flag and expiry
  _scheduleNanoResetRelease(millis() + 50);

  lastSerialRx = millis();
}

void processNanoTelemetry()
{
  while (Serial2.available())
  {
    char payload[AC_PAYLOAD_BUFFER_SIZE];
    size_t len = Serial2.readBytesUntil('\n', payload, sizeof(payload) - 1);
    if (len == 0)
      continue;

    // Ensure null-termination
    payload[len] = '\0';

    if (payload[0] != '{')
    {
      Serial.println("[ERROR] Invalid payload received from Nano.");
      continue;
    }

    StaticJsonDocument<AC_PAYLOAD_BUFFER_SIZE> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print("[ERROR] JSON Parse Failed: ");
      Serial.println(error.c_str());
      continue;
    }

    // Extract values into locals first
    float acV1 = doc["acV1"] | 0.0f;
    float acI1 = doc["acI1"] | 0.0f;
    float acV2 = doc["acV2"] | 0.0f;
    float acI2 = doc["acI2"] | 0.0f;
    float dcV = doc["dcV"] | 0.0f;
    float dcI = doc["dcI"] | 0.0f;
    float dcW = doc["dcW"] | 0.0f;
    float tmp = doc["tmp"] | 0.0f;
    float hum = doc["hum"] | 0.0f;
    float prs = doc["prs"] | 0.0f;

    // Update shared sensorData under mutex to avoid races
    if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      sensorData.acVoltage1 = acV1;
      sensorData.acCurrent1 = acI1;
      sensorData.acVoltage2 = acV2;
      sensorData.acCurrent2 = acI2;
      sensorData.dcVoltage = dcV;
      sensorData.dcCurrent = dcI;
      sensorData.dcPower = dcW;
      sensorData.temperature = tmp;
      sensorData.humidity = hum;
      sensorData.pressure = prs;
      sensorData.nano_connected = true;
      lastSerialRx = millis();
      xSemaphoreGive(metricsMutex);
    }
    else
    {
      Serial.println("[WARN] Failed to acquire metrics mutex for Nano telemetry update.");
    }
  }

  // Check for telemetry timeout while holding mutex briefly when clearing
  if (sensorData.nano_connected && (millis() - lastSerialRx > SERIAL_TIMEOUT_MS))
  {
    Serial.println("[WARNING] Nano Serial Telemetry Timeout!");

    clearNanoTelemetry();

    resetNano();
  }

  // Release Nano reset line if a non-blocking reset was scheduled
  if (nanoResetPending && millis() >= nanoResetReleaseAt)
  {
    digitalWrite(NANO_RST_PIN, HIGH);
    nanoResetPending = false;
    nanoResetReleaseAt = 0;
    Serial.println("[SYSTEM] Nano reset pulse complete, released reset line.");
  }
}

void clearNanoTelemetry()
{
  if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    sensorData = {};
    xSemaphoreGive(metricsMutex);
  }
  else
  {
    // Best-effort clear if mutex cannot be obtained
    sensorData = {};
  }
}