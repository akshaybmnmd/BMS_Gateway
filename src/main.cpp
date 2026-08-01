#include <Arduino.h>
#include "Config.h"
#include "BleCore.h"
// #include "DataLogger.h"
#include "NanoTelemetry.h"
#include "DisplayDriver.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <cctype>

TaskHandle_t NanoTelemetryHandle = NULL;

enum AppState
{
  STATE_WAIT_INTERVAL,
  STATE_CONNECT_BMS1,
  STATE_DELAY_BMS1,
  STATE_WAIT_BMS1_DATA,
  STATE_COOLDOWN,
  STATE_CONNECT_BMS2,
  STATE_DELAY_BMS2,
  STATE_WAIT_BMS2_DATA,
  STATE_UPDATE_SYSTEM_CONTROL
};

SystemMetrics sysMetrics;
SemaphoreHandle_t metricsMutex = NULL;

AppState currentState = STATE_CONNECT_BMS1;
unsigned long stateTimer = 0;

unsigned long lastLogTime = 0;

const int BUTTON_PIN = 15;
const int CONTACTOR_PIN = 32;
const int FAN_PIN = 13;
const int FAN_PWM_CHANNEL = 0;
const int FAN_PWM_FREQ = 30;
const int FAN_PWM_RES = 8;
const int TURN_ON_SOC = 60;
const int TURN_OFF_SOC = 40;
const unsigned long BOOT_DELAY_MS = 15000;

const int WIFI_RX_PIN = 4;
const int WIFI_TX_PIN = 5;
const int WIFI_BAUD = 115200;

bool manualOverride = false;
bool overrideState = false;

bool currentRelayState = false;
unsigned long lastRelaySwitchTime = 0;
const unsigned long RELAY_COOLDOWN_MS = 5000;

int currentView = 0;

void updateSystemControl();
void backgroundTask(void *parameter);
void updateRelayState(bool desiredState);

void processSerialCommands()
{
  char cmdBuf[64];
  size_t len = 0;

  if (Serial.available())
  {
    len = Serial.readBytesUntil('\n', cmdBuf, sizeof(cmdBuf) - 1);
  }
  else if (Serial1.available())
  {
    len = Serial1.readBytesUntil('\n', cmdBuf, sizeof(cmdBuf) - 1);
  }

  if (len == 0)
    return;

  cmdBuf[len] = '\0';

  // Trim leading/trailing whitespace
  char *start = cmdBuf;
  while (*start && isspace((unsigned char)*start))
    start++;
  char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)*(end - 1)))
    *(--end) = '\0';

  // Lowercase in-place
  for (char *p = start; *p; ++p)
    *p = (char)tolower((unsigned char)*p);

  if (strcmp(start, "on") == 0)
  {
    manualOverride = true;
    overrideState = true;
    Serial.println("\n[OVERRIDE] Manual Mode ACTIVE: Relay forced ON via command");
    updateRelayState(true);
  }
  else if (strcmp(start, "off") == 0)
  {
    manualOverride = true;
    overrideState = false;
    Serial.println("\n[OVERRIDE] Manual Mode ACTIVE: Relay forced OFF via command");
    updateRelayState(false);
  }
  else if (strcmp(start, "auto") == 0)
  {
    manualOverride = false;
    Serial.println("\n[OVERRIDE] Manual Mode DISABLED: Returning to Auto Battery Logic");
  }
  else
  {
    Serial.printf("\n[ERROR] Unknown command received: '%s'. Valid commands: 'on', 'off', 'auto'.\n", start);
  }
}

void sendTelemetryToWiFi(const SystemMetrics &metrics)
{
  StaticJsonDocument<512> doc;

  doc["acV1"] = metrics.acVoltage;
  doc["acI1"] = metrics.acCurrent;
  doc["acW1"] = metrics.acPower;
  doc["acV2"] = metrics.acVoltage2;
  doc["acI2"] = metrics.acCurrent2;
  doc["acW2"] = metrics.acPower2;

  doc["dcV"] = metrics.dcVoltage;
  doc["dcI"] = metrics.dcCurrent;
  doc["dcW"] = metrics.dcPower;

  doc["bms1V"] = bms1Data.voltage;
  doc["bms1I"] = bms1Data.current;
  doc["bms1Soc"] = bms1Data.soc;
  doc["bms1Tmp"] = bms1Data.maxTemp;
  doc["bms2V"] = bms2Data.voltage;
  doc["bms2I"] = bms2Data.current;
  doc["bms2Soc"] = bms2Data.soc;
  doc["bms2Tmp"] = bms2Data.maxTemp;

  doc["netI"] = metrics.netCurrent;
  doc["netW"] = metrics.netPower;
  doc["avgSoc"] = metrics.avgSoc;
  doc["relay"] = metrics.relayClosed;
  doc["envTmp"] = metrics.envTemp;
  doc["envHum"] = metrics.envHum;
  doc["envPrs"] = metrics.envPres;
  doc["sysSts"] = (int)metrics.status;
  doc["fanSpd"] = (int)metrics.fan_speed;

  serializeJson(doc, Serial1);
  Serial1.println();
}

