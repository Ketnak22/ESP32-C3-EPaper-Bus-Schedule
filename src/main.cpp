#include <Arduino.h>
#include <vector>

#include <WiFi.h>
#include <time.h>

#include <ztm_api.h>
#include "secrets.h"

#include <U8g2_for_Adafruit_GFX.h>

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;

const char *ntpServer = "pool.ntp.org";
const char *tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3";

const String WIDOCZNA_BUS_STOP_ID = "2329";
const std::vector<String> WIDOCZNA_BUS_STOP_NRS = {String("01"), String("02")};
const String WIDOCZNA_BUS_STOP_NAME = "Majdan Widoczna";

const String PRASKIEGO_PULKU_BUS_STOP_ID = "2293"; 
const std::vector<String> PRASKIEGO_PULKU_BUS_STOP_NRS = {String("01"), String("02")};
const String PRASKIEGO_PULKU_BUS_STOP_NAME = "1. Praskiego Pułku";

#define ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>

// Connections for ESP32-C3 Supermini
static const uint8_t EPD_BUSY = 21;
static const uint8_t EPD_CS = 5;
static const uint8_t EPD_RST = 7;
static const uint8_t EPD_DC = 6;
static const uint8_t EPD_SCK = 8;
static const uint8_t EPD_MOSI = 10;

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

U8G2_FOR_ADAFRUIT_GFX u8g2;

std::vector<ScheduleEntry> schedules;

static const uint8_t X_MARGIN = 25;
static const uint8_t LINE_TEXTBOX_PADDING = 8;
static const uint8_t Y_OFFSET = 50;
static const uint8_t BUS_STOP_NAME_Y_OFFSET = 45;
static const uint8_t HEADER_ROW_HEIGHT = 34;

int schedulesY = 0;
int secondLineY = 0;
int lastMinute = -1;

/* --- FUNCTION DECLARATIONS --- */
void connectToWifi();
void syncTime();
void drawBackground(bool isFullRefresh);
void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules);
void displayTextOnCenter(const String &text);
void displayTextOnCenterHorizontally(const String &text, const int y);
void showStatusMessage(const String &text);
void displayBusStopName(const String &busStopName, const std::vector<String> &busStopNrs);

void setup()
{
  Serial.begin(115200);
  // Wait for serial or timeout (standard ESP32-C3 check)
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200, true, 50, false);

  // Full clear display for long term storage
  // To delete later
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
  display.hibernate();
  while (true) {
    delay(1000);
  }

  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  u8g2.setForegroundColor(GxEPD_BLACK);

  // Initial Full Clear
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  connectToWifi();
  syncTime();

  showStatusMessage("Pobieranie danych...");
  schedules = getStopScheduleBusstopComplex(WIDOCZNA_BUS_STOP_ID, WIDOCZNA_BUS_STOP_NRS);

  showStatusMessage("Usypianie WiFi...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Draw the initial Interface
  drawBackground(true);
}

void loop()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    connectToWifi();
    syncTime();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return; 
  }

  // Full Refresh Every Hour
  // Check if minute is 0 and we haven't done it yet this minute
  if (timeinfo.tm_min == 0 && lastMinute != 0)
  {
    // Perform Full Refresh to clear ghosting.
    drawBackground(true);
  }
  lastMinute = timeinfo.tm_min;

  // Data refresh
  std::vector<ScheduleEntry> displayedSchedules = getFutureSchedules(schedules, 3);

  if (displayedSchedules.empty())
  {
    connectToWifi();
    syncTime();

    schedules = getStopScheduleBusstopComplex(WIDOCZNA_BUS_STOP_ID, WIDOCZNA_BUS_STOP_NRS);
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Refresh the list
    displayedSchedules = getFutureSchedules(schedules, 3);
    
    // If still empty, show error and sleep
    if (displayedSchedules.empty())
    {
        showStatusMessage("Brak odjazd\xC3\xB3""w!"); // Brak odjazdów
        // Sleep for a minute before trying again
        // Note: Status message already does a refresh, so we can just sleep
    }
    else 
    {
        // Data found, redraw background (partial) to clear the "Fetching data" status message
        // passing 'false' means it clears the status area but keeps the main UI
        drawBackground(false); 
    }
  }

  if (!displayedSchedules.empty()) 
  {
    displayedSchedules = convertScheduleTimesToMinutesLeft(displayedSchedules);

    // Partial Refresh for the schedule area only
    display.setPartialWindow(0, secondLineY + 1, display.width(), display.height() - secondLineY - 1);
    display.firstPage();
    do
    {
      display.fillScreen(GxEPD_WHITE);
      displayBusStopSchedule(schedulesY, displayedSchedules);
    } while (display.nextPage());
  }

  display.hibernate();

  // Recalculate time to sleep until the next full minute
  if (!getLocalTime(&timeinfo)) return;
  
  time_t now = mktime(&timeinfo);
  time_t nextMinute = now + (60 - timeinfo.tm_sec); // Seconds until next minute
  
  // Calculate duration in microseconds for ESP32 timer
  uint64_t sleepDurationUs = (uint64_t)(nextMinute - now) * 1000000ULL;

  // Ensure we sleep at least 1 second to avoid rapid loops
  if(sleepDurationUs < 1000000ULL) sleepDurationUs = 1000000ULL;

  esp_sleep_enable_timer_wakeup(sleepDurationUs);
  esp_light_sleep_start();
}

