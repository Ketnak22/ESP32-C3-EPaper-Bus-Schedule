import requests

from os import getenv
from dotenv import load_dotenv

load_dotenv()
ZTM_API_KEY = getenv("ZTM_API_KEY")
WIDOCZNA_BUS_STOP_ID = "2329"
MARSZALKOWSKA_BUS_STOP_ID = "7009"

DEFAULT_HEADERS = {"Cache-Control": "no-cache"}

api_url = "https://api.um.warszawa.pl/api/action/dbtimetable_get/"

# Get available lines for a bus stop
def get_lines(bus_stop_id, bus_stop_nr):
    resp = requests.get(api_url, 
        params={
            "id": "88cd555f-6f31-43ca-9de4-66c479ad5942",
            "busstopId": bus_stop_id,
            "busstopNr": bus_stop_nr,
            "apikey": ZTM_API_KEY
        }, 
        headers=DEFAULT_HEADERS
    )
    
    return [item['values'][0]['value'] for item in resp.json()['result']]    

# Get bus stop schedule for a specific line
def get_stop_schedule(bus_stop_id, bus_stop_nr, line):
    resp = requests.get(api_url, params={
        "id": "e923fa0e-d96c-43f9-ae6e-60518c9f3238",
        "busstopId": bus_stop_id,
        "busstopNr": bus_stop_nr,
        "line": line,
        "apikey": ZTM_API_KEY
        }, 
        headers=DEFAULT_HEADERS
    )
    
    formatted_resp = []
    for item in resp.json()['result']:
        temp = {}
        for obj in item:
            if obj['key'] == 'kierunek':
                temp['kierunek'] = obj['value']
            if obj['key'] == 'czas':
                temp['czas'] = obj['value']
        if temp:
            temp['linia'] = line
            formatted_resp.append(temp)

    return formatted_resp

# Get bus stop schedule for all lines
def get_stop_schedule_all_lines(bus_stop_id, bus_stop_nr):
    lines = get_lines(bus_stop_id, bus_stop_nr)
    return [
        schedule 
        for line in lines 
        for schedule in get_stop_schedule(bus_stop_id, bus_stop_nr, line)
    ]