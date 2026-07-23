#include <Arduino.h>
#include "Config.h"
#include "BleCore.h"
#include "DataLogger.h"
#include "AcSensorCore.h"
#include "DisplayDriver.h"

TaskHandle_t DisplayTaskHandle = NULL;

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
  STATE_PROCESS_LOGIC
};

SystemMetrics sysMetrics;
SemaphoreHandle_t metricsMutex = NULL;

AppState currentState = STATE_CONNECT_BMS1;
unsigned long stateTimer = 0;
unsigned long lastAcRead = 0;

unsigned long lastLogTime = 0;

const int BUTTON_PIN = 15;
const int CONTACTOR_PIN = 32;
const int FAN_PIN = 13;
const int FAN_PWM_CHANNEL = 0;
const int FAN_PWM_FREQ = 25000; // 25 kHz pushes coil whine above human hearing
const int FAN_PWM_RES = 8;      // 8-bit resolution (0-255)

int currentView = 0;

void evaluateContactorLogic();
void displayWorker(void *parameter);

void setup()
{
  Serial.begin(115200);
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
  setupAcSensors();
  setupBLE();

  xTaskCreatePinnedToCore(
      displayWorker,
      "DisplayTask",
      4096,
      NULL,
      1,
      &DisplayTaskHandle,
      0);

  Serial.println("\n--- System Setup Complete. Waiting for initial interval... ---");
}

void loop()
{
  // Core 1 runs the BLE State Machine completely decoupled from display refreshes
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
      currentState = STATE_PROCESS_LOGIC;
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
        currentState = STATE_PROCESS_LOGIC;
      }
    }
    break;

  case STATE_WAIT_BMS2_DATA:
    if (activeBms->dataReady)
    {
      disconnectBLE();
      currentState = STATE_PROCESS_LOGIC;
    }
    else if (millis() - stateTimer >= TIMEOUT_MS)
    {
      Serial.printf("[DEBUG %lu] BMS 2 Request Timed Out!\n", millis());
      activeBms->isConnected = false;
      disconnectBLE();
      currentState = STATE_PROCESS_LOGIC;
    }
    break;

  case STATE_PROCESS_LOGIC:
    readAcSensors();
    evaluateContactorLogic();
    activeBms = nullptr;
    stateTimer = millis();
    currentState = STATE_WAIT_INTERVAL;
    break;
  }
}