/* Function Definitions */

// Draw static parts of the display
void drawBackground(bool isFullRefresh) {
  if (isFullRefresh) {
    display.setFullWindow();
  } else {
    // Redraw headers in case a Status Message overwrote them.
    // Refresh the top part of the screen.
    display.setPartialWindow(0, 0, display.width(), secondLineY + 2);
  }

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    displayBusStopName(WIDOCZNA_BUS_STOP_NAME, WIDOCZNA_BUS_STOP_NRS);

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

void showStatusMessage(const String &text)
{
  // Center screen popup
  int16_t h = 60;
  int16_t y_start = (display.height() - h) / 2;

  display.setPartialWindow(0, y_start, display.width(), h);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(10, y_start, display.width()-20, h, GxEPD_BLACK);
    displayTextOnCenter(text);
  } while (display.nextPage());
}

void displayTextOnCenter(const String &text)
{
  u8g2.setFont(u8g2_font_helvR18_te);
  int16_t w = u8g2.getUTF8Width(text.c_str());
  int16_t h = u8g2.getFontAscent();
  int16_t x = (display.width() - w) / 2;
  int16_t y = (display.height() / 2) + (h / 2);

  u8g2.setCursor(x, y);
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.print(text);
}

void displayTextOnCenterHorizontally(const String &text, const int y)
{
  int16_t w = u8g2.getUTF8Width(text.c_str());
  int16_t x = (display.width() - w) / 2;
  u8g2.setCursor(x, y);
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.print(text);
}

void connectToWifi()
{
  showStatusMessage("\xC5\x81\xC4\x85""czenie z WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20)
  {
    delay(500);
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    showStatusMessage("Po\xC5\x82\xC4\x85""czono z WiFi!");
  }
  else
  {
    showStatusMessage("B\xC5\x82\xC4\x85""d WiFi!");
    delay(2000);
  }
}

void syncTime()
{
  showStatusMessage("Synchronizacja czasu...");
  configTime(0, 0, ntpServer);
  setenv("TZ", tzInfo, 1);
  tzset();

  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo, 100) && retry < 50)
  {
    retry++;
    delay(100);
  }
}

void displayBusStopName(const String &busStopName, const std::vector<String> &busStopNrs)
{
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setFont(u8g2_font_helvB18_te);

  String displayedText = busStopName;
  for (const auto &busNr : busStopNrs)
  {
    displayedText += " " + String(busNr) + " &";
  }
  if (displayedText.endsWith(" &"))
  {
    displayedText.remove(displayedText.length() - 2);
  }

  displayTextOnCenterHorizontally(displayedText, BUS_STOP_NAME_Y_OFFSET);
}

void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules)
{
  if (schedules.empty())
  {
    u8g2.setFont(u8g2_font_helvB18_te);
    u8g2.setForegroundColor(GxEPD_BLACK);
    displayTextOnCenterHorizontally("Brak odjazdów", y + Y_OFFSET);
    return;
  }

  int16_t yPosition = y;
  for (const auto &entry : schedules)
  {
    if (yPosition > display.height()) break;

    // --- Line Number Box ---
    u8g2.setFont(u8g2_font_helvB18_te);
    int w = u8g2.getUTF8Width(entry.line.c_str());
    int h = u8g2.getFontAscent();

    const int rectX = X_MARGIN;
    const int rectY = yPosition - h - LINE_TEXTBOX_PADDING;
    const int rectWidth = w + (2 * LINE_TEXTBOX_PADDING);
    const int rectHeight = h + (2 * LINE_TEXTBOX_PADDING);

    display.fillRect(rectX, rectY, rectWidth, rectHeight, GxEPD_BLACK);

    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(GxEPD_BLACK);
    u8g2.setCursor(rectX + LINE_TEXTBOX_PADDING, yPosition);
    u8g2.print(entry.line);

    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    // --- Time ---
    u8g2.setFont(u8g2_font_helvB18_te);
    w = u8g2.getUTF8Width(entry.time.c_str());
    const int timeX = display.width() - X_MARGIN - w;
    u8g2.setCursor(timeX, yPosition);
    u8g2.print(entry.time);

    // --- Direction ---
    u8g2.setFont(u8g2_font_helvR18_te);
    const int directionX = rectX + rectWidth + (rectWidth / 4);
    int maxDirWidth = timeX - directionX - 10;

    String printDir = entry.direction;
    if (u8g2.getUTF8Width(printDir.c_str()) > maxDirWidth)
    {
      while (printDir.length() > 0 && u8g2.getUTF8Width((printDir + "...").c_str()) > maxDirWidth)
      {
        printDir.remove(printDir.length() - 1);
      }
      printDir += "...";
    }

    u8g2.setCursor(directionX, yPosition);
    u8g2.print(printDir);

    yPosition += Y_OFFSET;
  }
}