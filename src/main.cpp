#include <Arduino.h>
#include <vector>
#include <WiFi.h>
#include <time.h>

#include "ztm_api.h"
#include "secrets.h"

#include <U8g2_for_Adafruit_GFX.h>

const String BUS_STOP_ID = "7009"; // Marszałkowska
const String BUS_STOP_NR = "01";
const String BUS_STOP_NAME = "Marszałkowska";

// Assuming refresh every 1 minute, do a full refresh every 60 minutes
const int FULL_REFRESH_THRESHOLD = 60; 

const char *ntpServer = "pool.ntp.org";
const char *tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3";

#define ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>

// Pin Declarations
static const uint8_t EPD_BUSY = 21;
static const uint8_t EPD_CS = 5;
static const uint8_t EPD_RST = 7;
static const uint8_t EPD_DC = 6;
static const uint8_t EPD_SCK = 8;
static const uint8_t EPD_MOSI = 10;

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

// Global cache of schedules
std::vector<ScheduleEntry> globalSchedules;

// Refresh Counter (Stored in RTC memory to survive sleep)
RTC_DATA_ATTR int refreshCounter = 0;

// Declarations
void connectToWifi(bool silent);
void syncTime(bool silent);
void drawBackground(bool isFullRefresh);
void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules);
void displayTextOnCenterHorizontally(const String &text, const int y);
void displayTextOnCenter(const String &text);
void showStatusMessage(const String &text);
void displayBusStopName(const String &busStopName, const String &busStopNr);

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

  // Setup Network & Time
  connectToWifi(false);
  syncTime(false);

  // Initial Fetch
  showStatusMessage("Pobieranie danych...");
  globalSchedules = fetchScheduleFromServer(BUS_STOP_ID, BUS_STOP_NR);
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Initial full draw
  drawBackground(true);
  
  // Reset counter on clean boot
  refreshCounter = 0; 
}

void loop() {
  
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
      
      Serial.println("Fetching new data...");
      
      connectToWifi(true);
      syncTime(true); 

      // Fetch fresh data
      std::vector<ScheduleEntry> freshData = fetchScheduleFromServer(BUS_STOP_ID, BUS_STOP_NR);
      
      if (!freshData.empty()) {
          globalSchedules = freshData;
      }
      
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
  } else {
      Serial.printf("Cache ok (%d buses). Skipping WiFi.\n", globalSchedules.size());
  }

  // Full refresh logic
  refreshCounter++;
  
  if (refreshCounter >= FULL_REFRESH_THRESHOLD) {
      Serial.println("Triggering Full Refresh to clean ghosting...");
      drawBackground(true);
      refreshCounter = 0;
  }

  // Calculate x minutes left
  std::vector<ScheduleEntry> displayList = convertScheduleTimesToMinutesLeft(globalSchedules);
  displayList.resize(std::min((size_t)3, displayList.size())); // Limit to 3 entries

  // Populate schedules
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

  // Sleep
  if (!getLocalTime(&timeinfo)) return;
  int secondsToSleep = (60 - timeinfo.tm_sec) + 2; 
  uint64_t sleepUs = (uint64_t)secondsToSleep * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  
  esp_light_sleep_start();
}

// --- HELPERS ---

void drawBackground(bool isFullRefresh) {
  if (isFullRefresh) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), 100); 

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    displayBusStopName(BUS_STOP_NAME, BUS_STOP_NR);

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
  if (!silent) showStatusMessage("Sync. Czasu...");
  configTime(0, 0, ntpServer);
  setenv("TZ", tzInfo, 1);
  tzset();
  struct tm t;
  int r = 0;
  while (!getLocalTime(&t, 100) && r < 20) { r++; delay(100); }
}

void displayBusStopName(const String &busStopName, const String &busStopNr) {
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setFont(u8g2_font_helvB18_te);
  displayTextOnCenterHorizontally(busStopName + " " + busStopNr, BUS_STOP_NAME_Y_OFFSET);
}

void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules) {
  int16_t yPos = y;
  for (const auto &entry : schedules) {
    if (yPos > display.height()) break;

    // Line
    u8g2.setFont(u8g2_font_helvB18_te);
    int w = u8g2.getUTF8Width(entry.line.c_str());
    int h = u8g2.getFontAscent();
    int rX = X_MARGIN;
    int rY = yPos - h - LINE_TEXTBOX_PADDING;
    int rW = w + (2 * LINE_TEXTBOX_PADDING);
    int rH = h + (2 * LINE_TEXTBOX_PADDING);

    display.fillRect(rX, rY, rW, rH, GxEPD_BLACK);
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(GxEPD_BLACK);
    u8g2.setCursor(rX + LINE_TEXTBOX_PADDING, yPos);
    u8g2.print(entry.line);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    // Time
    u8g2.setFont(u8g2_font_helvB18_te);
    w = u8g2.getUTF8Width(entry.time.c_str());
    int tX = display.width() - X_MARGIN - w;
    u8g2.setCursor(tX, yPos);
    u8g2.print(entry.time);

    // Direction
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