void evaluateContactorLogic()
{
  if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    const unsigned long STALE_TIMEOUT_MS = 5 * 60 * 1000;
    unsigned long currentMillis = millis();

    // 1. Evaluate individual data validity
    bool bms1Valid = (bms1Data.lastUpdateTime > 0) && (currentMillis - bms1Data.lastUpdateTime < STALE_TIMEOUT_MS);
    bool bms2Valid = (bms2Data.lastUpdateTime > 0) && (currentMillis - bms2Data.lastUpdateTime < STALE_TIMEOUT_MS);

    bool bms1Live = bms1Valid && bms1Data.isConnected;
    bool bms2Live = bms2Valid && bms2Data.isConnected;

    // 2. Set explicit Grace Period Status
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

    // 3. Act on the Status
    if (sysMetrics.graceStatus != GRACE_EXPIRED)
    {
      if (sysMetrics.graceStatus == GRACE_ACTIVE)
      {
        Serial.println("\n[WARNING] BLE connection lost. Operating in GRACE_ACTIVE state.");
      }

      sysMetrics.avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
      sysMetrics.socDelta = abs(bms1Data.soc - bms2Data.soc);
      sysMetrics.minVoltage = (bms1Data.voltage < bms2Data.voltage) ? bms1Data.voltage : bms2Data.voltage;
      sysMetrics.voltageDelta = abs(bms1Data.voltage - bms2Data.voltage);
      sysMetrics.peakTemp = (bms1Data.maxTemp > bms2Data.maxTemp) ? bms1Data.maxTemp : bms2Data.maxTemp;
      sysMetrics.netCurrent = bms1Data.current + bms2Data.current;
      sysMetrics.currentDelta = abs(bms1Data.current - bms2Data.current);
      sysMetrics.netPower = bms1Data.power + bms2Data.power;
      sysMetrics.powerDelta = abs(bms1Data.power - bms2Data.power);
      sysMetrics.acVoltage = acTelemetry.acVoltage1;
      sysMetrics.acCurrent = acTelemetry.acCurrent1;
      sysMetrics.acPower = acTelemetry.acVoltage1 * acTelemetry.acCurrent1;
      sysMetrics.acVoltage2 = acTelemetry.acVoltage2;
      sysMetrics.acCurrent2 = acTelemetry.acCurrent2;
      sysMetrics.acPower2 = acTelemetry.acVoltage2 * acTelemetry.acCurrent2;
      sysMetrics.dcVoltage = acTelemetry.dcVoltage;
      sysMetrics.dcCurrent = acTelemetry.dcCurrent;
      sysMetrics.dcPower = acTelemetry.dcPower;
      sysMetrics.envTemp = acTelemetry.temperature;
      sysMetrics.envHum = acTelemetry.humidity;
      sysMetrics.envPres = acTelemetry.pressure;
      sysMetrics.nano_connected = acTelemetry.nano_connected;

      if (sysMetrics.netCurrent > 1.0)
        sysMetrics.status = STATUS_CHARGING;
      else if (sysMetrics.netCurrent < -1.0)
        sysMetrics.status = STATUS_DISCHARGING;
      else
        sysMetrics.status = STATUS_IDLE;

      SystemMetrics metricsForIO = sysMetrics;

      xSemaphoreGive(metricsMutex);

      // --- SILENT FAN CONTROL LOGIC ---
      int fanSpeed = 0;

      if (metricsForIO.envTemp >= 45.0)
      {
        fanSpeed = FAN_MAX_DUTY;
      }
      else if (metricsForIO.envTemp >= 25.0)
      {
        // Map 25.0C - 45.0C linearly to 80 - 255
        float tempRange = FAN_FULL_TEMP - FAN_START_TEMP;
        float pwmRange = (float)(FAN_MAX_DUTY - FAN_MIN_DUTY);
        fanSpeed = (int)((float)FAN_MIN_DUTY + ((metricsForIO.envTemp - FAN_START_TEMP) * (pwmRange / tempRange)));
      }
      else
      {
        fanSpeed = 0; // Off
      }

      ledcWrite(FAN_PWM_CHANNEL, fanSpeed);
      // -----------------------------

      if (millis() - lastLogTime >= LOG_INTERVAL_MS)
      {
        logMetricsToFlash(metricsForIO);
        lastLogTime = millis();
      }

      Serial.println("\n================ SYSTEM METRICS ================");
      Serial.printf("STATUS   : %s\n", statusToString(metricsForIO.status));
      Serial.println("------------------------------------------------");
      Serial.printf("BMS 1    : %.2fV | %6.2fA | %5.0fW | %3d%% | %.1fC\n", bms1Data.voltage, bms1Data.current, bms1Data.power, bms1Data.soc, bms1Data.maxTemp);
      Serial.printf("BMS 2    : %.2fV | %6.2fA | %5.0fW | %3d%% | %.1fC\n", bms2Data.voltage, bms2Data.current, bms2Data.power, bms2Data.soc, bms2Data.maxTemp);
      Serial.println("------------------------------------------------");
      Serial.printf("AC 1     : %.1fV | %.2fA | %.0fW\n", metricsForIO.acVoltage, metricsForIO.acCurrent, metricsForIO.acPower);
      Serial.printf("AC 2     : %.1fV | %.2fA | %.0fW\n", metricsForIO.acVoltage2, metricsForIO.acCurrent2, metricsForIO.acPower2);
      Serial.printf("PZEM DC  : %.1fV | %.2fA | %.1fW\n", metricsForIO.dcVoltage, metricsForIO.dcCurrent, metricsForIO.dcPower);
      Serial.printf("ENV      : Temp: %.1fC | Hum: %.1f%% | Pres: %.1fhPa\n", metricsForIO.envTemp, metricsForIO.envHum, metricsForIO.envPres);
      Serial.println("------------------------------------------------");
      Serial.printf("DELTAS   : Volt:%.3fV | Cur:%.2fA | Pwr:%.0fW\n", metricsForIO.voltageDelta, metricsForIO.currentDelta, metricsForIO.powerDelta);
      Serial.printf("DC TOTAL : Net: %6.2fA | %5.0fW\n", metricsForIO.netCurrent, metricsForIO.netPower);
      Serial.printf("AC SENSE : %.1fV | %.2fA | %.0f VA\n", metricsForIO.acVoltage, metricsForIO.acCurrent, metricsForIO.acPower);
      Serial.printf("HEALTH   : Avg SoC %d%% (Imb %d%%) | Peak Temp %.1fC\n", metricsForIO.avgSoc, metricsForIO.socDelta, metricsForIO.peakTemp);
      Serial.println("================================================\n");
    }
    else
    {
      digitalWrite(CONTACTOR_PIN, LOW);
      sysMetrics.status = STATUS_ERROR;
      xSemaphoreGive(metricsMutex);
      Serial.println("\n[CRITICAL ERROR] BMS Data Timeout (5+ min). Defaulting to safe state.");
    }
  }
  else
  {
    Serial.println("[WARN] Core 1 failed to acquire Mutex!");
  }
}

// Thread safe, low priority background UI monitor locked to Core 0
void displayWorker(void *parameter)
{
  const unsigned long VIEW_INTERVAL = 5000;
  const unsigned long DEBOUNCE_DELAY = 50;
  unsigned long lastScreenUpdate = 0;
  const unsigned long SCREEN_REFRESH_MS = 1000;

  unsigned long lastViewChange = millis();
  unsigned long lastDebounceTime = 0;
  bool lastButtonState = HIGH;
  bool buttonProcessed = false;

  for (;;)
  {
    bool advanceView = false;
    unsigned long currentMillis = millis();

    // 1. Timer-Based Auto Rotate
    if (currentMillis - lastViewChange >= VIEW_INTERVAL)
    {
      advanceView = true;
    }

    // 2. Physical Debounced Button Read
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

    // 3. Process View Layout Wrap-Around & Force Update flag
    bool forceUpdate = advanceView;
    if (advanceView)
    {
      currentView = (currentView + 1) % 5; // Max 5 views (0 through 4)
      lastViewChange = currentMillis;
    }

    // 4. Only draw to the screen once per second, OR if the button was just pressed
    if (forceUpdate || (currentMillis - lastScreenUpdate >= SCREEN_REFRESH_MS))
    {
      // Snapshot Strategy: Lock data, copy struct instantly, unlock immediately
      SystemMetrics localMetricsSnapshot;
      bool snapshotValid = false;

      if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
      {
        localMetricsSnapshot = sysMetrics;
        xSemaphoreGive(metricsMutex);
        snapshotValid = true;
      }

      // 5. Draw outside the critical section to prevent clogging Core 1
      if (snapshotValid)
      {
        updateDisplay(localMetricsSnapshot, currentView);
        lastScreenUpdate = currentMillis; // Reset the 1-second refresh timer
      }
    }

    // Block this task for 100ms. Gives CPU cycles completely back to Core 0's radio stacks.
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}