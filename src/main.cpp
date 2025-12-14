#include <Arduino.h>
#include <vector>
#include <array>
#include <WiFi.h>
#include <time.h>
#include <Encoder.h>
#include <ezButton.h>

#include "ztm_api.h"
#include "secrets.h"

#include <U8g2_for_Adafruit_GFX.h>

// Pin Declarations
static const uint8_t EPD_BUSY = 21;
static const uint8_t EPD_CS = 5;
static const uint8_t EPD_RST = 7;
static const uint8_t EPD_DC = 6;
static const uint8_t EPD_SCK = 8;
static const uint8_t EPD_MOSI = 10;
static const uint8_t RT_CLK = 4;
static const uint8_t RT_DW = 3;
static const uint8_t RT_SW = 2;

Encoder encoder(RT_CLK, RT_DW);
int prevEncoderPos = 0;

ezButton encoderBtn(RT_SW);

// Track button holding time
unsigned long buttonPressStart = 0;

struct BUS_STOP_INFO {
    String id;
    std::vector<String> nr;
    String name;
};

const std::array<BUS_STOP_INFO, 6> busStops = {{
    {"7009", {"01", "02"}, "Marszałkowska"},
    {"2097", {"01", "02", "03", "04"}, "Saska"},
    {"2329", {"01", "02"}, "Majdan Widoczna"},
    {"7006", {"01", "02", "03", "04", "05", "06", "07", "08", "12", "14"}, "Metro Politechnika"},
    {"2103", {"01", "02"}, "CH Promenada"},
    {"2008", {"01", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12", "13", "14", "15", "16", "17", "22", "23"}, "Wiatraczna"}
}};

uint8_t currentBusStop = 0;
uint8_t currentBusStopNrIndex = 0;

bool changeStopMode = false;
uint8_t changingBusStop = 0;
String changingBusStopName = "";

const int FULL_REFRESH_THRESHOLD = 60; 

const char *ntpServer = "pool.ntp.org";
const char *tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3";

#define ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2;

// Layout Constants
static const uint8_t X_MARGIN = 25;
static const uint8_t LINE_TEXTBOX_PADDING = 8;
static const uint8_t BUS_STOP_NAME_Y_OFFSET = 45;
static const uint8_t HEADER_ROW_HEIGHT = 34;
static const uint8_t Y_OFFSET = 50;

int schedulesY = 0;
int secondLineY = 0;

std::vector<ScheduleEntry> globalSchedules;
int refreshCounter = 0; 
const unsigned long REFRESH_INTERVAL_MS = 60000; 
unsigned long previousRefreshMillis = 0;

// Declarations
void connectToWifi(bool silent);
void syncTime(bool silent);
void drawBackground(bool isFullRefresh, bool isChangingBusStop);
void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules);
void displayTextOnCenterHorizontally(const String &text, const int y);
void displayTextOnCenter(const String &text);
void showStatusMessage(const String &text);
void refreshLogic();
void enterDeepSleep(); // Logic to turn off

void setup() {
  Serial.begin(115200);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200, true, 50, false);
  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  u8g2.setForegroundColor(GxEPD_BLACK);

  // Initial Clear
  display.setFullWindow();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());

  connectToWifi(false);
  syncTime(false);

  showStatusMessage("Pobieranie danych...");
  globalSchedules = fetchScheduleFromServer(busStops[currentBusStop].id, busStops[currentBusStop].nr[currentBusStopNrIndex]);
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  drawBackground(true, false);
  
  previousRefreshMillis = millis(); 
  refreshCounter = 1; 

  refreshLogic();
}

