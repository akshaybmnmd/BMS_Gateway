#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <U8g2lib.h>
#include "Config.h"

void setupDisplay();
void drawSplashScreen();
void updateDisplay(const SystemMetrics& metrics, int currentView); 

#endif