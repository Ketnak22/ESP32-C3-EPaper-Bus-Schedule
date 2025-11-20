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

#define LED_INDICATOR 8

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

// Connections for ESP32-C3 Supermini
static const uint8_t EPD_BUSY = 21; // to EPD BUSY labled as 'TX' pin
static const uint8_t EPD_CS = 5;    // to EPD CS
static const uint8_t EPD_RST = 7;   // to EPD RST
static const uint8_t EPD_DC = 6;    // to EPD DC
static const uint8_t EPD_SCK = 8;   // to EPD CLK labled as 'RX' pin
static const uint8_t EPD_MOSI = 10; // to EPD DIN / SDA

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

/* --- Function Declarations --- */
void connectToWifi();
void syncTime();

void displayBusStopName(const String &busStopId, const String &busStopName, const std::vector<String> &busStopNrs);
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
  // Setup built-in LED for status indication
  pinMode(LED_INDICATOR, OUTPUT);
  digitalWrite(LED_INDICATOR, HIGH);

  // Initialize display
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200, true, 50, false);

  // Ensure font is selected for status messages
  display.setFont(&FreeSansBold18pt7b);

  // Initial full clear to ensure clean state
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  digitalWrite(LED_INDICATOR, LOW);

  connectToWifi();
  syncTime(); 

  showStatusMessage("Downloading data...");

  std::vector<ScheduleEntry> schedules = getStopScheduleBusstopComplex(WIDOCZNA_BUS_STOP_ID, WIDOCZNA_BUS_STOP_NRS);
  schedules = getFutureSchedules(schedules);
  schedules = convertScheduleTimesToMinutesLeft(schedules);

  delay(100); // Small delay to ensure screen update completes before heavy processing

  display.setFont(&FreeSans9pt7b);

  // --- Main Content Refresh (Full Window for clean look) ---
  display.setFullWindow(); 
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // Display bus stop name
    displayBusStopName(WIDOCZNA_BUS_STOP_ID, "Majdan Widoczna", WIDOCZNA_BUS_STOP_NRS);

    // Display line separator
    const int lineY = (int)(1.5 * BUS_STOP_NAME_Y_OFFSET);
    display.drawLine(X_MARGIN, lineY, display.width() - X_MARGIN, lineY, GxEPD_BLACK);

    const int y = lineY + BUS_STOP_NAME_Y_OFFSET + LINE_TEXTBOX_PADDING ;

    // Display bus stop schedule
    displayBusStopSchedule(y, schedules);
  } while (display.nextPage());
  
  delay(60000);

  clearScreenForStorage();
  display.hibernate();
}

void loop()
{
  digitalWrite(LED_INDICATOR, HIGH);
  delay(1000);
  digitalWrite(LED_INDICATOR, LOW);
  delay(1000);
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
  showStatusMessage("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20)
  {
    delay(500);
    digitalWrite(LED_INDICATOR, !digitalRead(LED_INDICATOR));
    retryCount++;
  }
  digitalWrite(LED_INDICATOR, LOW);

  if(WiFi.status() == WL_CONNECTED) {
    showStatusMessage("WiFi Connected!");
    // Optional: Display IP in a second pass if needed, or combine strings
    // showStatusMessage("IP: " + WiFi.localIP().toString());
  } else {
    showStatusMessage("WiFi Failed!");
    delay(2000);
  }
}

void syncTime()
{
  showStatusMessage("Syncing time...");

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
    showStatusMessage("Time synced!");
  } else {
    showStatusMessage("Time sync failed");
  }
  delay(500); // Short pause to let user read
}

void displayBusStopName(const String &busStopId, const String &busStopName, const std::vector<String> &busStopNrs)
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

  // displayedText += "    (ID: " + busStopId + ")";
  displayTextOnCenterHorizontally(displayedText, BUS_STOP_NAME_Y_OFFSET);
}

void displayBusStopSchedule(const int y, const std::vector<ScheduleEntry> &schedules)
{
  // Calculate minutes left for each schedule
  

  int16_t x1, y1;
  uint16_t w, h;

  int16_t yPosition = y;
  for (const auto &entry : schedules) 
  {

    if (yPosition + Y_OFFSET > display.height())
    {
      break; // Stop if we reach the bottom of the display
    }

    // Line number in bold font in black box
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(X_MARGIN, yPosition);

    display.getTextBounds(entry.line, X_MARGIN, yPosition, &x1, &y1, &w, &h);

    const int rectX = x1 - LINE_TEXTBOX_PADDING;
    const int rectY = yPosition - h - LINE_TEXTBOX_PADDING;
    const int rectWidth = w + (2 * LINE_TEXTBOX_PADDING);
    display.fillRect(rectX, rectY, rectWidth, h + (2 * LINE_TEXTBOX_PADDING), GxEPD_BLACK); // Clear previous text

    display.setTextColor(GxEPD_WHITE); // Invert text color for visibility
    display.print(entry.line);

    // Direction in regular font next to line number
    display.setTextColor(GxEPD_BLACK); // Reset text color for direction
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds(entry.line, X_MARGIN, yPosition, &x1, &y1, &w, &h);
    display.setCursor(rectX + rectWidth + (rectWidth / 4), yPosition);
    display.print(entry.direction);
    
    // Time aligned to the right in bold font
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds(entry.time, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - X_MARGIN - w, yPosition);
    display.print(entry.time);

    yPosition += Y_OFFSET;
  }
}