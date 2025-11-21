#include "ztm_api.h"
#include "secrets.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Helper function to perform HTTPS GET requests
String httpGETRequest(const String& url);

// Internal helper functions
std::vector<String> getLines(const String& busStopId, const String& busStopNr);
std::vector<ScheduleEntry> getStopSchedule(const String& busStopId, const String& busStopNr, const String& line);
std::vector<ScheduleEntry> getStopScheduleAllLines(const String& busStopId, const String& busStopNr);

String httpGETRequest(const String& url) {
    std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
    client->setInsecure();
    HTTPClient https;

    if (https.begin(*client, url)) {
        int httpCode = https.GET();
        if (httpCode > 0 && (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)) {
            return https.getString();
        }
        https.end();
    }
    return "";
}

std::vector<String> getLines(const String& busStopId, const String& busStopNr) {
    std::vector<String> lines;
    String url = "https://api.um.warszawa.pl/api/action/dbtimetable_get/?id=88cd555f-6f31-43ca-9de4-66c479ad5942&busstopId=" + busStopId + "&busstopNr=" + busStopNr + "&apikey=" + String(ZTM_API_KEY);
    
    String payload = httpGETRequest(url);
    if (payload.length() == 0) return lines;

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
        JsonArray result = doc["result"];
        for (JsonObject item : result) {
            lines.push_back(item["values"][0]["value"].as<String>());
        }
    }
    return lines;
}

std::vector<ScheduleEntry> getStopSchedule(const String& busStopId, const String& busStopNr, const String& line) {
    std::vector<ScheduleEntry> schedules;
    String url = "https://api.um.warszawa.pl/api/action/dbtimetable_get/?id=e923fa0e-d96c-43f9-ae6e-60518c9f3238&busstopId=" + busStopId + "&busstopNr=" + busStopNr + "&line=" + line + "&apikey=" + String(ZTM_API_KEY);

    String payload = httpGETRequest(url);
    if (payload.length() == 0) return schedules;

    StaticJsonDocument<256> filter;
    filter["result"][0][0]["key"] = true;
    filter["result"][0][0]["value"] = true;

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
        JsonArray result = doc["result"];
        for (JsonArray item : result) {
            ScheduleEntry entry;
            entry.line = line;
            for (JsonObject val : item) {
                const char* key = val["key"];
                if (strcmp(key, "kierunek") == 0) {
                    entry.direction = val["value"].as<String>();
                }
                if (strcmp(key, "czas") == 0) {
                    entry.time = val["value"].as<String>();
                }
            }
            schedules.push_back(entry);
        }
    }
    return schedules;
}

std::vector<ScheduleEntry> getStopScheduleAllLines(const String& busStopId, const String& busStopNr) {
    std::vector<String> lines = getLines(busStopId, busStopNr);
    std::vector<ScheduleEntry> allSchedules;
    for (const String& line : lines) {
        std::vector<ScheduleEntry> lineSchedules = getStopSchedule(busStopId, busStopNr, line);
        allSchedules.insert(allSchedules.end(), lineSchedules.begin(), lineSchedules.end());
    }
    return allSchedules;
}

std::vector<ScheduleEntry> getStopScheduleBusstopComplex(const String& busStopId, const std::vector<String>& busStopNrs) {
    std::vector<ScheduleEntry> allSchedules;
    for (const String& busStopNr : busStopNrs) {
        std::vector<ScheduleEntry> stopSchedules = getStopScheduleAllLines(busStopId, busStopNr);
        allSchedules.insert(allSchedules.end(), stopSchedules.begin(), stopSchedules.end());
    }
    return allSchedules;
}

std::vector<ScheduleEntry> getFutureSchedules(const std::vector<ScheduleEntry> &allSchedules, const int scheduleCount)
{
  std::vector<ScheduleEntry> futureSchedules;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    return futureSchedules;
  }

  int currentTimeInMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  // Collect all future departures
  for (const auto &entry : allSchedules)
  {
    int scheduleHour = entry.time.substring(0, 2).toInt();
    int scheduleMinute = entry.time.substring(3, 5).toInt();
    int scheduleTimeInMinutes = scheduleHour * 60 + scheduleMinute;

    if (scheduleTimeInMinutes < currentTimeInMinutes)
    {
      continue; // Skip past departures
    }
    futureSchedules.push_back(entry);
  }

  // Sort by time
  std::sort(futureSchedules.begin(), futureSchedules.end(), [](const ScheduleEntry &a, const ScheduleEntry &b)
            { return a.time < b.time; });

  // Resize the vector to keep only the closest upcoming buses
  if (futureSchedules.size() > scheduleCount) {
      futureSchedules.resize(scheduleCount);
  }

  return futureSchedules;
}

std::vector<ScheduleEntry> convertScheduleTimesToMinutesLeft(const std::vector<ScheduleEntry> &schedules)
{
  std::vector<ScheduleEntry> updatedSchedules;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    return updatedSchedules; // Return empty if time retrieval fails
  }

  // Calculate current time in minutes from midnight for easy comparison
  int currentTimeInMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int currentTimeInSeconds = currentTimeInMinutes * 60 + timeinfo.tm_sec;

  for (const auto &entry : schedules)
  {
    ScheduleEntry updatedEntry = entry;

    // Assuming entry.time is in "HH:MM" format
    int scheduleHour = entry.time.substring(0, 2).toInt();
    int scheduleMinute = entry.time.substring(3, 5).toInt();
    int scheduleTimeInMinutes = scheduleHour * 60 + scheduleMinute;
    
    int minutesLeft = scheduleTimeInMinutes - currentTimeInMinutes;
    if (minutesLeft > 1)
    {
      updatedEntry.time = String(minutesLeft) + " min";
      updatedSchedules.push_back(updatedEntry);
    } else if (minutesLeft == 1) {
      updatedEntry.time = "<1 min";
      updatedSchedules.push_back(updatedEntry);
    } else if (minutesLeft == 0) {
      updatedEntry.time = "Teraz";
      updatedSchedules.push_back(updatedEntry);
    } else {
      // Skip past departures
      continue;
    }
  }

  return updatedSchedules;
}