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
static const uint8_t RT_SW = 2; // Button Pin

Encoder encoder(RT_CLK, RT_DW);
int prevEncoderPos = 0;

ezButton encoderBtn(RT_SW);

// Variable to track button holding time
unsigned long buttonPressStart = 0;

struct BUS_STOP_INFO {
    String id;
    std::vector<String> nr;
    String name;
};

const std::array<BUS_STOP_INFO, 3> busStops = {{
    {"7009", {"01", "02"}, "Marszałkowska"},
    {"2097", {"01", "02", "03", "04"}, "Saska"},
    {"2329", {"01", "02"}, "Majdan Widoczna"}
}};

uint8_t currentBusStop = 0;
uint8_t currentBusStopNrIndex = 0;

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
void drawBackground(bool isFullRefresh);
void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules);
void displayTextOnCenterHorizontally(const String &text, const int y);
void displayTextOnCenter(const String &text);
void showStatusMessage(const String &text);
void displayBusStopName(const String &busStopName, const String &busStopNr);
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

  drawBackground(true);
  
  previousRefreshMillis = millis(); 
  refreshCounter = 1; 

  refreshLogic();
}

void loop() {
  encoderBtn.loop();
  
  // Shutdown logic
  if (encoderBtn.getState() == 0) { 
      // If we weren't holding it before, mark the start time
      if (buttonPressStart == 0) {
          buttonPressStart = millis();
      }
      else if (millis() - buttonPressStart > 5000) {
          enterDeepSleep();
      }
  } else {
      buttonPressStart = 0;
  }

  long newPosition = encoder.read() / 4;
  bool actionTaken = false;

  // Encoder Rotation Logic
  if (newPosition != prevEncoderPos) {
    if (newPosition > prevEncoderPos) {
        currentBusStop = (currentBusStop + 1) % busStops.size();
    } else {
        currentBusStop = (currentBusStop - 1 + busStops.size()) % busStops.size();
    }
    currentBusStopNrIndex = 0;
    
    Serial.printf("Encoder position: %d. New stop: %s\n", (int)newPosition, busStops[currentBusStop].name.c_str());
    prevEncoderPos = newPosition;
    actionTaken = true;
  }

  // Short Press Logic (Only triggers on release-to-press transition)
  if (encoderBtn.isPressed()) {
      currentBusStopNrIndex = (currentBusStopNrIndex + 1) % busStops[currentBusStop].nr.size();
      Serial.printf("Button pressed. New bus stop number: %s\n", busStops[currentBusStop].nr[currentBusStopNrIndex].c_str());
      actionTaken = true;
  }

  if (actionTaken) {
      Serial.println("Input detected. Updating display...");
      globalSchedules.clear(); 
      drawBackground(true);
      refreshCounter = 1; 
      refreshLogic(); 
      previousRefreshMillis = millis(); 
  }
  
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
      drawBackground(true);
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

void drawBackground(bool isFullRefresh) {
  if (isFullRefresh) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), 100); 
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    displayBusStopName(busStops[currentBusStop].name, busStops[currentBusStop].nr[currentBusStopNrIndex]);
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
    u8g2.setFont(u8g2_font_helvB18_te);
    w = u8g2.getUTF8Width(entry.time.c_str());
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