void loop() {
  encoderBtn.loop();
  
  if (encoderBtn.isPressed()) {
    buttonPressStart = millis();
  }

  // Handle Deep Sleep (holding > 6 seconds)
  if (encoderBtn.getState() == 0 && buttonPressStart != 0) {
      if (millis() - buttonPressStart > 6000) {
          enterDeepSleep();
      }
  }

  // Handle Button Release
  if (encoderBtn.isReleased()) {
    long duration = millis() - buttonPressStart;
    buttonPressStart = 0; // Reset timer

    // Ignore if duration was long enough to trigger deep sleep
    if (duration <= 6000) {
      
      // 1s - 6s
      if (duration >= 1000) {
         changeStopMode = !changeStopMode;
         
         if (changeStopMode) {
           // Enter Change Mode
           changingBusStop = currentBusStop;
           drawBackground(false, true);
         } else {
           // Exit Change Mode (Save & Refresh)
           currentBusStop = changingBusStop;
           globalSchedules.clear(); 
           currentBusStopNrIndex = 0;
           refreshCounter = 1;
           drawBackground(false, false);
           refreshLogic();
         }
      }
      // < 1s
      else {
        if (changeStopMode) {
           // If in Change Mode: Short click CONFIRMS selection
           changeStopMode = false;
           currentBusStop = changingBusStop;
           globalSchedules.clear(); 
           currentBusStopNrIndex = 0;
           refreshCounter = 1;
           drawBackground(false, false);
           refreshLogic();
        } else {
           currentBusStopNrIndex++;
           if (currentBusStopNrIndex >= busStops[currentBusStop].nr.size()) {
             currentBusStopNrIndex = 0;
           }
           
           // Clear data and refresh
           globalSchedules.clear();
           refreshCounter = 1;
          //  showStatusMessage("Zmiana numeru...");
           drawBackground(false, false);
           refreshLogic();
        }
      }
    }
  }

  // Encoder Rotation Logic
  long newPosition = encoder.read() / 4;
  if (changeStopMode) {
    if (newPosition != prevEncoderPos) {
      if (newPosition > prevEncoderPos) {
          changingBusStop = (changingBusStop + 1) % busStops.size();
      } else {
          changingBusStop = (changingBusStop - 1 + busStops.size()) % busStops.size();
      }
    
      Serial.printf("Encoder position: %d. New stop: %s\n", (int)newPosition, busStops[changingBusStop].name.c_str());
      prevEncoderPos = newPosition;
      
      // Update display with new candidate name
      drawBackground(false, true);
    }
  } else {
    // Keep encoder position synced even if not in change mode to prevent jumps
    prevEncoderPos = newPosition;
  }

  // Automatic Timer Refresh
  unsigned long currentMillis = millis();
  if (currentMillis - previousRefreshMillis >= REFRESH_INTERVAL_MS) {
    previousRefreshMillis = currentMillis; 
    refreshLogic();
  }
}

void enterDeepSleep() {
    Serial.println("Long press detected. Shutting down...");
    
    // Clear and power off the display
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
    display.powerOff();

    // Wait for button release
    Serial.println("Waiting for button release...");
    while(digitalRead(RT_SW) == 0) {
        delay(50);
    }
    
    Serial.println("Entering Deep Sleep.");
    Serial.flush(); 
    
    esp_deep_sleep_start();
}

void refreshLogic() {
  Serial.println("--- Starting Scheduled Refresh ---");
  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);
  if (!timeValid) {
      connectToWifi(true);
      syncTime(true);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      getLocalTime(&timeinfo);
  }
  removePastSchedules(globalSchedules);
  if (globalSchedules.size() <= 3) {
      connectToWifi(true);
      syncTime(true); 
      std::vector<ScheduleEntry> freshData = fetchScheduleFromServer(busStops[currentBusStop].id, busStops[currentBusStop].nr[currentBusStopNrIndex]);
      if (!freshData.empty()) globalSchedules = freshData;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
  } 
  refreshCounter++;
  if (refreshCounter >= FULL_REFRESH_THRESHOLD) {
      drawBackground(true, false);
      refreshCounter = 0;
  }
  std::vector<ScheduleEntry> displayList = convertScheduleTimesToMinutesLeft(globalSchedules);
  displayList.resize(std::min((size_t)3, displayList.size())); 
  display.setPartialWindow(0, secondLineY + 1, display.width(), display.height() - secondLineY - 1);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (displayList.empty()) {
       u8g2.setFont(u8g2_font_helvB18_te);
       displayTextOnCenterHorizontally("Brak odjazdów", schedulesY + 50);
    } else {
       displayBusStopSchedule(schedulesY, displayList);
    }
  } while (display.nextPage());
  display.hibernate();
}

