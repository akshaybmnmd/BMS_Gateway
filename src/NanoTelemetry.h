#ifndef NANO_TELEMETRY_H
#define NANO_TELEMETRY_H

#include <Arduino.h>

struct SensorTelemetry {
    float acVoltage1;
    float acCurrent1;
    float acVoltage2;
    float acCurrent2;
    float dcVoltage;
    float dcCurrent;
    float dcPower;
    float temperature;
    float humidity;
    float pressure;
    bool nano_connected;
};

extern SensorTelemetry sensorData;

void setupNanoTelemetry();
void processNanoTelemetry(); 

#endif