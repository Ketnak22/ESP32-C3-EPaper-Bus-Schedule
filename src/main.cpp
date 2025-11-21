#include <Arduino.h>
#include <vector>

#include <WiFi.h>
#include <time.h>

#include <ztm_api.h>
#include "secrets.h"

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;

// --- NTP Configuration ---
const char *ntpServer = "pool.ntp.org";
// Timezone for Warsaw, Poland (Central European Time with Daylight Saving)
const char *tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3";

const String WIDOCZNA_BUS_STOP_ID = "2329";
const String MARSZALKOWSKA_BUS_STOP_ID = "7009";

const std::vector<String> WIDOCZNA_BUS_STOP_NRS = {String("01"), String("02")};
const std::vector<String> MARSZALKOWSKA_BUS_STOP_NRS = {String("01"), String("02")};

#define ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

static const uint8_t X_MARGIN = 25;
static const uint8_t LINE_TEXTBOX_PADDING = 10;
static const uint8_t Y_OFFSET = 60;
static const uint8_t BUS_STOP_NAME_Y_OFFSET = 30;
static const uint8_t HEADER_Y_OFFSET = 20;

// Connections for ESP32-C3 Supermini
static const uint8_t EPD_BUSY = 21; // to EPD BUSY labled as 'TX' pin
static const uint8_t EPD_CS = 5;    // to EPD CS
static const uint8_t EPD_RST = 7;   // to EPD RST
static const uint8_t EPD_DC = 6;    // to EPD DC
static const uint8_t EPD_SCK = 8;   // to EPD CLK labled as 'RX' pin
static const uint8_t EPD_MOSI = 10; // to EPD DIN / SDA

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

std::vector<ScheduleEntry> schedules;

int schedulesY = 0;
int lineY = 0;
int headerY = 0;
int secondLineY = 0;

/* --- Function Declarations --- */
void connectToWifi();
void syncTime();

void displayBusStopName(const String &busStopName, const std::vector<String> &busStopNrs);
void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules);

void displayTextOnCenter(const String &text); // Helper to display centered text
void displayTextOnCenterHorizontally(const String &text, const int y); // Helper to display centered text at specific y position
void showStatusMessage(const String &text); // Helper to display status messages

const int getFontSizeInPixels(const GFXfont *font)
{
  return (font->yAdvance);
}

void clearScreenForStorage()
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  display.powerOff();
}

void setup()
{
  // Serial.begin(115200);
  // unsigned long start = millis();
  // while (!Serial && millis() - start < 2000);


  // Initialize display
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200, true, 50, false);

  // Ensure font is selected for status messages
  display.setFont(&FreeSansBold12pt7b);

  // Initial full clear to ensure clean state
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  connectToWifi();
  syncTime(); 

  showStatusMessage("Pobieranie danych...");

  schedules = getStopScheduleBusstopComplex(WIDOCZNA_BUS_STOP_ID, WIDOCZNA_BUS_STOP_NRS);

  delay(100); // Small delay to ensure screen update completes before heavy processing

  display.setFont(&FreeSans9pt7b);

  // --- Main Content Refresh (Full Window for clean look) ---
  display.setFullWindow(); 
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE); // Clear screen

    // Display bus stop name
    displayBusStopName("Majdan Widoczna", WIDOCZNA_BUS_STOP_NRS);

    // Display line separator
    lineY = (int)(1.5 * BUS_STOP_NAME_Y_OFFSET);
    display.drawLine(X_MARGIN, lineY, display.width() - X_MARGIN, lineY, GxEPD_BLACK);

    headerY = lineY + HEADER_Y_OFFSET;
    // Display schedule header
    display.setFont(&FreeSans9pt7b);
    display.setCursor(X_MARGIN + LINE_TEXTBOX_PADDING, headerY);
    display.setTextColor(GxEPD_BLACK);
    display.print("Linia");
    displayTextOnCenterHorizontally("Kierunek", headerY);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("Odjazd", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - X_MARGIN - w, headerY);
    display.print("Odjazd");

    secondLineY = headerY + HEADER_Y_OFFSET / 2;
    display.drawLine(X_MARGIN, secondLineY, display.width() - X_MARGIN, secondLineY, GxEPD_BLACK);

    schedulesY = secondLineY + BUS_STOP_NAME_Y_OFFSET + LINE_TEXTBOX_PADDING;
  } while (display.nextPage());
  // delay(60000);

  // clearScreenForStorage();
}

void loop()
{
  std::vector<ScheduleEntry> displayedSchedules = getFutureSchedules(schedules, 3);
  displayedSchedules = convertScheduleTimesToMinutesLeft(displayedSchedules);

  // Set partial window for schedules
  display.setPartialWindow(0, secondLineY + 1, display.width(), display.height() - secondLineY - 1);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE); // Clear partial window

    // Display bus stop schedule
    displayBusStopSchedule(schedulesY, displayedSchedules);
  } while (display.nextPage());

  // Put display to sleep to save power
  display.hibernate();

  // Calculate wakeup time at the start of the next minute
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    return; // Can't sleep without valid time
  }
  time_t now = mktime(&timeinfo);
  time_t nextMinute = now + (60 - timeinfo.tm_sec);
  time_t sleepDuration = nextMinute - now;

  // Put ESP32-C3 to light sleep for minute until next update
  delay(sleepDuration * 1000);

  // esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
  // esp_light_sleep_start();
  // delay(60000); // For testing, use delay instead of sleep
}

