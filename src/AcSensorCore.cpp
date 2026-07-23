#include "AcSensorCore.h"
#include <ArduinoJson.h>
#include "Config.h"

// Zero-initialize the entire struct
AcTelemetry acTelemetry = {};
unsigned long lastSerialRx = 0;

void clearAcTelemetry();

void setupAcSensors()
{
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("[INFO] UART2 Initialized on RX:16, TX:17 for Nano Telemetry.");
}

void readAcSensors()
{
  while (Serial2.available())
  {
    char payload[AC_PAYLOAD_BUFFER_SIZE];
    size_t len = Serial2.readBytesUntil('\n', payload, sizeof(payload) - 1);
    payload[len] = '\0';

    if (len > 0 && payload[0] == '{')
    {
      StaticJsonDocument<AC_PAYLOAD_BUFFER_SIZE> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error)
      {
        acTelemetry.acVoltage1 = doc["acV1"] | 0.0f;
        acTelemetry.acCurrent1 = doc["acI1"] | 0.0f;
        acTelemetry.acVoltage2 = doc["acV2"] | 0.0f;
        acTelemetry.acCurrent2 = doc["acI2"] | 0.0f;
        acTelemetry.dcVoltage = doc["dcV"] | 0.0f;
        acTelemetry.dcCurrent = doc["dcI"] | 0.0f;
        acTelemetry.dcPower = doc["dcW"] | 0.0f;
        acTelemetry.temperature = doc["tmp"] | 0.0f;
        acTelemetry.humidity = doc["hum"] | 0.0f;
        acTelemetry.pressure = doc["prs"] | 0.0f;

        acTelemetry.nano_connected = true;
        lastSerialRx = millis();
      }
      else
      {
        Serial.print("[ERROR] JSON Parse Failed: ");
        Serial.println(error.c_str());
      }
    }
  }

  // Safety net timeout
  if (acTelemetry.nano_connected && (millis() - lastSerialRx > SERIAL_TIMEOUT_MS))
  {
    Serial.println("[WARNING] Nano Serial Telemetry Timeout!");

    // Wipe the struct clean instantly
    clearAcTelemetry();
  }
}

void clearAcTelemetry()
{
  // This safely zeroes out all floats and sets the boolean to false
  acTelemetry = {};
}