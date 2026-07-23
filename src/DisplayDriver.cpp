#include "DisplayDriver.h"
#include "BleCore.h"
#include "AcSensorCore.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);

const char *shortStatus(SystemStatus s)
{
  if (s == STATUS_CHARGING)
    return "CHG";
  if (s == STATUS_DISCHARGING)
    return "DIS";
  if (s == STATUS_IDLE)
    return "IDL";
  return "ERR";
}

const char *graceStatusToString(GracePeriodStatus g)
{
  if (g == GRACE_NONE)
    return "NONE-LIVE";
  if (g == GRACE_ACTIVE)
    return "ACTIVE!";
  return "EXPIRED";
}

void setupDisplay()
{
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setContrast(150);
}

void drawSplashScreen()
{
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_profont15_tf);
  u8g2.drawStr(25, 25, "BMS GATEWAY");

  u8g2.setFont(u8g2_font_profont11_tf);
  u8g2.drawStr(10, 45, "Starting System...");
  u8g2.drawStr(10, 60, "Initializing BLE...");

  u8g2.sendBuffer();
}

void drawOverviewScreen(const SystemMetrics &metrics)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "SYS: %s | AC: %.0fW", shortStatus(metrics.status), metrics.acPower);
  u8g2.drawStr(0, 10, buf);

  snprintf(buf, sizeof(buf), "B1: %.1fV %5.1fA %d%%", bms1Data.voltage, bms1Data.current, bms1Data.soc);
  u8g2.drawStr(0, 23, buf);

  snprintf(buf, sizeof(buf), "B2: %.1fV %5.1fA %d%%", bms2Data.voltage, bms2Data.current, bms2Data.soc);
  u8g2.drawStr(0, 36, buf);

  snprintf(buf, sizeof(buf), "NET: %.1fA (%.0fW)", metrics.netCurrent, metrics.netPower);
  u8g2.drawStr(0, 49, buf);

  snprintf(buf, sizeof(buf), "Imb: %d%% | MaxT: %.1fC", metrics.socDelta, metrics.peakTemp);
  u8g2.drawStr(0, 62, buf);
}

void drawACDetailsScreen(const SystemMetrics &metrics)
{
  char buf[32];
  u8g2.drawStr(0, 10, "-- AC GRID DIAG --");

  snprintf(buf, sizeof(buf), "Grid V: %.1f V", metrics.acVoltage);
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "Load I: %.2f A", metrics.acCurrent);
  u8g2.drawStr(0, 38, buf);

  snprintf(buf, sizeof(buf), "Power : %.0f W", metrics.acPower);
  u8g2.drawStr(0, 51, buf);
}

void drawHardwareFlagsScreen(const SystemMetrics &metrics)
{
  char buf[32];
  u8g2.drawStr(0, 10, "- HARDWARE FLAGS -");

  snprintf(buf, sizeof(buf), "BMS1 BLE: %s", bms1Data.isConnected ? "CONN" : "DROP");
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "BMS2 BLE: %s", bms2Data.isConnected ? "CONN" : "DROP");
  u8g2.drawStr(0, 38, buf);

  // CHANGED: Reflects Nano Serial connection instead of I2C ADC
  snprintf(buf, sizeof(buf), "Nano RX : %s", metrics.nano_connected ? "OK" : "ERR");
  u8g2.drawStr(0, 51, buf);

  snprintf(buf, sizeof(buf), "Relay   : %s", metrics.status == STATUS_ERROR ? "OPEN" : "CLOSED");
  u8g2.drawStr(0, 64, buf);
}

void drawSystemTimersScreen(const SystemMetrics &metrics)
{
  char buf[32];
  unsigned long upSec = millis() / 1000;

  unsigned long bmsAge = (millis() - bms1Data.lastUpdateTime) / 1000;

  u8g2.drawStr(0, 10, "- SYSTEM TIMERS -");

  snprintf(buf, sizeof(buf), "Uptime: %luh %lum %lus", (upSec / 3600), (upSec % 3600) / 60, upSec % 60);
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "BMS Data Age: %lus", bmsAge);
  u8g2.drawStr(0, 38, buf);

  snprintf(buf, sizeof(buf), "Mem Free: %u KB", ESP.getFreeHeap() / 1024);
  u8g2.drawStr(0, 51, buf);

  snprintf(buf, sizeof(buf), "Grace Tmr: %s", graceStatusToString(metrics.graceStatus));
  u8g2.drawStr(0, 64, buf);
}

void drawEnvironmentScreen(const SystemMetrics &metrics)
{
  char buf[32];
  u8g2.drawStr(0, 10, "- ENVIRONMENT DATA -");

  snprintf(buf, sizeof(buf), "Temp : %.1f C", metrics.envTemp);
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "Humid: %.1f %%", metrics.envHum);
  u8g2.drawStr(0, 38, buf);

  snprintf(buf, sizeof(buf), "Press: %.1f hPa", metrics.envPres);
  u8g2.drawStr(0, 51, buf);
}

void updateDisplay(const SystemMetrics &metrics, int currentView)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont11_tf);

  switch (currentView)
  {
  case 0:
    drawOverviewScreen(metrics);
    break;
  case 1:
    drawACDetailsScreen(metrics);
    break;
  case 2:
    drawHardwareFlagsScreen(metrics);
    break;
  case 3:
    drawSystemTimersScreen(metrics);
    break;
  case 4:
    drawEnvironmentScreen(metrics);
    break;
  }

  u8g2.sendBuffer();
}