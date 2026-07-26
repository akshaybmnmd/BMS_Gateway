#include "NanoTelemetry.h"
#include <ArduinoJson.h>
#include "Config.h"

// Zero-initialize the entire struct
SensorTelemetry sensorData = {};
unsigned long lastSerialRx = 0;

void clearNanoTelemetry();

void setupNanoTelemetry()
{
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("[INFO] UART2 Initialized on RX:16, TX:17 for Nano Telemetry.");
}

void processNanoTelemetry()
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
        sensorData.acVoltage1 = doc["acV1"] | 0.0f;
        sensorData.acCurrent1 = doc["acI1"] | 0.0f;
        sensorData.acVoltage2 = doc["acV2"] | 0.0f;
        sensorData.acCurrent2 = doc["acI2"] | 0.0f;
        sensorData.dcVoltage = doc["dcV"] | 0.0f;
        sensorData.dcCurrent = doc["dcI"] | 0.0f;
        sensorData.dcPower = doc["dcW"] | 0.0f;
        sensorData.temperature = doc["tmp"] | 0.0f;
        sensorData.humidity = doc["hum"] | 0.0f;
        sensorData.pressure = doc["prs"] | 0.0f;

        sensorData.nano_connected = true;
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
  if (sensorData.nano_connected && (millis() - lastSerialRx > SERIAL_TIMEOUT_MS))
  {
    Serial.println("[WARNING] Nano Serial Telemetry Timeout!");

    // Wipe the struct clean instantly
    clearNanoTelemetry();
  }
}

void clearNanoTelemetry()
{
  // This safely zeroes out all floats and sets the boolean to false
  sensorData = {};
}