#ifndef AC_SENSOR_CORE_H
#define AC_SENSOR_CORE_H

#include <Arduino.h>

void setupAcSensors();
void readAcSensors(); 

extern float acVoltage;
extern float acCurrent;
extern float acVoltage2;
extern float acCurrent2;
extern float dcVoltage;
extern float dcCurrent;
extern float dcPower;
extern bool nanoConnected;
extern float envTemp;
extern float envHum;
extern float envPres;

#endif