void drawBackground(bool isFullRefresh, bool isChangingBusStop) {
  if (isFullRefresh) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), 100); 
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Display bus stop name
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setFont(u8g2_font_helvB18_te);
    displayTextOnCenterHorizontally(isChangingBusStop ? "< " + busStops[changingBusStop].name + " >" : busStops[changingBusStop].name + " " + busStops[changingBusStop].nr[currentBusStopNrIndex], BUS_STOP_NAME_Y_OFFSET);

    int lineY = (int)(1.5 * BUS_STOP_NAME_Y_OFFSET);
    display.drawLine(X_MARGIN, lineY, display.width() - X_MARGIN, lineY, GxEPD_BLACK);
    secondLineY = lineY + HEADER_ROW_HEIGHT;
    display.drawLine(X_MARGIN, secondLineY, display.width() - X_MARGIN, secondLineY, GxEPD_BLACK);
    u8g2.setFont(u8g2_font_helvR12_te);
    u8g2.setForegroundColor(GxEPD_BLACK);
    int8_t fontAscent = u8g2.getFontAscent();
    int headerY = lineY + (HEADER_ROW_HEIGHT / 2) + (fontAscent / 2);
    u8g2.setCursor(X_MARGIN + LINE_TEXTBOX_PADDING, headerY);
    u8g2.print("Linia");
    displayTextOnCenterHorizontally("Kierunek", headerY);
    int16_t w = u8g2.getUTF8Width("Odjazd");
    u8g2.setCursor(display.width() - X_MARGIN - w, headerY);
    u8g2.print("Odjazd");
    schedulesY = secondLineY + 45;
  } while (display.nextPage());
}

void connectToWifi(bool silent) {
  if (!silent) showStatusMessage("Łączenie z WiFi...");
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  int r = 0;
  while (WiFi.status() != WL_CONNECTED && r < 20) { delay(500); r++; }
  if (!silent && WiFi.status() != WL_CONNECTED) showStatusMessage("Błąd WiFi!");
}

void syncTime(bool silent) {
  if (!silent) showStatusMessage("Synchronizacja Czasu...");
  configTime(0, 0, ntpServer);
  setenv("TZ", tzInfo, 1);
  tzset();
  struct tm t;
  int r = 0;
  while (!getLocalTime(&t, 100) && r < 20) { r++; delay(100); }
}


void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules) {
  int16_t yPos = y;
  for (const auto &entry : schedules) {
    if (yPos > display.height()) break;
    
    u8g2.setFont(u8g2_font_helvB18_te);
    
    int actualTextWidth = u8g2.getUTF8Width(entry.line.c_str());
    int boxContentWidth;

    // Fixed for trams/short numbers, dynamic for buses etc.
    if (entry.line.length() <= 2) { 
        boxContentWidth = u8g2.getUTF8Width("55"); // Use "55" as standard width for 1-2 chars
    } else { 
        boxContentWidth = actualTextWidth;
    }

    int h = u8g2.getFontAscent();
    int rX = X_MARGIN;
    int rY = yPos - h - LINE_TEXTBOX_PADDING;
    int rW = boxContentWidth + (2 * LINE_TEXTBOX_PADDING);
    int rH = h + (2 * LINE_TEXTBOX_PADDING);

    // Draw box
    display.fillRect(rX, rY, rW, rH, GxEPD_BLACK);

    // Draw text (centered horizontally inside the box)
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(GxEPD_BLACK);
    
    int textX = rX + (rW - actualTextWidth) / 2;
    
    u8g2.setCursor(textX, yPos);
    u8g2.print(entry.line);

    // Draw time and destination
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
    u8g2.setFont(u8g2_font_helvB18_te);
    
    int w = u8g2.getUTF8Width(entry.time.c_str());
    int tX = display.width() - X_MARGIN - w;
    u8g2.setCursor(tX, yPos);
    u8g2.print(entry.time);

    u8g2.setFont(u8g2_font_helvR18_te);
    int dX = rX + rW + 10;
    int maxW = tX - dX - 5;
    
    String pD = entry.direction;
    while (pD.length() > 0 && u8g2.getUTF8Width((pD + "...").c_str()) > maxW) {
       pD.remove(pD.length() - 1);
    }
    if (pD != entry.direction) pD += "...";
    
    u8g2.setCursor(dX, yPos);
    u8g2.print(pD);
    
    yPos += Y_OFFSET;
  }
}

void showStatusMessage(const String &text) {
  int16_t h = 60;
  int16_t y = (display.height() - h) / 2;
  display.setPartialWindow(0, y, display.width(), h);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(10, y, display.width()-20, h, GxEPD_BLACK);
    displayTextOnCenter(text);
  } while (display.nextPage());
}

void displayTextOnCenter(const String &text) {
  u8g2.setFont(u8g2_font_helvR18_te);
  int16_t w = u8g2.getUTF8Width(text.c_str());
  int16_t x = (display.width() - w) / 2;
  int16_t y = (display.height() / 2) + (u8g2.getFontAscent() / 2);
  u8g2.setCursor(x, y);
  u8g2.print(text);
}

void displayTextOnCenterHorizontally(const String &text, const int y) {
  int16_t w = u8g2.getUTF8Width(text.c_str());
  int16_t x = (display.width() - w) / 2;
  u8g2.setCursor(x, y);
  u8g2.print(text);
}