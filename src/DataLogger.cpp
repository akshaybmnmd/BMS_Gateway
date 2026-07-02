#include "DataLogger.h"
#include <LittleFS.h>

const char *LOG_FILE = "/metrics.csv";
const int MAX_FILE_SIZE = 1024 * 500; // 500 KB limit (prevents total flash fill)

void setupStorage()
{
    // true = Format the partition if it fails to mount (first boot)
    if (!LittleFS.begin(true))
    {
        Serial.println("[FATAL] LittleFS Mount Failed!");
        return;
    }
    Serial.println("[INFO] LittleFS Storage Mounted Successfully.");
}

void logMetricsToFlash(const SystemMetrics &metrics)
{
    // 1. Check file size to prevent overflow (Log Rotation)
    File file = LittleFS.open(LOG_FILE, "r");
    if (file && file.size() > MAX_FILE_SIZE)
    {
        file.close();
        Serial.println("[INFO] Log file full. Deleting old logs...");
        LittleFS.remove(LOG_FILE); // Simple rotation: nuke and start fresh
    }
    else if (file)
    {
        file.close();
    }

    // 2. Open in Append Mode
    file = LittleFS.open(LOG_FILE, "a");
    if (!file)
    {
        Serial.println("[ERROR] Failed to open log file for appending");
        return;
    }

    // 3. Write Data as CSV: Uptime, AC Volt, AC Power, DC Net Power, Avg SoC
    // Note: Since node 1 lacks Wi-Fi, it doesn't know real-world time.
    // We use millis() uptime. The Wi-Fi node will append real timestamps later.
    file.printf("%lu,%.1f,%.0f,%.0f,%d\n",
                millis() / 1000,
                metrics.acVoltage,
                metrics.acPower,
                metrics.netPower,
                metrics.avgSoc);

    file.close();
    Serial.println("[INFO] Metrics successfully written to Flash.");
}

void printStoredLogs()
{
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return;
    }
    Serial.println("--- START OF SAVED LOGS ---");
    while (file.available())
    {
        Serial.write(file.read());
    }
    Serial.println("--- END OF SAVED LOGS ---");
    file.close();
}