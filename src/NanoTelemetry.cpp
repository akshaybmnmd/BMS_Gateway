#include "NanoTelemetry.h"
#include <ArduinoJson.h>
#include "Config.h"

SensorTelemetry sensorData = {};
unsigned long lastSerialRx = 0;
const int NANO_RX_PIN = 16;
const int NANO_TX_PIN = 17;
const int NANO_BAUD = 9600;

void clearNanoTelemetry();

void setupNanoTelemetry()
{
  pinMode(NANO_RST_PIN, OUTPUT);
  digitalWrite(NANO_RST_PIN, HIGH);

  Serial2.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

  lastSerialRx = millis();
}

void resetNano()
{
  Serial.println("[SYSTEM] Triggering hardware reset on Nano...");

  digitalWrite(NANO_RST_PIN, LOW);
  delay(50);
  digitalWrite(NANO_RST_PIN, HIGH);

  lastSerialRx = millis();
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
    }else
    {
      Serial.println("[ERROR] Invalid payload received from Nano.");
    }
  }

  if (sensorData.nano_connected && (millis() - lastSerialRx > SERIAL_TIMEOUT_MS))
  {
    Serial.println("[WARNING] Nano Serial Telemetry Timeout!");

    clearNanoTelemetry();

    resetNano();
  }
}

void clearNanoTelemetry()
{
  sensorData = {};
}