void setup()
{
  Serial.begin(115200);
  Serial1.begin(WIFI_BAUD, SERIAL_8N1, WIFI_RX_PIN, WIFI_TX_PIN);
  // Reduce Serial timeouts to avoid blocking readBytesUntil() for long periods
  Serial.setTimeout(10);
  Serial1.setTimeout(10);
  pinMode(CONTACTOR_PIN, OUTPUT);
  digitalWrite(CONTACTOR_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(FAN_PIN, FAN_PWM_CHANNEL);

  metricsMutex = xSemaphoreCreateMutex();
  if (metricsMutex == NULL)
  {
    Serial.println("[FATAL] Failed to create Mutex!");
    while (1)
      ;
  }

  setupDisplay();
  drawSplashScreen();
  setupNanoTelemetry();
  setupBLE();

  xTaskCreatePinnedToCore(backgroundTask, "BackgroundCore0", 8192, NULL, 1, &NanoTelemetryHandle, 0);

  Serial.println("\n--- System Setup Complete. Waiting for initial interval... ---");
}

void loop()
{
  switch (currentState)
  {
  case STATE_WAIT_INTERVAL:
    if (millis() - stateTimer >= READ_INTERVAL_MS)
    {
      currentState = STATE_CONNECT_BMS1;
    }
    break;

  case STATE_CONNECT_BMS1:
    activeBms = &bms1Data;
    if (connectAndSubscribe(BMS1_MAC))
    {
      stateTimer = millis();
      currentState = STATE_DELAY_BMS1;
    }
    else
    {
      Serial.printf("[DEBUG %lu] BMS 1 connect failed. Jumping to Cooldown.\n", millis());
      stateTimer = millis();
      currentState = STATE_COOLDOWN;
    }
    break;

  case STATE_DELAY_BMS1:
    if (millis() - stateTimer >= 500)
    {
      if (triggerBmsRead())
      {
        stateTimer = millis();
        currentState = STATE_WAIT_BMS1_DATA;
      }
      else
      {
        disconnectBLE();
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      }
    }
    break;

  case STATE_WAIT_BMS1_DATA:
    if (activeBms->dataReady)
    {
      disconnectBLE();
      stateTimer = millis();
      currentState = STATE_COOLDOWN;
    }
    else if (millis() - stateTimer >= TIMEOUT_MS)
    {
      Serial.printf("[DEBUG %lu] BMS 1 Request Timed Out!\n", millis());
      activeBms->isConnected = false;
      disconnectBLE();
      stateTimer = millis();
      currentState = STATE_COOLDOWN;
    }
    break;

  case STATE_COOLDOWN:
    if (millis() - stateTimer >= COOLDOWN_MS)
    {
      currentState = STATE_CONNECT_BMS2;
    }
    break;

  case STATE_CONNECT_BMS2:
    activeBms = &bms2Data;
    if (connectAndSubscribe(BMS2_MAC))
    {
      stateTimer = millis();
      currentState = STATE_DELAY_BMS2;
    }
    else
    {
      Serial.printf("[DEBUG %lu] BMS 2 connect failed. Jumping to Process Logic.\n", millis());
      currentState = STATE_UPDATE_SYSTEM_CONTROL;
    }
    break;

  case STATE_DELAY_BMS2:
    if (millis() - stateTimer >= 500)
    {
      if (triggerBmsRead())
      {
        stateTimer = millis();
        currentState = STATE_WAIT_BMS2_DATA;
      }
      else
      {
        disconnectBLE();
        currentState = STATE_UPDATE_SYSTEM_CONTROL;
      }
    }
    break;

  case STATE_WAIT_BMS2_DATA:
    if (activeBms->dataReady)
    {
      disconnectBLE();
      currentState = STATE_UPDATE_SYSTEM_CONTROL;
    }
    else if (millis() - stateTimer >= TIMEOUT_MS)
    {
      Serial.printf("[DEBUG %lu] BMS 2 Request Timed Out!\n", millis());
      activeBms->isConnected = false;
      disconnectBLE();
      currentState = STATE_UPDATE_SYSTEM_CONTROL;
    }
    break;

  case STATE_UPDATE_SYSTEM_CONTROL:
    updateSystemControl();
    activeBms = nullptr;
    stateTimer = millis();
    currentState = STATE_WAIT_INTERVAL;
    break;
  }
}

void updateRelayState(bool autoDesiredState)
{
  bool finalDesiredState = manualOverride ? overrideState : autoDesiredState;

  if (finalDesiredState == currentRelayState)
    return;

  if (finalDesiredState == true && millis() < BOOT_DELAY_MS)
  {
    return;
  }

  if (millis() - lastRelaySwitchTime >= RELAY_COOLDOWN_MS)
  {
    digitalWrite(CONTACTOR_PIN, finalDesiredState ? HIGH : LOW);
    currentRelayState = finalDesiredState;
    lastRelaySwitchTime = millis();

    if (manualOverride)
    {
      Serial.printf("\n[OVERRIDE] Contactor physically forced to: %s\n", finalDesiredState ? "CLOSED" : "OPEN");
    }
    else
    {
      Serial.printf("\n[INFO] Auto-Logic Contactor switched to: %s\n", finalDesiredState ? "CLOSED" : "OPEN");
    }
  }
}

void updateSystemControl()
{
  if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    unsigned long currentMillis = millis();

    bool bms1Valid = (bms1Data.lastUpdateTime > 0) && (currentMillis - bms1Data.lastUpdateTime < STALE_TIMEOUT_MS);
    bool bms2Valid = (bms2Data.lastUpdateTime > 0) && (currentMillis - bms2Data.lastUpdateTime < STALE_TIMEOUT_MS);

    bool bms1Live = bms1Valid && bms1Data.isConnected;
    bool bms2Live = bms2Valid && bms2Data.isConnected;

    if (bms1Live && bms2Live)
    {
      sysMetrics.graceStatus = GRACE_NONE;
    }
    else if (bms1Valid && bms2Valid)
    {
      sysMetrics.graceStatus = GRACE_ACTIVE;
    }
    else
    {
      sysMetrics.graceStatus = GRACE_EXPIRED;
    }

    if (sysMetrics.graceStatus != GRACE_EXPIRED)
    {
      if (sysMetrics.graceStatus == GRACE_ACTIVE)
      {
        Serial.println("\n[WARNING] BLE connection lost. Operating in GRACE_ACTIVE state.");
      }

      sysMetrics.avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
      sysMetrics.socDelta = std::abs(bms1Data.soc - bms2Data.soc);
      sysMetrics.minVoltage = (bms1Data.voltage < bms2Data.voltage) ? bms1Data.voltage : bms2Data.voltage;
      sysMetrics.voltageDelta = std::abs(bms1Data.voltage - bms2Data.voltage);
      sysMetrics.peakTemp = (bms1Data.maxTemp > bms2Data.maxTemp) ? bms1Data.maxTemp : bms2Data.maxTemp;
      sysMetrics.netCurrent = bms1Data.current + bms2Data.current;
      sysMetrics.currentDelta = std::abs(bms1Data.current - bms2Data.current);
      sysMetrics.netPower = bms1Data.power + bms2Data.power;
      sysMetrics.powerDelta = std::abs(bms1Data.power - bms2Data.power);

      if (sysMetrics.netCurrent > 1.0)
        sysMetrics.status = STATUS_CHARGING;
      else if (sysMetrics.netCurrent < -1.0)
        sysMetrics.status = STATUS_DISCHARGING;
      else
        sysMetrics.status = STATUS_IDLE;
    }
    else
    {
      sysMetrics.status = STATUS_ERROR;
      Serial.println("\n[CRITICAL ERROR] BMS Data Timeout (5+ min). Defaulting to safe state.");
    }

    // Capture a thread-safe snapshot for the slow I/O operations
    SystemMetrics metricsForIO = sysMetrics;

    xSemaphoreGive(metricsMutex);

    // Send telemetry via UART to the Wi-Fi node using the snapshot
    sendTelemetryToWiFi(metricsForIO);

    // if (millis() - lastLogTime >= LOG_INTERVAL_MS)
    // {
    //   logMetricsToFlash(metricsForIO);
    //   lastLogTime = millis();
    // }
  }
  else
  {
    Serial.println("[WARN] Core 1 failed to acquire Mutex!");
  }
}

