from ztm_api import get_stop_schedule_all_lines

from fastapi import FastAPI
app = FastAPI()

def convert_time_to_int(time_str: str) -> int:
    hours, minutes, _ = map(int, time_str.split(":"))
    return hours * 60 + minutes

@app.get("/get_stop_schedule")
async def get_stop_schedule(busStopId: str, busStopNr: str, currentTimeInMinutes: int):
    schedules = get_stop_schedule_all_lines(busStopId, busStopNr)
    schedules = sorted(schedules, key=lambda x: (x['czas']))
    schedules = [s for s in schedules if convert_time_to_int(s['czas']) >= currentTimeInMinutes]
    return schedules