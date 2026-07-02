#include "AcSensorCore.h"
#include <Wire.h>

Adafruit_ADS1115 ads;
float acVoltage = 0.0;
float acCurrent = 0.0;
bool adsConnected = false;

unsigned long lastAdsRetry = 0;

float calibrationFactorV = 1.0;
float calibrationFactorI = 1.0;

bool pingI2C(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

void setupAcSensors() {
  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  if (!ads.begin()) {
    Serial.println("[WARNING] ADS1115 not found at boot! AC Sensing disabled.");
    adsConnected = false;
  } else {
    Serial.println("[INFO] ADS1115 Initialized successfully.");
    adsConnected = true;
  }
}

void readAcSensors() {

  if (!adsConnected) {
    if (millis() - lastAdsRetry > 10000) {
      lastAdsRetry = millis();
      if (ads.begin()) {
        Serial.println("[INFO] ADS1115 Reconnected Successfully!");
        adsConnected = true;
      } else {
        return;
      }
    } else {
      return;
    }
  }

  if (!pingI2C(0x48)) {
    Serial.println("[ERROR] ADS1115 connection lost during runtime!");
    adsConnected = false;
    acVoltage = 0.0;
    acCurrent = 0.0;
    return;
  }

  long sumSqV = 0;
  long sumSqI = 0;
  int count = 0;

  unsigned long startMillis = millis();

  while (millis() - startMillis < 40) {
    int16_t adc0 = ads.readADC_SingleEnded(0);
    int16_t adc1 = ads.readADC_SingleEnded(1);

    float v = (adc0 * 0.125 / 1000.0) - 1.65;
    float i = (adc1 * 0.125 / 1000.0) - 1.65;

    sumSqV += (v * v);
    sumSqI += (i * i);
    count++;
  }

  if (count > 0) {
    acVoltage = sqrt((float)sumSqV / count) * calibrationFactorV;
    acCurrent = sqrt((float)sumSqI / count) * calibrationFactorI;
  }
}