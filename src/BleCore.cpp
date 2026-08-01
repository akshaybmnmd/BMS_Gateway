#include "BleCore.h"
#include <NimBLEDevice.h>

BmsData bms1Data = {1, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, false, false, {0}, 0, 1};
BmsData bms2Data = {2, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, false, false, {0}, 0, 1};
BmsData *activeBms = nullptr;

NimBLEClient *pClient = nullptr;
NimBLERemoteCharacteristic *pActiveWriteChar = nullptr;
const uint8_t basicInfoCmd[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};

void clearBmsBuffer(BmsData *bms);
void resetBmsState(BmsData *bms);

static void notifyCB(NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify)
{
  if (activeBms == nullptr || length == 0)
    return;

  for (size_t i = 0; i < length; i++)
  {
    if (activeBms->bufferIdx < BMS_BUFFER_SIZE)
      activeBms->buffer[activeBms->bufferIdx++] = pData[i];
    else
    {
      // Buffer full; drop remaining bytes to avoid overflow
      break;
    }
  }

  // Ensure we can safely inspect the last byte of the incoming packet
  if (length > 0 && pData[length - 1] == 0x77)
  {
    // Basic header/length validation: need at least 24 bytes for baseline parsing
    if (activeBms->bufferIdx > 23 && activeBms->buffer[0] == 0xDD && activeBms->buffer[1] == 0x03)
    {
      // Parse only when we have the expected minimum bytes
      activeBms->voltage = (((uint16_t)activeBms->buffer[4] << 8) | activeBms->buffer[5]) * 0.01f;
      int16_t rawCurrent = (((int16_t)activeBms->buffer[6] << 8) | activeBms->buffer[7]);
      activeBms->current = rawCurrent * 0.01f;
      activeBms->soc = activeBms->buffer[23];
      activeBms->power = activeBms->voltage * activeBms->current;

      // NTC count may be untrusted; compute max possible based on buffer size
      uint8_t ntcCount = activeBms->buffer[26];
      uint8_t maxNtc = 0;
      if (activeBms->bufferIdx > 27)
      {
        maxNtc = (activeBms->bufferIdx - 27) / 2;
      }
      if (ntcCount > maxNtc)
      {
        // Cap ntcCount to avoid out-of-bounds access
        ntcCount = maxNtc;
      }

      float highestTemp = -100.0f;

      if (ntcCount > 0)
      {
        for (uint8_t i = 0; i < ntcCount; i++)
        {
          size_t hiIdx = 27 + (i * 2);
          size_t loIdx = hiIdx + 1;
          if (loIdx < activeBms->bufferIdx)
          {
            uint16_t rawTemp = (((uint16_t)activeBms->buffer[hiIdx] << 8) | activeBms->buffer[loIdx]);
            float celsius = (rawTemp / 10.0f) - 273.15f;
            if (celsius > highestTemp)
              highestTemp = celsius;
          }
        }
      }
      activeBms->maxTemp = (highestTemp > -50.0f) ? highestTemp : 0.0f;

      activeBms->isConnected = true;
      activeBms->dataReady = true;
      activeBms->lastUpdateTime = millis();
    }
    else
    {
      Serial.printf("[DEBUG %lu] Payload failed header/length validation.\n", millis());
    }

    clearBmsBuffer(activeBms);
  }
}

void setupBLE()
{
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pClient = NimBLEDevice::createClient();
}

void disconnectBLE()
{
  if (pClient != nullptr)
  {
    pClient->disconnect();
  }
}

bool connectAndSubscribe(const std::string &macAddress)
{
  resetBmsState(activeBms);
  pActiveWriteChar = nullptr;

  NimBLEAddress address(macAddress, BLE_ADDR_PUBLIC);

  if (!pClient->connect(address))
  {
    Serial.printf("[ERROR %lu] pClient->connect() failed entirely.\n", millis());
    activeBms->isConnected = false;
    return false;
  }

  NimBLERemoteService *pService = pClient->getService(BMS_SERVICE_UUID);

  if (pService != nullptr)
  {
    NimBLERemoteCharacteristic *pNotifyChar = pService->getCharacteristic(BMS_CHAR_NOTIFY_UUID);
    pActiveWriteChar = pService->getCharacteristic(BMS_CHAR_WRITE_UUID);

    if (pNotifyChar != nullptr && pNotifyChar->canNotify())
    {
      pNotifyChar->subscribe(true, notifyCB);
      return true;
    }
    else
    {
      Serial.printf("[ERROR %lu] Notify Characteristic %s missing or cannot notify.\n", millis(), BMS_CHAR_NOTIFY_UUID);
    }
  }
  else
  {
    Serial.printf("[ERROR %lu] Service %s not found on this device.\n", millis(), BMS_SERVICE_UUID);
  }

  activeBms->isConnected = false;
  Serial.printf("[DEBUG %lu] Disconnecting due to service/char failure.\n", millis());
  disconnectBLE();
  return false;
}

bool triggerBmsRead()
{
  if (pActiveWriteChar != nullptr && (pActiveWriteChar->canWrite() || pActiveWriteChar->canWriteNoResponse()))
  {
    pActiveWriteChar->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);
    return true;
  }
  Serial.printf("[ERROR %lu] Cannot write to characteristic %s!\n", millis(), BMS_CHAR_WRITE_UUID);
  return false;
}

void clearBmsBuffer(BmsData *bms)
{
  if (bms == nullptr)
    return;
  bms->bufferIdx = 0;
  memset(bms->buffer, 0, sizeof(bms->buffer));
}

void resetBmsState(BmsData *bms)
{
  if (bms == nullptr)
    return;
  bms->dataReady = false;
  clearBmsBuffer(bms);
}