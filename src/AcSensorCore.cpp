#include "AcSensorCore.h"
#include <Wire.h>

Adafruit_ADS1115 ads;
float acVoltage = 0.0;
float acCurrent = 0.0;
bool adsConnected = false;

unsigned long lastAdsRetry = 0;

float calibrationFactorV = 730.5f;
float calibrationFactorI = 1.0;

bool pingI2C(uint8_t address)
{
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

void setupAcSensors()
{
  Wire.begin();
  Wire.setClock(400000);

  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  if (!ads.begin())
  {
    Serial.println("[WARNING] ADS1115 not found at boot! AC Sensing disabled.");
    adsConnected = false;
  }
  else
  {
    Serial.println("[INFO] ADS1115 Initialized successfully.");
    adsConnected = true;
  }
}

void readAcSensors()
{
  if (!adsConnected)
  {
    if (millis() - lastAdsRetry > 10000)
    {
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

  if (!pingI2C(0x48))
  {
    Serial.println("[ERROR] ADS1115 connection lost during runtime (Ping failed)!");
    adsConnected = false;
    acVoltage = 0.0;
    acCurrent = 0.0;
    return;
  }

  const int MAX_SAMPLES = 100; 
  int16_t vSamples[MAX_SAMPLES];
  
  long sumV_DC = 0;
  int countV = 0;

  unsigned long startV = millis();
  while (millis() - startV < 150 && countV < MAX_SAMPLES) {
    vSamples[countV] = ads.readADC_SingleEnded(0);
    sumV_DC += vSamples[countV];
    countV++;
  }

  float trueMidV = (float)sumV_DC / countV;

  double sumSqV = 0.0;
  for (int i = 0; i < countV; i++) {
    float v = (vSamples[i] - trueMidV) * 0.125 / 1000.0;
    sumSqV += (v * v);
  }

  float rawRmsV = sqrt(sumSqV / countV);
  float instantVoltage = rawRmsV * calibrationFactorV;

  if (acVoltage == 0.0) {
    acVoltage = instantVoltage; // Seed the filter on first boot
  } else {
    acVoltage = (acVoltage * 0.80) + (instantVoltage * 0.20);
  }
}