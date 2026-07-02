#ifndef BLE_CORE_H
#define BLE_CORE_H

#include "Config.h"

extern BmsData bms1Data;
extern BmsData bms2Data;
extern BmsData* activeBms;

void setupBLE();
void disconnectBLE();
bool connectAndSubscribe(const std::string& macAddress);
bool triggerBmsRead();

#endif