// Helper to display centered text using Partial Refresh to reduce flashing
void showStatusMessage(const String &text) {
  // Define a partial window in the center of the screen (height 60px)
  int16_t h = 60;
  int16_t y_start = (display.height() - h) / 2;
  
  display.setPartialWindow(0, y_start, display.width(), h);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE); // Clear only the partial window
    displayTextOnCenter(text);
  } while (display.nextPage());
}

void displayTextOnCenter(const String &text) {
  int16_t x1, y1;
  uint16_t w, h;
  // Ensure font is set before calculating bounds
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  int16_t y = (display.height() - h) / 2 + h; 
  display.setCursor(x, y);
  display.setTextColor(GxEPD_BLACK);
  display.print(text);
}

void displayTextOnCenterHorizontally(const String &text, const int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, y);
  display.setTextColor(GxEPD_BLACK);
  display.print(text);
}

void connectToWifi()
{
  showStatusMessage("Łączenie z WiFi...");

  WiFi.begin(ssid, password);
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20)
  {
    delay(500);
    retryCount++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    showStatusMessage("Połączono z WiFi!");
    // Optional: Display IP in a second pass if needed, or combine strings
    // showStatusMessage("IP: " + WiFi.localIP().toString());
  } else {
    showStatusMessage("Nie udało się połączyć z WiFi!");
    delay(2000);
  }
}

void syncTime()
{
  showStatusMessage("Synchronizowanie czasu...");

  configTime(0, 0, ntpServer);
  setenv("TZ", tzInfo, 1);
  tzset();

  struct tm timeinfo;
  // Wait up to 10s for sync
  int retry = 0;
  while (!getLocalTime(&timeinfo, 100) && retry < 50)
  { 
    retry++;
    delay(100);
  }

  if (retry < 50) {
    showStatusMessage("Czas zsynchronizowany!");
  } else {
    showStatusMessage("Synchronizacja czasu nie powiodła się!");
  }
  delay(500); // Short pause to let user read
}

void displayBusStopName(const String &busStopName, const std::vector<String> &busStopNrs)
{
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSansBold12pt7b);

  String displayedText = busStopName;

  for (const auto &busNr : busStopNrs)
  {
    displayedText += " " + String(busNr) + " &";
  }
  if (displayedText.endsWith(" &")) {
      displayedText.remove(displayedText.length() - 2); 
  }

  displayTextOnCenterHorizontally(displayedText, BUS_STOP_NAME_Y_OFFSET);
}

void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules)
{
  if (schedules.empty()) {
    display.setFont(&FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    displayTextOnCenterHorizontally("Brak odjazdów", y + Y_OFFSET);
    return;
  }

  int16_t x1, y1;
  uint16_t w, h;

  int16_t yPosition = y;
  for (const auto &entry : schedules) 
  {

    if (yPosition > display.height())
    {
      break; // Stop if we reach the bottom of the display
    }

    // Line number in bold font in black box
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(X_MARGIN + LINE_TEXTBOX_PADDING, yPosition);

    display.getTextBounds(entry.line, X_MARGIN + LINE_TEXTBOX_PADDING, yPosition, &x1, &y1, &w, &h);

    const int rectX = x1 - LINE_TEXTBOX_PADDING;
    const int rectY = yPosition - h - LINE_TEXTBOX_PADDING;
    const int rectWidth = w + (2 * LINE_TEXTBOX_PADDING);
    display.fillRect(rectX, rectY, rectWidth, h + (2 * LINE_TEXTBOX_PADDING), GxEPD_BLACK); // Clear previous text

    display.setTextColor(GxEPD_WHITE); // Invert text color for visibility
    display.print(entry.line);

    // Calculate time X position for later use
    display.getTextBounds(entry.time, 0, 0, &x1, &y1, &w, &h);
    const int timeX = display.width() - X_MARGIN - w;

    // Direction in regular font next to line number
    display.setTextColor(GxEPD_BLACK); // Reset text color for direction
    display.setFont(&FreeSans12pt7b);
    const int directionX = rectX + rectWidth + (rectWidth / 4);
    display.getTextBounds(entry.direction, directionX, yPosition, &x1, &y1, &w, &h);
    if (directionX + w + 50 > timeX) {
      // If direction text would overflow, truncate it
      String truncatedDirection = entry.direction;
      while (truncatedDirection.length() > 0) {
        truncatedDirection.remove(truncatedDirection.length() - 1);
        display.getTextBounds(truncatedDirection + "...", 0, 0, &x1, &y1, &w, &h);
        if (directionX + w <= timeX) {
          truncatedDirection += "...";
          break;
        }
      }
      display.setCursor(directionX, yPosition);
      display.print(truncatedDirection);
    } else {
      display.setCursor(directionX, yPosition);
      display.print(entry.direction);
    }

    // Time aligned to the right in bold font
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(timeX, yPosition);
    display.print(entry.time);
    
    yPosition += Y_OFFSET;
  }
}