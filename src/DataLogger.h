#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include "Config.h"

void setupStorage();
void logMetricsToFlash(const SystemMetrics &metrics);
void printStoredLogs(); // For debugging

#endif