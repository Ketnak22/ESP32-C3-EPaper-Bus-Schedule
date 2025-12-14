#include "ztm_api.h"
#include "secrets.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <algorithm> // for std::remove_if

// Helper to get current minutes from midnight
int getCurrentMinutes() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return -1;
    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

// Helper: Parse "HH:MM" to minutes
int parseTimeStr(String timeStr) {
    int h = timeStr.substring(0, 2).toInt();
    int m = timeStr.substring(3, 5).toInt();
    return h * 60 + m;
}

std::vector<ScheduleEntry> fetchScheduleFromServer(const String& busStopId, const String& busStopNr) {
    std::vector<ScheduleEntry> schedules;

    if (WiFi.status() != WL_CONNECTED) return schedules;

    int currentMins = getCurrentMinutes();
    if (currentMins == -1) return schedules;

    HTTPClient http;
    WiFiClient client;

    // Call your Python API
    String url = String(SERVER_URL) + "/get_stop_schedule" + 
                 "?busStopId=" + busStopId + 
                 "&busStopNr=" + busStopNr + 
                 "&currentTimeInMinutes=" + String(currentMins);

    http.setTimeout(5000); 
    http.begin(client, url);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject obj : arr) {
                ScheduleEntry entry;
                entry.line = obj["linia"].as<String>();
                entry.direction = obj["kierunek"].as<String>();
                // Python sends "16:41:00", we want "16:41"
                String fullTime = obj["czas"].as<String>();
                entry.time = fullTime.substring(0, 5); 
                schedules.push_back(entry);
                if(schedules.size() >= 15) break; 
            }
        }
    }
    http.end();
    return schedules;
}

void removePastSchedules(std::vector<ScheduleEntry> &schedules) {
    int currentMins = getCurrentMinutes();
    if (currentMins == -1) return;

    // Use erase-remove idiom to filter vector in place
    schedules.erase(std::remove_if(schedules.begin(), schedules.end(), [currentMins](const ScheduleEntry &entry) {
        int busMins = parseTimeStr(entry.time);
        int diff = busMins - currentMins;
        
        if (diff > 1000) return false; // Future (Midnight wrap)
        if (diff < -1000) return false; // Future (Midnight wrap)

        // Remove if passed more than 1 minute ago
        return diff < -1; 
    }), schedules.end());
}

std::vector<ScheduleEntry> convertScheduleTimesToMinutesLeft(const std::vector<ScheduleEntry> &schedules) {
    std::vector<ScheduleEntry> updatedSchedules;
    int currentMins = getCurrentMinutes();
    if (currentMins == -1) return schedules;

    for (const auto &entry : schedules) {
        ScheduleEntry newEntry = entry;
        int busMins = parseTimeStr(entry.time);
        int diff = busMins - currentMins;
      
        
        if (diff > 1000) { 
             // Special case: Bus 24:10 (1450), Current 00:05 (5). Diff = 1445.
             // Real diff is 5 minutes.
             diff = diff - 1440; 
        }

        if (diff > 1) {
             newEntry.time = String(diff) + " min";
             updatedSchedules.push_back(newEntry);
        } else if (diff >= 0) {
             newEntry.time = (diff == 0) ? "Teraz" : "<1 min";
             updatedSchedules.push_back(newEntry);
        }
    }
    return updatedSchedules;
}