void backgroundTask(void *parameter)
{
  const unsigned long VIEW_INTERVAL = 3000;
  const unsigned long DEBOUNCE_DELAY = 50;
  unsigned long lastScreenUpdate = 0;
  const unsigned long SCREEN_REFRESH_MS = 1000;

  unsigned long lastViewChange = millis();
  unsigned long lastDebounceTime = 0;
  bool lastButtonState = HIGH;
  bool buttonProcessed = false;

  for (;;)
  {
    processSerialCommands();
    processNanoTelemetry();

    int fanSpeed = 0;

    if (sensorData.temperature >= FAN_FULL_TEMP)
    {
      fanSpeed = FAN_MAX_DUTY;
    }
    else if (sensorData.temperature >= FAN_START_TEMP)
    {
      float tempRange = FAN_FULL_TEMP - FAN_START_TEMP;
      float pwmRange = (float)(FAN_MAX_DUTY - FAN_MIN_DUTY);
      fanSpeed = (int)((float)FAN_MIN_DUTY + ((sensorData.temperature - FAN_START_TEMP) * (pwmRange / tempRange)));
    }
    else
    {
      fanSpeed = 0;
    }

    if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      sysMetrics.acVoltage = sensorData.acVoltage1;
      sysMetrics.acCurrent = sensorData.acCurrent1;
      sysMetrics.acPower = sensorData.acVoltage1 * sensorData.acCurrent1;
      sysMetrics.acVoltage2 = sensorData.acVoltage2;
      sysMetrics.acCurrent2 = sensorData.acCurrent2;
      sysMetrics.acPower2 = sensorData.acVoltage2 * sensorData.acCurrent2;
      sysMetrics.dcVoltage = sensorData.dcVoltage;
      sysMetrics.dcCurrent = sensorData.dcCurrent;
      sysMetrics.dcPower = sensorData.dcPower;
      sysMetrics.envTemp = sensorData.temperature;
      sysMetrics.envHum = sensorData.humidity;
      sysMetrics.envPres = sensorData.pressure;
      sysMetrics.nano_connected = sensorData.nano_connected;
      sysMetrics.fan_speed = fanSpeed;
      bool desiredRelayState = currentRelayState;
      bool needWarning = false;

      if (sysMetrics.graceStatus == GRACE_EXPIRED)
      {
        desiredRelayState = false;
      }
      else
      {
        if (sysMetrics.avgSoc > TURN_ON_SOC)
        {
          desiredRelayState = true;
        }
        else if (sysMetrics.avgSoc < TURN_OFF_SOC)
        {
          desiredRelayState = false;
          if (currentRelayState == true)
          {
            // note that we want to print a warning, but do it after releasing mutex
            needWarning = true;
          }
        }
      }

      // Store the desired state in metrics and release the mutex quickly
      sysMetrics.relayClosed = desiredRelayState;
      xSemaphoreGive(metricsMutex);

      // Perform slow I/O and hardware switching outside the mutex
      if (needWarning)
      {
        Serial.println("[WARNING] Battery low! Requesting contactor open.");
      }

      updateRelayState(desiredRelayState);
    }

    ledcWrite(FAN_PWM_CHANNEL, fanSpeed);

    bool advanceView = false;
    unsigned long currentMillis = millis();

    if (currentMillis - lastViewChange >= VIEW_INTERVAL)
    {
      advanceView = true;
    }

    bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastButtonState)
    {
      lastDebounceTime = currentMillis;
    }

    if ((currentMillis - lastDebounceTime) > DEBOUNCE_DELAY)
    {
      if (reading == LOW && !buttonProcessed)
      {
        advanceView = true;
        buttonProcessed = true;
      }
      else if (reading == HIGH)
      {
        buttonProcessed = false;
      }
    }
    lastButtonState = reading;

    if (advanceView)
    {
      currentView = (currentView + 1) % 5;
      lastViewChange = currentMillis;
    }

    if (advanceView || (currentMillis - lastScreenUpdate >= SCREEN_REFRESH_MS))
    {
      SystemMetrics localMetricsSnapshot;
      bool snapshotValid = false;

      if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
      {
        localMetricsSnapshot = sysMetrics;
        xSemaphoreGive(metricsMutex);
        snapshotValid = true;
      }

      if (snapshotValid)
      {
        updateDisplay(localMetricsSnapshot, currentView);
        lastScreenUpdate = currentMillis;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}