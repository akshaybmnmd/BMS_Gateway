#ifndef AC_SENSOR_CORE_H
#define AC_SENSOR_CORE_H

#include <Arduino.h>

struct AcTelemetry {
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

extern AcTelemetry acTelemetry;

void setupAcSensors();
void readAcSensors(); 

#endif