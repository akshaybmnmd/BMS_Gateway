#include "AcSensorCore.h"
#include <ArduinoJson.h>

float acVoltage = 0.0;
float acCurrent = 0.0;
float acVoltage2 = 0.0;
float acCurrent2 = 0.0;
float dcVoltage = 0.0;
float dcCurrent = 0.0;
float dcPower = 0.0;
float envTemp = 0.0;
float envHum = 0.0;
float envPres = 0.0;
bool nanoConnected = false;

unsigned long lastSerialRx = 0;

void setupAcSensors()
{
  // ESP32 Hardware Serial 2 maps to RX:16 and TX:17 by default
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("[INFO] UART2 Initialized on RX:16, TX:17 for Nano Telemetry.");
}

void readAcSensors()
{
  while (Serial2.available())
  {
    char payload[384];
    size_t len = Serial2.readBytesUntil('\n', payload, sizeof(payload) - 1);
    payload[len] = '\0'; // Null-terminate the string

    // Basic validation to ensure we are looking at a JSON string
    if (len > 0 && payload[0] == '{')
    {
      StaticJsonDocument<384> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error)
      {
        // Map all incoming telemetry fields
        acVoltage = doc["acV1"] | 0.0f;
        acCurrent = doc["acI1"] | 0.0f;
        acVoltage2 = doc["acV2"] | 0.0f;
        acCurrent2 = doc["acI2"] | 0.0f;
        dcVoltage = doc["dcV"] | 0.0f;
        dcCurrent = doc["dcI"] | 0.0f;
        dcPower = doc["dcW"] | 0.0f;
        envTemp = doc["tmp"] | 0.0f;
        envHum = doc["hum"] | 0.0f;
        envPres = doc["prs"] | 0.0f;

        nanoConnected = true;
        lastSerialRx = millis();
      }
      else
      {
        Serial.print("[ERROR] JSON Parse Failed: ");
        Serial.println(error.c_str());
      }
    }
  }

  // Connection timeout safety net (5 seconds)
  if (nanoConnected && (millis() - lastSerialRx > 5000))
  {
    Serial.println("[WARNING] Nano Serial Telemetry Timeout!");
    nanoConnected = false;
    acVoltage = 0.0;
    acCurrent = 0.0;
    acVoltage2 = 0.0;
    acCurrent2 = 0.0;
    dcVoltage = 0.0;
    dcCurrent = 0.0;
    dcPower = 0.0;
    envTemp = 0.0;
    envHum = 0.0;
    envPres = 0.0;
  }
}