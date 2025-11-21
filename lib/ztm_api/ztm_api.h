#pragma once

#include <Arduino.h>
#include <vector>

struct ScheduleEntry {
    String line;
    String direction;
    String time;
};

// Gets the complete schedule for a bus stop complex (multiple bus stop numbers)
std::vector<ScheduleEntry> getStopScheduleBusstopComplex(const String& busStopId, const std::vector<String>& busStopNrs);

// Filters and returns only future schedules from the provided list
std::vector<ScheduleEntry> getFutureSchedules(const std::vector<ScheduleEntry> &allSchedules, const int scheduleCount);

// Converts schedule times to minutes left from current time
std::vector<ScheduleEntry> convertScheduleTimesToMinutesLeft(const std::vector<ScheduleEntry> &schedules);