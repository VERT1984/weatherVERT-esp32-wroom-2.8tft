// =====================================================================
// ПОГОДНА СТАНЦІЯ — ESP32
// Плата в Arduino IDE: ESP32 Dev Module, схема розділів Huge APP (3MB No OTA)
// Ядро: arduino-esp32 3.x, ArduinoJson 7.x
// Бібліотеки: Adafruit GFX, Adafruit ILI9341, U8g2_for_Adafruit_GFX,
//             XPT2046_Touchscreen (Paul Stoffregen), Time (TimeLib),
//             WiFiManager (tzapu), ArduinoJson, QR-код ESP-IDF з ESP32 core
// У папці скетча: image_data.h, UkraineAlertMap.h, UkraineAlarmDistricts.h, WeatherNetwork.h WifiPortal.h
//                 Settings.h, WebConfig.h, NetworkSecurity.h, TlsCertificates.h, UkraineAlarmBackup.h
// =====================================================================
#include <FS.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_task_wdt.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "WifiPortal.h"
#include <qrcode.h>
#include <TimeLib.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "image_data.h"
#include "UkraineAlertMap.h"
#include "Settings.h"
#include "WebConfig.h"
#include "Notes.h"
#include "NetworkSecurity.h"
#include "WeatherNetwork.h"
#include "UkraineAlarmDistricts.h"
#include "UkraineAlarmBackup.h"

// =====================================================================
// КОНФІГУРАЦІЯ ЧАСУ ТА ПІНІВ
// =====================================================================
#define TZ_SUMMER 10800  // UTC+3
#define TZ_WINTER 7200   // UTC+2

#define TFT_CS 5
#define TFT_RST 4
#define TFT_DC 22
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_SCLK 18

#define TOUCH_CS 17

#define ALERT_POLL_MS 10000
#define ALERT_KEEPALIVE 0
#define MAP_FRESH_MS 180000UL
#define BUZZER_PIN 25
#define BUZZER_FREQ 1700
#define BUZZER_ACTIVE 0

#define BL_PIN 26
#define BL_INVERT 0

// ДОДАТКОВІ ПІДКЛЮЧЕННЯ світлодіод
#define LED_PIN 27
#define LED_G_PIN 32
#define LED_COMMON_ANODE 0
#define LDR_PIN 34
#include "Features.h"  // нічний режим 22:00–07:00, автояскравість, світлодіод

// =====================================================================
// РОЗМІРИ ТА ПОЗИЦІЇ
// =====================================================================
#define IMG_WIDTH 60
#define IMG_HEIGHT 60
#define RGB565(r, g, b) ((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3))

int bgR = 0, bgG = 0, bgB = 40;
int bgAR = 0, bgAG = 0, bgAB = 40;
int mainBgR = 0, mainBgG = 0, mainBgB = 40;
uint16_t bgColor = RGB565(0, 0, 40);
uint16_t calendarBgColor = RGB565(0, 0, 40);
uint16_t weatherBgColor = RGB565(0, 0, 40);
uint16_t clockBgColor = RGB565(0, 0, 40);
uint8_t bgTarget = 0;  // 0=загальний, 1=календар, 2=погода, 3=годинник
#define BACKGROUND_COLOR bgColor

#define TIME_Y 60
#define TEMP_X 10
#define TEMP_Y 10
// Рядки погоди ліворуч
#define WX_Y_SUN 86
#define WX_Y_PRESS 99
#define WX_Y_HUM 112
#define WX_Y_WIND 125
#define WX_Y_AQI 138

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 85
#define DAY_WIDTH (SCREEN_WIDTH / 7)
#define DAY_HEIGHT (SCREEN_HEIGHT / 5)

#define FORECAST_Y 160
#define FORECAST_ITEM_W 64

#define WIFI_CONNECT_TIMEOUT 30
#define WIFI_MANUAL_PORTAL_TIMEOUT 180
#define SWIPE_THRESHOLD 300

#define CAL_X1 155
#define CAL_Y1 70
#define CAL_X2 320
#define CAL_Y2 165

#define CLK_X1 95
#define CLK_Y1 20
#define CLK_X2 232
#define CLK_Y2 62

#define MENU_BTN_X 254
#define MENU_BTN_Y 4
#define MENU_BTN_W 62
#define MENU_BTN_H 62

#define MENU_X1 244
#define MENU_Y1 0
#define MENU_X2 320
#define MENU_Y2 70

#define WX_X1 0
#define WX_Y1 0
#define WX_X2 94
#define WX_Y2 78

#define ALERT_X1 0
#define ALERT_Y1 138
#define ALERT_X2 152
#define ALERT_Y2 159

#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800
#define TS_INVERT_X 1
#define TS_INVERT_Y 1

// =====================================================================
// КОЛЬОРИ
// =====================================================================
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define DARKGREEN 0x03E0
#define MAGENTA 0xF81F
#define ORANGE 0xFBE0
#define GREY 0x5AEB
#define CYAN 0x07FF
#define YELLOW 0xFFE0

// =====================================================================
// ОБ'ЄКТИ ТА ЗМІННІ
// =====================================================================
HTTPClient httpClient;
NetworkClientSecure weatherTlsClient;

class SntpClock {
  long offset = 0;
public:
  void begin() {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  }
  void setTimeOffset(long o) {
    offset = o;
  }
  bool valid() {
    return time(nullptr) > 1577836800L;
  }
  unsigned long getEpochTime() {
    return (unsigned long)(time(nullptr) + offset);
  }
  bool update() {
    return valid();
  }
  bool forceUpdate() {
    for (int i = 0; i < 150 && !valid(); i++) delay(100);
    return valid();
  }
};

SntpClock timeClient;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
XPT2046_Touchscreen ts(TOUCH_CS);

bool calendarFullscreen = false;
bool clockFullscreen = false;
bool clockAnalogView = true;
char cfTime[6] = "", cfSec[3] = "", cfDow[24] = "", cfDate[24] = "";
int menuState = 0;
int brightness = 100;
int shownAlertState = -1;
int calDisplayMonth = 1;
int calDisplayYear = 2026;
int calNotesPage = 0;

bool touchTracking = false;
int touchStartRawX = 0, touchCurrRawX = 0;
int touchStartRawY = 0, touchCurrRawY = 0;

char lastHours[3] = "", lastMinutes[3] = "", lastSeconds[3] = "";
char lastDayOfWeek[24] = "";
char lastTemperature[16] = "";
char lastSunrise[16] = "", lastSunset[16] = "";
char lastPressure[16] = "", lastHumidity[16] = "", lastWindSpeed[16] = "";
char lastWeatherCondition[32] = "";

int currentYear = 0;
int today = 0;

unsigned long lastWeatherUpdate = 0;
unsigned long lastCalendarUpdate = 0;
unsigned long lastNTPUpdate = 0;
unsigned long lastTouchCheck = 0;
unsigned long lastTimeDraw = 0;
unsigned long lastDSTCheck = 0;
long cachedTZ = TZ_WINTER;

void displayCalendar();
void drawMenuButton();
void drawAlertStatus();
void loadBrightness();
void applyBrightness();
void returnToMain();
void restoreAfterAlarm();
void drawSystem();
void loadBg();
void drawBgScreen();
void drawBgSlider(int ch);
void drawBgPreview();
void setBgFromTouch(int sx, int sy);
static void loadBgTarget();
static void saveBgTarget();
void drawAlertHist();
void loadAlertLog();
void drawWifiConfigScreen(const char* portalPassword, bool manual);
void drawSettingsQr();
void startAlarmScreen(int kind);
void alarmTick(unsigned long now);
bool httpGetOK(const String& url);
extern bool alarmActive;
char bootPortalPass[12] = "";

// =====================================================================
// ДОПОМІЖНІ ФУНКЦІЇ ДЛЯ ТЕКСТУ З АВТОМАТИЧНИМ ФОНОМ (ОБТІКАННЯМ)
// =====================================================================
void drawText(int x, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  int fontAscent = 12;  // Орієнтовна висота шрифту над базовою лінією
  int fontHeight = 16;
  tft.fillRect(x, y - fontAscent, tw + 2, fontHeight, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

void drawTextCentered(int cx, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  int x = cx - tw / 2;
  int fontAscent = 12;
  int fontHeight = 16;
  tft.fillRect(x - 2, y - fontAscent, tw + 4, fontHeight, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

int daysInMonth(int y, int m) {
  if (m == 2) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    return leap ? 29 : 28;
  }
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

time_t lastSundayAt1UTC(int y, int m) {
  tmElements_t tm;
  tm.Year = CalendarYrToTm(y);
  tm.Month = m;
  tm.Day = daysInMonth(y, m);
  tm.Hour = 1;
  tm.Minute = 0;
  tm.Second = 0;
  time_t t = makeTime(tm);
  while (weekday(t) != 1) t -= 86400UL;
  return t;
}

bool isUkraineSummerTime(time_t utcNow) {
  int y = year(utcNow);
  time_t springFwd = lastSundayAt1UTC(y, 3);
  time_t fallBack = lastSundayAt1UTC(y, 10);
  return (utcNow >= springFwd && utcNow < fallBack);
}

long currentTZOffset() {
  return cachedTZ;
}

void updateDSTCache() {
  time_t utcNow = (time_t)timeClient.getEpochTime() - cachedTZ;
  long newTZ = isUkraineSummerTime(utcNow) ? TZ_SUMMER : TZ_WINTER;
  if (newTZ != cachedTZ) {
    cachedTZ = newTZ;
    timeClient.setTimeOffset(newTZ);
    setTime((time_t)timeClient.getEpochTime());
    currentYear = year();
    today = day();
    Serial.printf("[DST] зміна офсету -> %lds (%s)\n", newTZ, newTZ == TZ_SUMMER ? "літо" : "зима");
  }
}

void updateNTPTime() {
  timeClient.update();
  timeClient.setTimeOffset(cachedTZ);
  time_t newEpoch = (time_t)timeClient.getEpochTime();
  if (newEpoch < (time_t)(1577836800UL + cachedTZ)) return;
  setTime(newEpoch);
  int prevYear = currentYear;
  int prevToday = today;
  currentYear = year();
  today = day();
  if (currentYear != prevYear || today != prevToday) {
    displayCalendar();
  }
}

static uint16_t iconBuf[IMG_WIDTH * IMG_HEIGHT];
void drawIcon(int x, int y, const uint16_t* img) {
  for (int i = 0; i < IMG_WIDTH * IMG_HEIGHT; i++) {
    uint16_t c = img[i];
    iconBuf[i] = (c == 0x0000) ? BACKGROUND_COLOR : c;
  }
  tft.drawRGBBitmap(x, y, iconBuf, IMG_WIDTH, IMG_HEIGHT);
}

void displayWeatherIcon(const char* cond, int x, int y, bool night = false) {
  if (strcmp(cond, "Clear") == 0) drawIcon(x, y, night ? img_150 : img_100);
  else if (strcmp(cond, "Clouds") == 0) drawIcon(x, y, night ? img_154 : img_102);
  else if (strcmp(cond, "Cloudy Day") == 0) drawIcon(x, y, night ? img_154 : img_104);
  else if (strcmp(cond, "Clear Night") == 0) drawIcon(x, y, img_150);
  else if (strcmp(cond, "Cloudy Night") == 0) drawIcon(x, y, img_154);
  else if (strcmp(cond, "Thunderstorm") == 0) drawIcon(x, y, img_303);
  else if (strcmp(cond, "Rain") == 0) drawIcon(x, y, img_306);
  else if (strcmp(cond, "Shower Rain") == 0) drawIcon(x, y, img_310);
  else if (strcmp(cond, "Hail") == 0) drawIcon(x, y, img_313);
  else if (strcmp(cond, "Snow") == 0) drawIcon(x, y, img_403);
  else if (strcmp(cond, "Drizzle") == 0) drawIcon(x, y, img_404);
  else drawIcon(x, y, img_NA);
}

int condPriority(const char* cond) {
  if (strcmp(cond, "Thunderstorm") == 0) return 7;
  if (strcmp(cond, "Snow") == 0) return 6;
  if (strcmp(cond, "Hail") == 0) return 5;
  if (strcmp(cond, "Rain") == 0) return 4;
  if (strcmp(cond, "Shower Rain") == 0) return 3;
  if (strcmp(cond, "Drizzle") == 0) return 2;
  if (strcmp(cond, "Clouds") == 0) return 1;
  return 0;
}

void drawForecastItem(int index, const char* condition, int tempMin, int tempMax, const char* dayName) {
  int x = index * FORECAST_ITEM_W + 1;
  int y = FORECAST_Y;
  tft.fillRect(x, y, IMG_WIDTH, IMG_HEIGHT + 35, BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  int tw = u8g2Fonts.getUTF8Width(dayName);
  drawText(x + (IMG_WIDTH - tw) / 2, y + 10, dayName, CYAN, BACKGROUND_COLOR);

  displayWeatherIcon(condition, x, y + 12);

  char buf[12];
  snprintf(buf, sizeof(buf), "%d/%d", tempMin, tempMax);
  tw = u8g2Fonts.getUTF8Width(buf);
  drawText(x + (IMG_WIDTH - tw) / 2, y + IMG_HEIGHT + 18, buf, GREEN, BACKGROUND_COLOR);
}

struct FcItem {
  char cond[16];
  int tmin;
  int tmax;
  char name[16];
};
FcItem fcCache[5];
int fcCount = 0;

struct HrItem {
  char cond[16];
  int temp;
  int hh;
  int mm;
  bool night;
};
HrItem hrCache[5];
int hrCount = 0;
bool hourlyScreen = false;
bool forecastGraphScreen = false;

void displayForecastItem(int index, const char* condition, int tempMin, int tempMax, const char* dayName) {
  if (index >= 0 && index < 5) {
    strlcpy(fcCache[index].cond, condition, sizeof(fcCache[index].cond));
    fcCache[index].tmin = tempMin;
    fcCache[index].tmax = tempMax;
    strlcpy(fcCache[index].name, dayName, sizeof(fcCache[index].name));
    if (index + 1 > fcCount) fcCount = index + 1;
  }
  drawForecastItem(index, condition, tempMin, tempMax, dayName);
}

void displayCalendar() {
  if (currentYear < 2020) return;
  tft.fillRect(160, 80, 160, 80, BACKGROUND_COLOR);

  int m = month();
  int dim = daysInMonth(currentYear, m);
  tmElements_t tm;
  tm.Year = CalendarYrToTm(currentYear);
  tm.Month = m;
  tm.Day = 1;
  tm.Hour = 0;
  tm.Minute = 0;
  tm.Second = 0;

  int fdow = weekday(makeTime(tm));
  fdow = (fdow == 1) ? 7 : fdow - 1;

  int x = 160 + ((fdow - 1) * DAY_WIDTH);
  int y = 85;

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);

  for (int d = 1; d <= dim; d++) {
    bool isToday = (d == today && m == month() && currentYear == year());
    int col = (fdow - 1 + d - 1) % 7;
    uint16_t fColor = isToday ? GREEN : (col == 6 ? RED : WHITE);

    char dStr[4];
    itoa(d, dStr, 10);
    drawText(x, y, dStr, fColor, BACKGROUND_COLOR);

    x += DAY_WIDTH;
    if ((d + fdow - 1) % 7 == 0) {
      x = 160;
      y += DAY_HEIGHT;
    }
  }
}

void displayCalendarFullscreen(int displayMonth, int displayYear) {
  bgColor = calendarBgColor;
  tft.fillScreen(BACKGROUND_COLOR);
  int dim = daysInMonth(displayYear, displayMonth);
  const char* monthNames[] = {
    "Сiчень", "Лютий", "Березень", "Квiтень", "Травень", "Червень",
    "Липень", "Серпень", "Вересень", "Жовтень", "Листопад", "Грудень"
  };

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  String lbl = String(monthNames[displayMonth - 1]) + " " + String(displayYear);
  int monthNoteCount = notesCountForCalendarMonth(displayMonth, displayYear);
  int monthNotePages = (monthNoteCount + 2) / 3;
  if (monthNotePages > 1) {
    lbl += " [";
    lbl += String(calNotesPage + 1);
    lbl += "/";
    lbl += String(monthNotePages);
    lbl += "]";
  }
  drawTextCentered(160, 20, lbl.c_str(), CYAN, BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawText(4, 8, "<", GREY, BACKGROUND_COLOR);
  drawText(306, 8, ">", GREY, BACKGROUND_COLOR);

  const char* dayHdrs[] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Нд" };
  const int cellW = 320 / 7;
  const int cellH = 34;
  const int weekGap = 26;
  const int startY = 34;

  for (int i = 0; i < 7; i++) {
    drawText(i * cellW + 4, startY + 10, dayHdrs[i], i >= 5 ? RED : DARKGREEN, BACKGROUND_COLOR);
  }
  tft.drawFastHLine(0, startY + 14, 320, GREY);

  tmElements_t tm;
  tm.Year = CalendarYrToTm(displayYear);
  tm.Month = displayMonth;
  tm.Day = 1;
  tm.Hour = 0;
  tm.Minute = 0;
  tm.Second = 0;

  int firstWday = weekday(makeTime(tm));
  int startCol = (firstWday == 1) ? 6 : firstWday - 2;
  int x = startCol * cellW;
  int y = startY + cellH;

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  for (int d = 1; d <= dim; d++) {
    bool isToday = (d == today && displayMonth == month() && displayYear == year());
    uint16_t fColor;
    if (isToday) fColor = GREEN;
    else {
      int col = (startCol + d - 1) % 7;
      fColor = (col >= 5 ? RED : WHITE);
    }
    int numberX = (d < 10) ? x + 14 : x + 5;
    char dStr[4];
    itoa(d, dStr, 10);
    drawText(numberX, y, dStr, fColor, BACKGROUND_COLOR);

    x += cellW;
    if ((d + startCol) % 7 == 0) {
      x = 0;
      y += weekGap;
    }
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 232, "< свайп > мiсяць    тап  назад", GREY, BACKGROUND_COLOR);
    notesDrawCalendar(displayMonth, displayYear, calNotesPage);
}

bool httpGetOK(const String& url) {
  return weatherHttpsGet(httpClient, weatherTlsClient, url, timeClient.valid());
}

void showNoInternet() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 110, "Пiдключiть до iнтернету", RED, BACKGROUND_COLOR);
}

static void drawBootConnectingDots(uint8_t dots) {
  static const char* frames[4] = {"CONNECTING", "CONNECTING.", "CONNECTING..", "CONNECTING..."};
  tft.fillRect(48, 145, 224, 36, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 170, frames[dots & 3], CYAN, BACKGROUND_COLOR);
}

// Кожен звук — пара {тривалість мс, частота Гц}; частота 0 означає паузу.
const uint16_t BEEP_ALERT[] = {
  150, BUZZER_FREQ, 150, 0, 150, BUZZER_FREQ, 150, 0, 150, BUZZER_FREQ, 450, 0,
  450, BUZZER_FREQ, 150, 0, 450, BUZZER_FREQ, 150, 0, 450, BUZZER_FREQ, 450, 0,
  150, BUZZER_FREQ, 150, 0, 150, BUZZER_FREQ, 150, 0, 150, BUZZER_FREQ, 0, 0
};
const uint16_t BEEP_CLEAR[] = {1500, BUZZER_FREQ, 0, 0};
// Короткий висхідний сигнал підключення: чотири тони з короткими паузами.
const uint16_t BEEP_WIFI_CONNECTED[] = {85, 660, 65, 0, 85, 880, 65, 0, 100, 1175, 65, 0, 150, 1568, 0, 0};
const uint16_t BEEP_NOTE_REMINDER[] = {180, 784, 100, 0, 180, 988, 100, 0, 220, 1175, 100, 0, 350, 1568, 0, 0};
const uint16_t* beepPat = nullptr;
int beepIdx = 0;
unsigned long beepNext = 0;
volatile bool wifiConnectedBeepPending = false;
volatile bool wifiRememberGoodPending = false;
volatile bool notesBeepRequest = false;
volatile uint8_t notesBeepRepeatsRequest = 1;
uint8_t beepRepeatLeft = 1;
bool minuteSilenceActive = false, minuteSilencePulse = false;
unsigned long minuteSilenceEndsAt = 0, minuteSilenceNextAt = 0, minuteSilencePulseEndsAt = 0;
uint32_t minuteSilenceDayKey = 0;
volatile int alertState = 0;
volatile int alertEvent = 0;
volatile int alertLastCode = 0;
char compactAlertStatuses[UA_MAP_REGION_COUNT + 1] = "";
char mapDrawnStatuses[UA_MAP_REGION_COUNT + 1] = "";
bool mapDrawnFresh = false;
volatile unsigned long alertLastOkMs = 0;
volatile unsigned long cityLastOkMs = 0;
volatile unsigned long cityLastOkMsBySlot[2] = { 0, 0 };
volatile int alertErrCount = 0;
volatile int alertLastErr = 0;
volatile unsigned long backupLastOkMs = 0;
volatile int backupErrCount = 0;
volatile int backupLastCode = 0;
volatile bool backupUsingSlot[2] = { false, false };
TaskHandle_t alertTaskHandle = NULL;
volatile bool wifiPortalActive = false;
unsigned long wifiPortalStartedMs = 0;

int sysScroll = 0;
int sysLastSy = -1;
unsigned long lastSysDraw = 0;
bool sysForce = true;
unsigned long sysBuiltMs = 0;

#define ALERT_LOG_MAX 20
struct AlertEv {
  uint32_t t;
  uint8_t lvl;
};
AlertEv alertLog[2][ALERT_LOG_MAX];
int alertLogCount[2] = { 0, 0 };
volatile int alertCurLevel[2] = { -1, -1 };
portMUX_TYPE alertLogMux = portMUX_INITIALIZER_UNLOCKED;
bool alertHistScreen = false;

unsigned long loopCountTmp = 0, loopRate = 0, loopMaxTmp = 0, loopMaxGap = 0;
unsigned long lastRateMs = 0, prevLoopMs = 0;
char lastRestartWhy[64] = "";
int wifiDrops = 0;
volatile uint8_t lastWifiDiscReason = 0;
char lastWifiEvent[56] = "-";

const char* wifiDiscWhy(uint8_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_EXPIRE: return "тайм-аут автентифiкацiї / причина AP";
    case WIFI_REASON_AUTH_LEAVE: return "AP надiслав вiд'єднання";
    case WIFI_REASON_ASSOC_EXPIRE: return "сесiя прострочена";
    case WIFI_REASON_ASSOC_LEAVE: return "штатний disconnect";
    case WIFI_REASON_AUTH_FAIL: return "пароль / auth";
    case WIFI_REASON_NO_AP_FOUND: return "AP не знайдений пiд час connect";
    case WIFI_REASON_BEACON_TIMEOUT: return "немає маякiв AP (сигнал/живлення)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL: return "не вдалось з'єднатись";
    default: return "див. код";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t r = info.wifi_sta_disconnected.reason;
    lastWifiDiscReason = r;
    snprintf(lastWifiEvent, sizeof(lastWifiEvent), "%u %s", r, wifiDiscWhy(r));
    Serial.printf("[WiFi] DISCONNECT reason=%u (%s) RSSI=недоступний heap=%u\n",
                  r, wifiDiscWhy(r), (unsigned)ESP.getFreeHeap());
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("[WiFi] GOT_IP %s RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiConnectedBeepPending = true;
    wifiRememberGoodPending = true;
  }
}

void buzzOn(uint16_t frequencyHz) {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, HIGH);
  else { ledcWriteTone(BUZZER_PIN, frequencyHz); ledcWrite(BUZZER_PIN, 128); }
}

void buzzOff() {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, LOW);
  else { ledcWriteTone(BUZZER_PIN, 0); ledcWrite(BUZZER_PIN, 0); }
}

void startBeep(const uint16_t* pat, uint8_t repeats) {
  beepPat = pat;
  beepIdx = 0;
  beepNext = 0;
  beepRepeatLeft = constrain(repeats, 1, 3);
}

void beepTick() {
  if (!beepPat) return;
  unsigned long n = millis();
  if (n < beepNext) return;
  uint16_t durationMs = beepPat[beepIdx * 2];
  uint16_t frequencyHz = beepPat[beepIdx * 2 + 1];
  if (durationMs == 0) {
    buzzOff();
    if (beepRepeatLeft > 1) {
      beepRepeatLeft--;
      beepIdx = 0;
      beepNext = n + 250UL;
      return;
    }
    beepPat = nullptr;
    beepRepeatLeft = 0;
    return;
  }
  if (frequencyHz == 0) buzzOff();
  else buzzOn(frequencyHz);
  beepNext = n + durationMs;
  beepIdx++;
}

void saveAlertLog() {
  File f = LittleFS.open("/alerts.bin", "w");
  if (!f) return;
  AlertEv logCopy[2][ALERT_LOG_MAX];
  int cnt[2];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(logCopy, alertLog, sizeof(alertLog));
  cnt[0] = alertLogCount[0];
  cnt[1] = alertLogCount[1];
  portEXIT_CRITICAL(&alertLogMux);
  f.write((uint8_t*)cnt, sizeof(cnt));
  f.write((uint8_t*)logCopy, sizeof(logCopy));
  f.close();
}

void loadAlertLog() {
  File f = LittleFS.open("/alerts.bin", "r");
  if (!f) return;
  if (f.size() == sizeof(int) * 2 + sizeof(alertLog)) {
    int cnt[2];
    f.read((uint8_t*)cnt, sizeof(cnt));
    f.read((uint8_t*)alertLog, sizeof(alertLog));
    for (int r = 0; r < 2; r++) alertLogCount[r] = constrain(cnt[r], 0, ALERT_LOG_MAX);
  }
  f.close();
}

void addAlertEvent(int r, uint32_t t, uint8_t lvl) {
  portENTER_CRITICAL(&alertLogMux);
  if (alertLogCount[r] >= ALERT_LOG_MAX) {
    memmove(&alertLog[r][0], &alertLog[r][1], sizeof(AlertEv) * (ALERT_LOG_MAX - 1));
    alertLogCount[r] = ALERT_LOG_MAX - 1;
  }
  alertLog[r][alertLogCount[r]].t = t;
  alertLog[r][alertLogCount[r]].lvl = lvl;
  alertLogCount[r]++;
  portEXIT_CRITICAL(&alertLogMux);
  saveAlertLog();
}

int mapCharLevel(char c) {
  if (c == 'R' || c == 'A') return 2;
  if (c == 'Y' || c == 'P' || c == '?') return 1;
  if (c == 'N') return 0;
  return -1;
}

int slotLevelFromMap(const char* st, uint8_t kind, int idx) {
  if (kind == 2 && idx >= 0 && idx < UA_MAP_REGION_COUNT) return mapCharLevel(st[idx]);
  if (kind == 1 && idx >= 0 && idx < UA_MAP_STATE_COUNT) {
    int best = -1;
    for (int i = 0; i < UA_MAP_REGION_COUNT; i++) {
      if (pgm_read_byte(&uaMapRegionState[i]) != idx) continue;
      int l = mapCharLevel(st[i]);
      if (l > best) best = l;
    }
    return best;
  }
  return -1;
}

void alertTask(void* arg) {
  NetworkClientSecure client;
  configureTrustedTls(client);
  HTTPClient http;
  HTTPClient backupHttp;

  int cycle = 0, cityTurn = 0;
  int prevCity = 0;
  bool firstCity = true;
  int prevLvl[2] = { -1, -1 };
  int fails[3] = { 0, 0, 0 };
  uint8_t sigKind[2] = { 255, 255 };
  int16_t sigIdx[2] = { -1, -1 };
  uint32_t sigUid[2] = { 0, 0 };
  bool sigInit = false;

  auto logChange = [&](int r, int lvl) {
    if (!timeClient.valid()) return;
    uint32_t nowUtc = (uint32_t)time(nullptr);
    int last = (alertLogCount[r] > 0) ? alertLog[r][alertLogCount[r] - 1].lvl : -1;
    if (prevLvl[r] == -1) prevLvl[r] = last;
    if (prevLvl[r] == -1) {
      if (lvl > 0) addAlertEvent(r, nowUtc, lvl);
    } else if (lvl != prevLvl[r]) {
      addAlertEvent(r, nowUtc, lvl);
    }
    prevLvl[r] = lvl;
  };

  auto applyLevel = [&](int slot, int level, bool fromBackup) {
    backupUsingSlot[slot] = fromBackup;
    alertCurLevel[slot] = level;
    if (level < 0) {
      if (slot == 0) alertState = 0;
      return;
    }
    logChange(slot, level);
    if (slot == 0) {
      if (!firstCity && level > prevCity) alertEvent = 1;
      else if (!firstCity && level == 0 && prevCity > 0) alertEvent = 2;
      firstCity = false;
      prevCity = level;
      alertState = (level == 2) ? 2 : (level == 1) ? 3
                                                   : 1;
    }
  };

  for (;;) {
    if (wifiPortalActive || menuState == 7 || webConfigBusy()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    AppCfg c;
    portENTER_CRITICAL(&cfgMux);
    memcpy(&c, &cfg, sizeof(c));
    portEXIT_CRITICAL(&cfgMux);

    for (int s = 0; s < 2; s++) {
      bool changed = (sigKind[s] != c.locKind[s] || sigIdx[s] != c.locIdx[s] || sigUid[s] != c.cityUid[s]);
      if (!changed) continue;
      sigKind[s] = c.locKind[s];
      sigIdx[s] = c.locIdx[s];
      sigUid[s] = c.cityUid[s];
      if (!sigInit) continue;
      prevLvl[s] = -1;
      portENTER_CRITICAL(&alertLogMux);
      alertLogCount[s] = 0;
      portEXIT_CRITICAL(&alertLogMux);
      saveAlertLog();
      alertCurLevel[s] = -1;
      if (s == 0) {
        firstCity = true;
        prevCity = 0;
        alertState = 0;
      }
    }
    sigInit = true;

    if ((!c.alertsToken[0] && !c.ukAlarmToken[0]) || (!c.locKind[0] && !c.locKind[1])) {
      alertState = 0;
      alertCurLevel[0] = -1;
      alertCurLevel[1] = -1;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    for (int s = 0; s < 2; s++)
      if (!c.locKind[s]) alertCurLevel[s] = -1;

    bool city0 = c.locKind[0] && c.cityUid[0];
    bool city1 = c.locKind[1] && c.cityUid[1];
    int n = cycle++;
    bool mapJob = (n % 3 == 1) || !(city0 || city1);
    int slot = 0;
    if (!mapJob) {
      if (city0 && city1) slot = (cityTurn++ & 1);
      else slot = city0 ? 0 : 1;
    }
    bool primaryOk = false;
    const bool netReady = (WiFi.status() == WL_CONNECTED) && timeClient.valid();

    // 1. основне джерело (alerts.in.ua)
    if (netReady && c.alertsToken[0] && httpsLock(8000)) {
      String url = mapJob ? String("https://api.alerts.in.ua/v1/alerts/active.json")
                          : String("https://api.alerts.in.ua/v2/iot/active_air_raid_alerts/") + String((unsigned long)c.cityUid[slot]) + ".json";
      http.setReuse(false);
      http.begin(client, url);
      http.useHTTP10(true);
      http.addHeader("Authorization", String("Bearer ") + c.alertsToken);
      http.setTimeout(mapJob ? 8000 : 4000);
      http.setConnectTimeout(3000);

      int code = tlsHeapAvailable(mapJob ? "alerts.in.ua map" : "alerts.in.ua city") ? http.GET() : -1000;
      alertLastCode = code;
      if (code != 200) {
        alertErrCount++;
        alertLastErr = code;
      }

      if (code == 200 && mapJob) {
        char next[UA_MAP_REGION_COUNT + 1];
        if (uaParseActiveAlerts(http, next, sizeof(next))) {
          portENTER_CRITICAL(&alertLogMux);
          memcpy(compactAlertStatuses, next, sizeof(next));
          portEXIT_CRITICAL(&alertLogMux);

          for (int s = 0; s < 2; s++) {
            if (!c.locKind[s] || c.cityUid[s]) continue;
            applyLevel(s, slotLevelFromMap(next, c.locKind[s], c.locIdx[s]), false);
          }
          alertLastOkMs = millis();
          primaryOk = true;
        }
      } else if (code == 200 && !mapJob) {
        String st = http.getString();
        st.trim();
        st.replace("\"", "");
        char ch = st.length() ? st.charAt(0) : '?';
        int level = -1;
        if (ch == 'N') level = 0;
        else if (ch == 'Y' || ch == 'P') level = 1;
        else if (ch == 'R') level = 2;

        if (level >= 0) {
          applyLevel(slot, level, false);
          cityLastOkMs = millis();
          cityLastOkMsBySlot[slot] = cityLastOkMs;
          primaryOk = true;
        }
      }
      tlsClose(client, http);
      httpsUnlock();
    }

    // 2. Резерв: якщо alerts.in.ua не відповів. Окремий TLS-сеанс на інший хост.
    if (!primaryOk && c.ukAlarmToken[0] && netReady && httpsLock(8000)) {
      const char* backupNames[2] = { nullptr, nullptr };
      for (int s = 0; s < 2; s++) {
        if (!c.locKind[s]) continue;
        if (c.locKind[s] == 1 && c.locIdx[s] >= 0 && c.locIdx[s] < UA_MAP_STATE_COUNT)
          backupNames[s] = uaMapStateNames[c.locIdx[s]];
        else if (c.locKind[s] == 2 && c.locIdx[s] >= 0 && c.locIdx[s] < UA_MAP_REGION_COUNT)
          backupNames[s] = uaMapRegionNames[c.locIdx[s]];
      }

      backupHttp.setReuse(false);
      backupHttp.begin(client, "https://api.ukrainealarm.com/api/v3/alerts");
      backupHttp.setTimeout(6000);
      backupHttp.setConnectTimeout(3000);
      backupHttp.addHeader("Authorization", c.ukAlarmToken);

      int backupCode = tlsHeapAvailable("UkraineAlarm") ? backupHttp.GET() : -1000;
      backupLastCode = backupCode;

      if (backupCode == 200) {
        int backupLevels[2] = { -1, -1 };
        if (uaBackupParseActiveAlerts(backupHttp, backupNames[0], c.locKind[0],
                                      backupNames[1], c.locKind[1], backupLevels)) {
          unsigned long okMs = millis();
          for (int s = 0; s < 2; s++) {
            if (!backupNames[s]) continue;
            applyLevel(s, backupLevels[s], true);
            cityLastOkMsBySlot[s] = okMs;
          }
          cityLastOkMs = okMs;
          backupLastOkMs = okMs;
        }
      } else {
        backupErrCount++;
      }
      tlsClose(client, backupHttp);
      httpsUnlock();
    }

    vTaskDelay(pdMS_TO_TICKS(ALERT_POLL_MS));
  }
}

const char* shortDayName(int wday) {
  const char* names[] = { "Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
  return names[wday];
}

void restartWithReason(const char* why) {
  File f = LittleFS.open("/restart.txt", "w");
  if (f) {
    f.println(why);
    f.close();
  }
  delay(100);
  ESP.restart();
}

void loadRestartReason() {
  File f = LittleFS.open("/restart.txt", "r");
  if (f) {
    String tmp = f.readStringUntil('\n');
    tmp.trim();
    strncpy(lastRestartWhy, tmp.c_str(), sizeof(lastRestartWhy) - 1);
    lastRestartWhy[sizeof(lastRestartWhy) - 1] = '\0';
    f.close();
    LittleFS.remove("/restart.txt");
  }
}

void rawToScreen(int rawX, int rawY, int& sx, int& sy) {
  sx = map(rawX, TS_MINX, TS_MAXX, 0, 320);
  sy = map(rawY, TS_MINY, TS_MAXY, 0, 240);
  if (TS_INVERT_X) sx = 319 - sx;
  if (TS_INVERT_Y) sy = 239 - sy;
}

bool inRect(int x, int y, int x1, int y1, int x2, int y2) {
  return x >= x1 && x <= x2 && y >= y1 && y <= y2;
}

void drawGear(int cx, int cy, uint16_t col, uint16_t bg) {
  tft.fillCircle(cx, cy, 11, col);
  for (int i = 0; i < 12; i++) {
    float a = i * TWO_PI / 12.0f;
    float ca = cos(a), sa = sin(a);
    float px = -sa * 2.5f, py = ca * 2.5f;
    int x1 = lroundf(cx + ca * 9 + px), y1 = lroundf(cy + sa * 9 + py);
    int x2 = lroundf(cx + ca * 9 - px), y2 = lroundf(cy + sa * 9 - py);
    int x3 = lroundf(cx + ca * 16 - px), y3 = lroundf(cy + sa * 16 - py);
    int x4 = lroundf(cx + ca * 16 + px), y4 = lroundf(cy + sa * 16 + py);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, col);
    tft.fillTriangle(x1, y1, x3, y3, x4, y4, col);
  }
  tft.fillCircle(cx, cy, 6, bg);
  tft.drawCircle(cx, cy, 6, col);
}

static const uint16_t MENU_CELLS[9] = {
  0xF548, 0xEB29, 0xF548,
  0xEB29, 0xF548, 0x5E5A,
  0xF548, 0xFF7D, 0xEB29
};

void drawMenuButton() {
  const int X = MENU_BTN_X;
  const int Y = MENU_BTN_Y;
  const int W = MENU_BTN_W;
  const int H = MENU_BTN_H;

  // Плитка з рамкою і тінню
  tft.fillRoundRect(X, Y, W, H, 12, RGB565(22, 27, 42));
  tft.drawRoundRect(X, Y, W, H, 12, RGB565(110, 122, 150));
  tft.drawRoundRect(X + 1, Y + 1, W - 2, H - 2, 11, RGB565(52, 60, 82));

  // Сітка 3×3 із кольорових квадратиків
  const int cell = 15;
  const int gap = 4;
  const int pad = (W - (3 * cell + 2 * gap)) / 2;

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int x = X + pad + col * (cell + gap);
      int y = Y + pad + row * (cell + gap);
      int index = row * 3 + col;

      tft.fillRoundRect(x, y, cell, cell, 3, MENU_CELLS[index]);
    }
  }
}

void animateMenuButton(unsigned long now) {
  static const uint8_t path[9] = { 0, 1, 2, 5, 8, 7, 6, 3, 4 };
  static unsigned long lastFrame = 0;
  static int previous = -1;
  static int step = 0;

  if (now - lastFrame < 220UL) return;
  lastFrame = now;

  const int cell = 15;
  const int gap = 4;
  const int pad = (MENU_BTN_W - (3 * cell + 2 * gap)) / 2;

  // Повертаємо попередній квадратик до його звичайного кольору.
  if (previous >= 0) {
    int row = previous / 3;
    int col = previous % 3;
    int x = MENU_BTN_X + pad + col * (cell + gap);
    int y = MENU_BTN_Y + pad + row * (cell + gap);

    tft.fillRoundRect(x, y, cell, cell, 3, MENU_CELLS[previous]);
  }

  // Білий контур — короткий «проблиск» на наступному квадратику.
  int current = path[step];
  int row = current / 3;
  int col = current % 3;
  int x = MENU_BTN_X + pad + col * (cell + gap);
  int y = MENU_BTN_Y + pad + row * (cell + gap);

  tft.drawRoundRect(x, y, cell, cell, 3, WHITE);

  previous = current;
  step = (step + 1) % 9;
}

void drawAlertStatus() {
  int st = alertState;
  shownAlertState = st;
  uint16_t bg, fg;
  const char* txt;
  if (st == 2) {
    bg = RED;
    fg = WHITE;
    txt = "ТРИВОГА";
  } else if (st == 3) {
    bg = YELLOW;
    fg = BLACK;
    txt = "ТРИВОГА";
  } else if (st == 1) {
    bg = DARKGREEN;
    fg = WHITE;
    txt = "ВСЕ СПОКIЙНО";
  } else {
    bg = GREY;
    fg = WHITE;
    txt = "ХЗ";
  }
  tft.fillRoundRect(10, 142, 140, 16, 4, bg);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  int tw = u8g2Fonts.getUTF8Width(txt);
  drawText(10 + (140 - tw) / 2, 154, txt, fg, bg);
}

// у Features.h (нічний режим + автояскравість)

void loadBrightness() {
  int b0 = brightness, r = bgR, g = bgG, b = bgB;
  cfgLoadUi(b0, r, g, b);
  brightness = b0;
}

void saveBrightness() {
  cfgSaveBrightness(brightness);
}

// ---------------------------------------------------------------------
// ГЕНЕРАТОР QR-КОДІВ (ESP-IDF 5.x / Arduino 3.x)
// ---------------------------------------------------------------------
static int qrDrawX = 0, qrDrawY = 0, qrDrawScale = 6;

static void drawQrModules(esp_qrcode_handle_t qr) {
  const int quiet = 2;
  const int size = esp_qrcode_get_size(qr);
  const int total = (size + quiet * 2) * qrDrawScale;
  tft.fillRect(qrDrawX, qrDrawY, total, total, WHITE);
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (esp_qrcode_get_module(qr, x, y)) {
        tft.fillRect(qrDrawX + (x + quiet) * qrDrawScale,
                     qrDrawY + (y + quiet) * qrDrawScale,
                     qrDrawScale, qrDrawScale, BLACK);
      }
    }
  }
}

void drawQr(const char* text, int x0, int y0, int scale) {
  qrDrawX = x0;
  qrDrawY = y0;
  qrDrawScale = scale;

  esp_qrcode_config_t config;
  memset(&config, 0, sizeof(config));
  config.display_func = drawQrModules;
  config.max_qrcode_version = 4;
  config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;

  if (esp_qrcode_generate(&config, text) != ESP_OK) {
    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawText(x0, y0 + 20, "QR: текст задовгий", RED, BACKGROUND_COLOR);
  }
}

void drawSettingsQr() {
  webStart();
  tft.fillScreen(BACKGROUND_COLOR);
  if (WiFi.status() != WL_CONNECTED) {
    u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
    drawTextCentered(160, 100, "Немає Wi-Fi", RED, BACKGROUND_COLOR);
  } else {
    char url[64];
    snprintf(url, sizeof(url), "http://%s/?pin=%s", WiFi.localIP().toString().c_str(), cfg.pin);
    drawQr(url, 6, 9, 6);

    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawText(236, 26, "Налаштування", CYAN, BACKGROUND_COLOR);
    drawText(236, 56, "Наведи камеру", WHITE, BACKGROUND_COLOR);
    drawText(236, 72, "телефона ", WHITE, BACKGROUND_COLOR);
    drawText(236, 88, "QR-код", WHITE, BACKGROUND_COLOR);

    String pinStr = String("PIN ") + cfg.pin;
    drawText(236, 128, pinStr.c_str(), YELLOW, BACKGROUND_COLOR);

    drawText(236, 156, "Телефон і ", GREY, BACKGROUND_COLOR);
    drawText(236, 172, "станція в одній", GREY, BACKGROUND_COLOR);
    drawText(236, 188, "Wi-Fi мережі", GREY, BACKGROUND_COLOR);
  }

  tft.fillRoundRect(236, 198, 80, 38, 8, DARKGREEN);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(276, 223, "Назад", WHITE, DARKGREEN);
}

void minuteSilenceOff() {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, LOW);
  else { ledcWriteTone(BUZZER_PIN, 0); ledcWrite(BUZZER_PIN, 0); }
  minuteSilencePulse = false;
}

void minuteSilenceTick(unsigned long now) {
  if (!cfg.minuteSilence) {
    if (minuteSilencePulse && !alarmActive && !beepPat && !alertEvent) minuteSilenceOff();
    else minuteSilencePulse = false;
    minuteSilenceActive = false;
    return;
  }
  if (!minuteSilenceActive && timeClient.valid() && hour() == 9 && minute() == 0) {
    uint32_t dayKey = (uint32_t)year() * 10000UL + (uint32_t)month() * 100UL + (uint32_t)day();
    if (dayKey != minuteSilenceDayKey) {
      minuteSilenceDayKey = dayKey;
      minuteSilenceActive = true;
      minuteSilenceEndsAt = now + 60000UL;
      minuteSilenceNextAt = now;
    }
  }
  if (!minuteSilenceActive) return;
  if (alarmActive || beepPat || alertEvent) {
    if (minuteSilencePulse) minuteSilenceOff();
    minuteSilenceNextAt = now + 1000UL;
    return;
  }
  if ((long)(now - minuteSilenceEndsAt) >= 0) {
    minuteSilenceActive = false;
    minuteSilenceOff();
    return;
  }
  if (minuteSilencePulse && (long)(now - minuteSilencePulseEndsAt) >= 0) minuteSilenceOff();
  if ((long)(now - minuteSilenceNextAt) >= 0) {
    minuteSilenceNextAt = now + 1000UL;
    if (!alarmActive && !beepPat && !alertEvent) {
      if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, HIGH);
      else { ledcWriteTone(BUZZER_PIN, 900); ledcWrite(BUZZER_PIN, 24); }
      minuteSilencePulse = true;
      minuteSilencePulseEndsAt = now + 70UL;
    }
  }
}

bool alarmActive = false;
int alarmKind = 0;
bool alarmDisplayYellow = false;
unsigned long alarmStartMs = 0;

void drawWrappedCentered(const char* text, int cx, int y, int lineH, int maxW, uint16_t fg, uint16_t bg) {
  char line[64] = "";
  char word[40];
  const char* p = text;
  while (*p) {
    int n = 0;
    while (*p && *p != ' ' && n < 39) word[n++] = *p++;
    word[n] = '\0';
    while (*p == ' ') p++;
    char trial[110];
    snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && u8g2Fonts.getUTF8Width(trial) > maxW) {
      drawTextCentered(cx, y, line, fg, bg);
      y += lineH;
      strlcpy(line, word, sizeof(line));
    } else {
      strlcpy(line, trial, sizeof(line));
    }
  }
  if (line[0]) drawTextCentered(cx, y, line, fg, bg);
}

void drawAlarmFrame(bool inv) {
  uint16_t activeBg = alarmDisplayYellow ? YELLOW : RED;
  uint16_t bg = (alarmKind == 1) ? (inv ? BLACK : activeBg) : DARKGREEN;
  uint16_t fg = (alarmKind == 1) ? (inv ? activeBg : (alarmDisplayYellow ? BLACK : WHITE)) : WHITE;
  tft.fillScreen(bg);

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  if (cfg.locTitle[0][0]) drawTextCentered(160, 28, cfg.locTitle[0], fg, bg);

  u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
  if (alarmKind == 1) drawWrappedCentered(cfg.alarmText, 160, 110, 40, 300, fg, bg);
  else drawWrappedCentered("ВIДБIЙ", 160, 125, 40, 300, fg, bg);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 230, "торкнiться - закрити", fg, bg);
}

void startAlarmScreen(int kind) {
  alarmKind = kind;
  alarmDisplayYellow = (kind == 1 && alertState == 3);
  alarmActive = true;
  alarmStartMs = millis();
  ledcWrite(BL_PIN, BL_INVERT ? 0 : 255);
  drawAlarmFrame(false);
}

#define ALARM_DURATION 5000UL

void alarmTick(unsigned long now) {
  if (alarmKind == 1 && (alertState == 2 || alertState == 3)) {
    bool yellow = (alertState == 3);
    if (yellow != alarmDisplayYellow) {
      alarmDisplayYellow = yellow;
      drawAlarmFrame(false);  // екран і світлодіод змінюють рівень синхронно
    }
  }
  if (now - alarmStartMs >= ALARM_DURATION) {
    alarmActive = false;
    applyBrightness();
    restoreAfterAlarm();
  }
}

void decodeUkraineMapRow(int y, uint8_t* row) {
  memset(row, 255, UA_MAP_W);
  if (y < 0 || y >= UA_MAP_H) return;

  uint16_t pos = pgm_read_word(&uaMapRowOffsets[y]);
  uint16_t end = pgm_read_word(&uaMapRowOffsets[y + 1]);
  int x = 0;
  while (pos < end && x < UA_MAP_W) {
    uint8_t len = pgm_read_byte(&uaMapRle[pos++]);
    uint8_t region = pgm_read_byte(&uaMapRle[pos++]);
    for (int i = 0; i < len && x < UA_MAP_W; i++) row[x++] = region;
  }
}

inline void classifyMapBoundary(uint8_t id, uint8_t neighbor, uint8_t state,
                                bool& districtEdge, bool& oblastEdge, bool& outline) {
  if (neighbor == id) return;
  if (neighbor == 255 || neighbor >= UA_MAP_REGION_COUNT) {
    outline = true;
    return;
  }
  const uint8_t neighborState = pgm_read_byte(&uaMapRegionState[neighbor]);
  if (state != neighborState) oblastEdge = true;
  else if (id > neighbor) districtEdge = true;
}

// ----- Мапа тривог:
#define MAP_SRC_X0 24  // де в початковій картинці починається країна
#define MAP_SRC_Y0 2
#define MAP_SRC_W 272
#define MAP_SRC_H 185
#define MAP_DST_W 310  // розмір на екрані
#define MAP_DST_H 211
#define MAP_DST_X0 5
#define MAP_DST_Y0 8
#define MAP_BG RGB565(14, 18, 26)

static uint8_t mapSrcRow[UA_MAP_W];
static int mapSrcRowY = -1;

// id областей для кожного пікселя рядка
void decodeScaledMapRow(int dy, uint8_t* out) {
  if (dy < 0 || dy >= MAP_DST_H) {
    memset(out, 255, MAP_DST_W);
    return;
  }
  int sy = MAP_SRC_Y0 + (dy * MAP_SRC_H) / MAP_DST_H;
  if (sy != mapSrcRowY) {
    decodeUkraineMapRow(sy, mapSrcRow);
    mapSrcRowY = sy;
  }
  for (int dx = 0; dx < MAP_DST_W; dx++) out[dx] = mapSrcRow[MAP_SRC_X0 + (dx * MAP_SRC_W) / MAP_DST_W];
}

char mapClockDrawn[9] = "";
void drawMapClock(bool force) {
  if (!timeClient.valid()) return;
  time_t t = (time_t)timeClient.getEpochTime();
  tm* p = gmtime(&t);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", p->tm_hour, p->tm_min, p->tm_sec);
  if (!force && strcmp(buf, mapClockDrawn) == 0) return;
  tft.fillRect(0, 0, 92, 19, MAP_BG);
  u8g2Fonts.setFont(u8g2_font_logisoso16_tn);
  u8g2Fonts.setBackgroundColor(MAP_BG);
  u8g2Fonts.setForegroundColor(WHITE);
  u8g2Fonts.setCursor(4, 17);
  u8g2Fonts.print(buf);
  strcpy(mapClockDrawn, buf);
}

// Скільки секунд тому оновились дані (правий нижній кут): білий — свіжі, помаранчевий — застарілі
void drawMapAge() {
  unsigned long lastOk = alertLastOkMs;
  char buf[32];
  uint16_t col = WHITE;
  if (!lastOk) {
    strcpy(buf, "немає даних");
    col = RGB565(150, 155, 165);
  } else {
    unsigned long secs = (millis() - lastOk) / 1000UL;
    snprintf(buf, sizeof(buf), "оновл. %lu с тому", secs);
    if (secs * 1000UL > MAP_FRESH_MS) col = ORANGE;
  }
  tft.fillRect(198, 223, 122, 17, MAP_BG);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setBackgroundColor(MAP_BG);
  u8g2Fonts.setForegroundColor(col);
  u8g2Fonts.setCursor(204, 236);
  u8g2Fonts.print(buf);
}

void drawAlertMap(bool full) {
  char statuses[UA_MAP_REGION_COUNT + 1];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(statuses, compactAlertStatuses, sizeof(statuses));
  portEXIT_CRITICAL(&alertLogMux);
  statuses[UA_MAP_REGION_COUNT] = '\0';

  unsigned long lastOk = alertLastOkMs;
  bool hasData = lastOk != 0 && statuses[0] != '\0';
  bool fresh = hasData && millis() - lastOk < MAP_FRESH_MS;

  const uint16_t mapBg = MAP_BG;
  const uint16_t oblastBorder = RGB565(38, 40, 46);
  const uint16_t districtBorder = RGB565(72, 75, 82);
  if (full) tft.fillScreen(mapBg);  // повна заливка лише при першому відкритті

  uint8_t rowStorage[3][MAP_DST_W];
  uint8_t* rowA = rowStorage[0];
  uint8_t* rowB = rowStorage[1];
  uint8_t* rowC = rowStorage[2];
  uint16_t colors[MAP_DST_W];
  mapSrcRowY = -1;
  decodeScaledMapRow(0, rowA);
  decodeScaledMapRow(0, rowB);
  decodeScaledMapRow(1, rowC);

  tft.startWrite();
  tft.setAddrWindow(MAP_DST_X0, MAP_DST_Y0, MAP_DST_W, MAP_DST_H);
  for (int y = 0; y < MAP_DST_H; y++) {
    for (int x = 0; x < MAP_DST_W; x++) {
      uint8_t id = rowB[x];
      if (id == 255) {
        colors[x] = mapBg;
      } else {
        const uint8_t state = pgm_read_byte(&uaMapRegionState[id]);
        bool districtEdge = false, oblastEdge = false;
        bool outline = x == 0 || x == MAP_DST_W - 1 || y == 0 || y == MAP_DST_H - 1;
        classifyMapBoundary(id, x > 0 ? rowB[x - 1] : 255, state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, x + 1 < MAP_DST_W ? rowB[x + 1] : 255, state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, rowA[x], state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, rowC[x], state, districtEdge, oblastEdge, outline);

        if (outline || oblastEdge) colors[x] = oblastBorder;
        else if (districtEdge) colors[x] = districtBorder;
        else colors[x] = uaDistrictMapColor(id, statuses, hasData);
      }
    }
    tft.writePixels(colors, MAP_DST_W, true);

    uint8_t* oldA = rowA;
    rowA = rowB;
    rowB = rowC;
    rowC = oldA;
    if (y + 2 < MAP_DST_H) decodeScaledMapRow(y + 2, rowC);
    else memset(rowC, 255, MAP_DST_W);
  }
  tft.endWrite();

  // верхній ряд: годинник (зліва) і кнопка "НАЗАД" (справа) — у порожніх кутах мапи
  tft.fillRoundRect(244, 0, 74, 38, 5, DARKGREEN);
  tft.drawRoundRect(244, 0, 74, 38, 5, WHITE);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(281, 24, "НАЗАД", WHITE, DARKGREEN);
  mapClockDrawn[0] = '\0';
  drawMapClock(true);

  // легенда в один рядок + час оновлення справа
  tft.fillRect(0, 222, 320, 18, mapBg);
  tft.fillRect(4, 227, 8, 8, RGB565(220, 35, 35));
  tft.fillRect(66, 227, 8, 8, RGB565(245, 185, 0));
  tft.fillRect(134, 227, 8, 8, RGB565(25, 155, 65));
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawText(16, 236, "ТРИВОГА", WHITE, mapBg);
  drawText(78, 236, "ЧАСТКОВА", WHITE, mapBg);
  drawText(146, 236, "СПОКІЙНО", WHITE, mapBg);
  drawMapAge();

  memcpy(mapDrawnStatuses, statuses, sizeof(mapDrawnStatuses));
  mapDrawnFresh = fresh;
}

void updateAlertMapIfNeeded() {
  if (menuState != 5 || touchTracking) return;
  drawMapClock(false);
  static unsigned long lastAgeDraw = 0;
  if (millis() - lastAgeDraw >= 5000) {
    lastAgeDraw = millis();
    drawMapAge();
  }

  char current[UA_MAP_REGION_COUNT + 1];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(current, compactAlertStatuses, sizeof(current));
  portEXIT_CRITICAL(&alertLogMux);
  current[UA_MAP_REGION_COUNT] = '\0';

  unsigned long lastOk = alertLastOkMs;
  bool fresh = lastOk != 0 && millis() - lastOk < MAP_FRESH_MS;
  if (fresh != mapDrawnFresh || memcmp(current, mapDrawnStatuses, sizeof(current)) != 0)
    drawAlertMap(false);
}

constexpr int MENU_COL1_X = 12;
constexpr int MENU_COL2_X = 166;
constexpr int MENU_COL_W = 142;
constexpr int MENU_ROW1_Y = 44;
constexpr int MENU_ROW2_Y = 92;
constexpr int MENU_ROW3_Y = 140;
constexpr int MENU_ITEM_H = 40;
constexpr int MENU_BACK_Y = 194;
constexpr int MENU_BACK_H = 40;

void drawMenu() {
  tft.fillScreen(BACKGROUND_COLOR);

  const uint16_t cBright = RGB565(150, 100, 20);
  const uint16_t cParams = RGB565(35, 80, 145);
  const uint16_t cBg     = RGB565(105, 55, 135);
  const uint16_t cMap    = RGB565(155, 45, 45);
  const uint16_t cWifi   = RGB565(20, 115, 105);
  const uint16_t cConfig = RGB565(35, 115, 55);

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 30, "Меню", CYAN, BACKGROUND_COLOR);

  tft.fillRoundRect(MENU_COL1_X, MENU_ROW1_Y, MENU_COL_W, MENU_ITEM_H, 8, cBright);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW1_Y, MENU_COL_W, MENU_ITEM_H, 8, cParams);
  tft.fillRoundRect(MENU_COL1_X, MENU_ROW2_Y, MENU_COL_W, MENU_ITEM_H, 8, cBg);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW2_Y, MENU_COL_W, MENU_ITEM_H, 8, cMap);
  tft.fillRoundRect(MENU_COL1_X, MENU_ROW3_Y, MENU_COL_W, MENU_ITEM_H, 8, cWifi);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW3_Y, MENU_COL_W, MENU_ITEM_H, 8, cConfig);
  tft.fillRoundRect(MENU_COL1_X, MENU_BACK_Y, 296, MENU_BACK_H, 8, DARKGREEN);

  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW1_Y + 27, "Яскравість", WHITE, cBright);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW1_Y + 27, "Параметри", WHITE, cParams);
  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW2_Y + 27, "Фон", WHITE, cBg);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW2_Y + 27, "Мапа тривог", WHITE, cMap);
  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW3_Y + 27, "Зміна Wi-Fi", WHITE, cWifi);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW3_Y + 27, "Налаштування", WHITE, cConfig);
  drawTextCentered(160, MENU_BACK_Y + 27, "Назад", WHITE, DARKGREEN);
}

void drawWifiConfigScreen(const char* portalPassword, bool manual) {
  tft.fillScreen(BACKGROUND_COLOR);
  char qrText[72];
  snprintf(qrText, sizeof(qrText), "WIFI:T:WPA;S:ESP32-WeatherStation;P:%s;;", portalPassword);
  drawQr(qrText, 6, 9, 6);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);

  drawText(236, 26, "Підключення", CYAN, BACKGROUND_COLOR);
  drawText(236, 56, "Наведи камеру", WHITE, BACKGROUND_COLOR);
  drawText(236, 72, "на QR-код,", WHITE, BACKGROUND_COLOR);
  drawText(236, 88, "далі обери", WHITE, BACKGROUND_COLOR);
  drawText(236, 104, "свою Wi-Fi", WHITE, BACKGROUND_COLOR);
  drawText(236, 120, "мережу", WHITE, BACKGROUND_COLOR);
  drawText(236, 150, "вручну:", GREY, BACKGROUND_COLOR);
  drawText(236, 166, portalPassword, YELLOW, BACKGROUND_COLOR);
  drawText(236, 184, "192.168.4.1", GREY, BACKGROUND_COLOR);

  if (manual) {
    tft.fillRoundRect(236, 198, 80, 38, 8, DARKGREEN);
    u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
    drawTextCentered(276, 223, "Назад", WHITE, DARKGREEN);
  }
}

void closeWifiConfigPortal() {
  bool ok = (wpState == WP_OK);
  wpStop();
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  wifiPortalActive = false;
  wifiPortalStartedMs = 0;
  if (ok) {
    menuState = 7;
    drawSettingsQr();
  } else {
    menuState = 1;
    drawMenu();
  }
}

void startWifiConfigPortal() {
  wifiPortalActive = true;
  wifiPortalStartedMs = millis();
  menuState = 6;
  char portalPassword[10];
  snprintf(portalPassword, sizeof(portalPassword), "W%08lX", (unsigned long)esp_random());
  drawWifiConfigScreen(portalPassword, true);
  webStop();
  wpStart(portalPassword);
}

void drawBrightnessBar() {
  u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
  tft.fillRect(0, 55, 320, 50, BACKGROUND_COLOR);
  String bStr = String(brightness) + " %";
  drawTextCentered(160, 92, bStr.c_str(), YELLOW, BACKGROUND_COLOR);

  tft.drawRoundRect(30, 120, 260, 30, 6, WHITE);
  tft.fillRect(32, 122, 256, 26, BACKGROUND_COLOR);
  int w = map(brightness, 0, 100, 0, 256);
  tft.fillRect(32, 122, w, 26, YELLOW);
}

void drawBrightness() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 30, "Яскравiсть", CYAN, BACKGROUND_COLOR);
  drawBrightnessBar();

  tft.fillRoundRect(20, 190, 280, 42, 8, DARKGREEN);
  drawTextCentered(160, 219, "Назад", WHITE, DARKGREEN);
}

void setBrightnessFromTouch(int sx, int sy) {
  if (!inRect(sx, sy, 20, 105, 300, 170)) return;
  int level = constrain((sx - 30) * 100 / 260, 10, 100);
  if (level != brightness) {
    brightness = level;
    applyBrightness();
    drawBrightnessBar();
  }
}

void flashMenuButton(int sx, int sy) {
  if (menuState != 1) return;

  int x, y, w, h;

  if (inRect(sx, sy, MENU_COL1_X, MENU_ROW1_Y,
             MENU_COL1_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
    x = MENU_COL1_X; y = MENU_ROW1_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW1_Y,
                    MENU_COL2_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
    x = MENU_COL2_X; y = MENU_ROW1_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW2_Y,
                    MENU_COL1_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
    x = MENU_COL1_X; y = MENU_ROW2_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW2_Y,
                    MENU_COL2_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
    x = MENU_COL2_X; y = MENU_ROW2_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW3_Y,
                    MENU_COL1_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
    x = MENU_COL1_X; y = MENU_ROW3_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW3_Y,
                    MENU_COL2_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
    x = MENU_COL2_X; y = MENU_ROW3_Y; w = MENU_COL_W; h = MENU_ITEM_H;
  } else if (inRect(sx, sy, 12, MENU_BACK_Y, 308, MENU_BACK_Y + MENU_BACK_H)) {
    x = 12; y = MENU_BACK_Y; w = 296; h = MENU_BACK_H;
  } else {
    return;
  }

  tft.drawRoundRect(x, y, w, h, 8, WHITE);
  delay(60);
}

void handleMenuTap(int sx, int sy) {
  flashMenuButton(sx, sy);
  if (menuState == 6) {
    if (inRect(sx, sy, 12, MENU_BACK_Y, 308, MENU_BACK_Y + MENU_BACK_H)) {
      closeWifiConfigPortal();
    }
  } else if (menuState == 7) {
    if (inRect(sx, sy, 226, 188, 319, 239)) {
      menuState = 1;
      webConfigSessionActive = false;
      lastWeatherUpdate = 0;
      drawMenu();
    }
  } else if (menuState == 1) {
    if (inRect(sx, sy, MENU_COL1_X, MENU_ROW1_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
      menuState = 2;
      drawBrightness();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW1_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
      menuState = 3;
      sysScroll = 0;
      sysForce = true;
      drawSystem();
    } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW2_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
      menuState = 4;
      bgTarget = 0;
      loadBgTarget();
      drawBgScreen();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW2_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
      menuState = 5;
      drawAlertMap(true);
    } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW3_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
      startWifiConfigPortal();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW3_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
      menuState = 7;
      drawSettingsQr();
    } else if (inRect(sx, sy, 12, MENU_BACK_Y, 308, MENU_BACK_Y + MENU_BACK_H)) {
      menuState = 0;
      returnToMain();
    }
  } else if (menuState == 2) {
    if (inRect(sx, sy, 20, 190, 300, 232)) {
      saveBrightness();
      menuState = 1;
      drawMenu();
    } else {
      setBrightnessFromTouch(sx, sy);
    }
  } else if (menuState == 3) {
    if (inRect(sx, sy, 20, 212, 300, 238)) {
      menuState = 1;
      drawMenu();
    }
  } else if (menuState == 4) {
    if (inRect(sx, sy, 0, 27, 320, 52)) {
      bgTarget = constrain(sx / 80, 0, 3);
      loadBgTarget();
      drawBgScreen();
    } else if (inRect(sx, sy, 4, 190, 94, 232)) {
      bgR = 0;
      bgG = 0;
      bgB = 40;
      bgAR = bgR; bgAG = bgG; bgAB = bgB;
      for (int ch = 0; ch < 3; ch++) drawBgSlider(ch);
      drawBgPreview();
    } else if (inRect(sx, sy, 98, 190, 232, 232)) {
      saveBgTarget();
      menuState = 0;
      returnToMain();
    } else if (inRect(sx, sy, 236, 190, 316, 232)) {
      bgR = bgAR;
      bgG = bgAG;
      bgB = bgAB;
      menuState = 1;
      drawMenu();
    } else {
      setBgFromTouch(sx, sy);
    }
  } else if (menuState == 5) {
    if (inRect(sx, sy, 236, 0, 319, 38)) {
      menuState = 1;
      drawMenu();
    }
  }
}

#define SYS_LINE_H 18
#define SYS_TOP 28
#define SYS_BOTTOM 210
#define SYS_MAX_LINES 40

String sysLbl[SYS_MAX_LINES];
String sysVal[SYS_MAX_LINES];
int sysCount = 0;

String fmtKB(uint32_t bytes) {
  return String(bytes / 1024) + " КБ";
}
String fmtUptime() {
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400UL;
  s %= 86400UL;
  char b[28];
  snprintf(b, sizeof(b), "%lu д %02lu:%02lu:%02lu", d, s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
  return String(b);
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "ввiмкнення живлення";
    case ESP_RST_SW: return "програмний restart";
    case ESP_RST_PANIC: return "ЗБIЙ програми (panic)";
    case ESP_RST_INT_WDT: return "watchdog (int)";
    case ESP_RST_TASK_WDT: return "watchdog (задача)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "ПРОСАДКА живлення";
    case ESP_RST_EXT: return "кнопка / зовнiшнiй";
    case ESP_RST_DEEPSLEEP: return "глибокий сон";
    default: return "невiдомо";
  }
}

void addSys(const char* label, const String& value) {
  if (sysCount < SYS_MAX_LINES) {
    sysLbl[sysCount] = label;
    sysVal[sysCount] = value;
    sysCount++;
  }
}

void buildSysLines() {
  sysCount = 0;
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  addSys("Wi-Fi", wifiOk ? WiFi.SSID() : String("немає зв'язку"));
  addSys("IP", wifiOk ? WiFi.localIP().toString() : String("-"));
  addSys("Шлюз", WiFi.gatewayIP().toString());
  addSys("DNS", WiFi.dnsIP().toString());
  addSys("MAC", WiFi.macAddress());
  addSys("Канал", String(WiFi.channel()));
  int rssi = WiFi.RSSI();
  const char* q = rssi >= -50 ? "вiдмiнний" : rssi >= -60 ? "дуже добрий"
                                            : rssi >= -70 ? "добрий"
                                            : rssi >= -80 ? "слабкий"
                                                          : "поганий";
  addSys("Сигнал", wifiOk ? String(rssi) + " dBm (" + q + ")" : String("-"));
  addSys("Працює", fmtUptime());
  addSys("Причина старту", resetReasonText());
  if (strlen(lastRestartWhy) > 0) addSys("Чому restart", lastRestartWhy);
  addSys("Обривiв Wi-Fi", String(wifiDrops));
  addSys("Останнiй обрив", lastWifiEvent);
  addSys("Чип", String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + ", " + String(ESP.getChipCores()) + " ядра");
  addSys("Частота CPU", String(ESP.getCpuFreqMHz()) + " МГц");
  float tc = temperatureRead();
  addSys("Темп. чипа", (isnan(tc) || tc < -20 || tc > 110) ? String("-") : String(tc, 0) + " C");
  addSys("Heap вiльно", fmtKB(ESP.getFreeHeap()) + " / " + fmtKB(ESP.getHeapSize()));
  addSys("Heap мiнiмум", fmtKB(ESP.getMinFreeHeap()));
  addSys("Flash", String(ESP.getFlashChipSize() / 1048576) + " МБ");
  addSys("LittleFS", fmtKB(LittleFS.usedBytes()) + " / " + fmtKB(LittleFS.totalBytes()));
  addSys("Цикл loop", String(loopRate) + " /с, макс. пауза " + String(loopMaxGap) + " мс");
  addSys("Тривога: мiсто 1", cityLastOkMsBySlot[0] ? "ok " + String((millis() - cityLastOkMsBySlot[0]) / 1000UL) + " с тому" : String("немає вiдповiдi"));
  addSys("Тривога: мiсто 2", cityLastOkMsBySlot[1] ? "ok " + String((millis() - cityLastOkMsBySlot[1]) / 1000UL) + " с тому" : String("немає вiдповiдi"));
  addSys("Помилок API", String(alertErrCount) + " (остання: " + String(alertLastErr) + ")");
  addSys("Погода", lastWeatherUpdate ? "оновл. " + String((millis() - lastWeatherUpdate) / 60000UL) + " хв тому" : String("-"));
  addSys("Час (NTP)", timeClient.valid() ? String("синхронiзовано") : String("НЕ синхронiзовано"));
}

void drawSystem() {
  if (sysForce || millis() - sysBuiltMs >= 5000) {
    buildSysLines();
    sysBuiltMs = millis();
    sysForce = false;
  }
  int contentH = sysCount * SYS_LINE_H;
  int viewH = SYS_BOTTOM - SYS_TOP;
  int maxScroll = max(0, contentH - viewH);
  sysScroll = constrain(sysScroll, 0, maxScroll);

  tft.fillRect(0, SYS_TOP, 320, viewH, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  for (int i = 0; i < sysCount; i++) {
    int base = SYS_TOP + 13 + i * SYS_LINE_H - sysScroll;
    if (base < SYS_TOP + 12 || base > SYS_BOTTOM - 2) continue;
    drawText(6, base, sysLbl[i].c_str(), CYAN, BACKGROUND_COLOR);
    drawText(112, base, sysVal[i].c_str(), WHITE, BACKGROUND_COLOR);
  }

  if (maxScroll > 0) {
    int barH = max(16, viewH * viewH / contentH);
    int barY = SYS_TOP + (viewH - barH) * sysScroll / maxScroll;
    tft.fillRect(316, SYS_TOP, 4, viewH, 0x2104);
    tft.fillRect(316, barY, 4, barH, GREY);
  }

  tft.fillRect(0, 0, 320, SYS_TOP, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 22, "Параметри", CYAN, BACKGROUND_COLOR);

  tft.fillRect(0, SYS_BOTTOM + 1, 320, 240 - SYS_BOTTOM - 1, BACKGROUND_COLOR);
  tft.fillRoundRect(20, 212, 280, 26, 6, DARKGREEN);
  drawTextCentered(160, 231, "Назад", WHITE, DARKGREEN);
}

void drawAlertHist() {
  AlertEv logCopy[2][ALERT_LOG_MAX];
  int cnt[2];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(logCopy, alertLog, sizeof(alertLog));
  cnt[0] = alertLogCount[0];
  cnt[1] = alertLogCount[1];
  portEXIT_CRITICAL(&alertLogMux);

  time_t nowLocal = (time_t)time(nullptr) + cachedTZ;
  tm* np = gmtime(&nowLocal);
  int todayY = np->tm_year, todayM = np->tm_mon, todayD = np->tm_mday;

  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 20, "Журнал тривог", CYAN, BACKGROUND_COLOR);

  const char* names[2] = { cfg.locTitle[0][0] ? cfg.locTitle[0] : "-", cfg.locTitle[1][0] ? cfg.locTitle[1] : "-" };
  const int ROWS = 8;
  for (int r = 0; r < 2; r++) {
    int x0 = 4 + r * 160;
    int cur = alertCurLevel[r];
    uint16_t hbg = (cur == 2) ? RED : (cur == 1) ? YELLOW
                                    : (cur == 0) ? DARKGREEN
                                                 : GREY;
    uint16_t hfg = (cur == 1) ? BLACK : WHITE;
    tft.fillRoundRect(x0, 28, 152, 20, 4, hbg);
    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawTextCentered(x0 + 76, 42, names[r], hfg, hbg);

    int startIdx = max(0, cnt[r] - ROWS);
    int row = 0;
    for (int i = startIdx; i < cnt[r]; i++) {
      int lvl = logCopy[r][i].lvl;
      uint16_t bg = (lvl == 2) ? RED : (lvl == 1) ? YELLOW
                                                  : DARKGREEN;
      uint16_t fg = (lvl == 1) ? BLACK : WHITE;
      const char* lab = (lvl == 2) ? "ТРИВОГА" : (lvl == 1) ? "ЖОВТИЙ"
                                                            : "ВIДБIЙ";

      time_t lt = (time_t)logCopy[r][i].t + cachedTZ;
      tm* p = gmtime(&lt);
      bool isToday = (p->tm_year == todayY && p->tm_mon == todayM && p->tm_mday == todayD);
      char line[40];
      if (isToday) {
        snprintf(line, sizeof(line), "%02d:%02d %s", p->tm_hour, p->tm_min, lab);
      } else {
        snprintf(line, sizeof(line), "%02d.%02d %02d:%02d %s", p->tm_mday, p->tm_mon + 1, p->tm_hour, p->tm_min, lab);
      }
      int y = 52 + row * 20;
      tft.fillRoundRect(x0, y, 152, 18, 4, bg);
      drawText(x0 + 6, y + 13, line, fg, bg);
      row++;
    }
    if (row == 0) {
      drawText(x0 + 6, 70, "немає подiй", GREY, BACKGROUND_COLOR);
    }
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 232, "тап - назад", GREY, BACKGROUND_COLOR);
}

void loadBg() {
  int b0 = brightness;
  cfgLoadUi(b0, bgR, bgG, bgB);
  bgAR = bgR;
  bgAG = bgG;
  bgAB = bgB;
  mainBgR = bgR; mainBgG = bgG; mainBgB = bgB;
  bgColor = RGB565(bgR, bgG, bgB);
  calendarBgColor = cfgLoadScreenBg("calBg", bgColor);
  uint16_t oldWeatherBg = cfgLoadScreenBg("graphBg", cfgLoadScreenBg("hourBg", bgColor));
  weatherBgColor = cfgLoadScreenBg("weatherBg", oldWeatherBg);
  clockBgColor = cfgLoadScreenBg("clockBg", bgColor);
}

void saveBg() {
  cfgSaveBg(bgR, bgG, bgB);
}

static void loadBgTarget() {
  if (bgTarget == 0) {
    bgR = mainBgR; bgG = mainBgG; bgB = mainBgB;
  } else {
    uint16_t c = bgTarget == 1 ? calendarBgColor : bgTarget == 2 ? weatherBgColor : clockBgColor;
    bgR = ((c >> 11) & 0x1F) * 255 / 31;
    bgG = ((c >> 5) & 0x3F) * 255 / 63;
    bgB = (c & 0x1F) * 255 / 31;
  }
  bgAR = bgR; bgAG = bgG; bgAB = bgB;
}

static void saveBgTarget() {
  uint16_t c = RGB565(bgR, bgG, bgB);
  if (bgTarget == 0) {
    mainBgR = bgR; mainBgG = bgG; mainBgB = bgB;
    bgColor = c;
    saveBg();
  } else if (bgTarget == 1) {
    calendarBgColor = c; cfgSaveScreenBg("calBg", c);
  } else if (bgTarget == 2) {
    weatherBgColor = c; cfgSaveScreenBg("weatherBg", c);
  } else {
    clockBgColor = c; cfgSaveScreenBg("clockBg", c);
  }
}

void drawBgSlider(int ch) {
  int y = 58 + ch * 32;
  int val = (ch == 0) ? bgR : (ch == 1) ? bgG
                                        : bgB;
  uint16_t col = (ch == 0) ? RED : (ch == 1) ? GREEN
                                             : RGB565(70, 130, 255);
  const char* name = (ch == 0) ? "R" : (ch == 1) ? "G"
                                                 : "B";

  tft.fillRect(0, y - 4, 320, 32, bgColor);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawText(8, y + 19, name, col, bgColor);

  tft.drawRoundRect(30, y, 220, 24, 5, WHITE);
  tft.fillRect(32, y + 2, 216, 20, 0x2104);
  int w = map(val, 0, 255, 0, 216);
  tft.fillRect(32, y + 2, w, 20, col);

  String valStr = String(val);
  drawText(258, y + 19, valStr.c_str(), WHITE, bgColor);
}

void drawBgPreview() {
  uint16_t c = RGB565(bgR, bgG, bgB);
  tft.fillRoundRect(20, 156, 280, 28, 6, c);
  tft.drawRoundRect(20, 156, 280, 28, 6, WHITE);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(90, 175, "Чернiгiв 12.3 C", YELLOW, c);
  drawTextCentered(220, 175, "Приклад тексту", WHITE, c);
}

void drawBgScreen() {
  bgColor = RGB565(mainBgR, mainBgG, mainBgB);  // редактор лишається на загальному фоні
  tft.fillScreen(bgColor);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 20, "Колiр фону", CYAN, bgColor);
  static const char* tabs[4] = { "Загальний", "Календар", "Погода", "Годинник" };
  for (int i = 0; i < 4; i++) {
    int x = i * 80;
    uint16_t tab = (i == bgTarget) ? RGB565(35, 82, 125) : RGB565(28, 37, 58);
    tft.fillRoundRect(x + 1, 27, 78, 24, 4, tab);
    tft.drawRoundRect(x + 1, 27, 78, 24, 4, (i == bgTarget) ? CYAN : GREY);
    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawTextCentered(x + 40, 44, tabs[i], WHITE, tab);
  }
  for (int ch = 0; ch < 3; ch++) drawBgSlider(ch);
  drawBgPreview();

  tft.fillRoundRect(4, 190, 90, 42, 8, RGB565(130, 70, 0));
  tft.fillRoundRect(98, 190, 134, 42, 8, DARKGREEN);
  tft.fillRoundRect(236, 190, 80, 42, 8, GREY);

  drawTextCentered(49, 217, "Скинути", WHITE, RGB565(130, 70, 0));
  drawTextCentered(165, 217, "Застосувати", WHITE, DARKGREEN);
  drawTextCentered(276, 217, "Назад", WHITE, GREY);
}

void setBgFromTouch(int sx, int sy) {
  for (int ch = 0; ch < 3; ch++) {
    int y = 58 + ch * 32;
    if (sy >= y - 4 && sy <= y + 28) {
      int x = constrain(sx, 32, 248);
      int value = map(x, 32, 248, 0, 255);
      if (ch == 0) bgR = value;
      else if (ch == 1) bgG = value;
      else bgB = value;
      drawBgSlider(ch);
      drawBgPreview();
      return;
    }
  }
}

void drawHourly() {
  bgColor = weatherBgColor;
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 28, "Прогноз по годинах", CYAN, BACKGROUND_COLOR);
  if (hrCount == 0) {
    drawTextCentered(160, 120, "Немає даних", GREY, BACKGROUND_COLOR);
  }
  for (int i = 0; i < hrCount; i++) {
    int x = i * FORECAST_ITEM_W + 1;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d", hrCache[i].hh, hrCache[i].mm);
    int tw = u8g2Fonts.getUTF8Width(buf);
    drawText(x + (IMG_WIDTH - tw) / 2, 70, buf, CYAN, BACKGROUND_COLOR);

    displayWeatherIcon(hrCache[i].cond, x, 80, hrCache[i].night);

    snprintf(buf, sizeof(buf), "%d C", hrCache[i].temp);
    tw = u8g2Fonts.getUTF8Width(buf);
    drawText(x + (IMG_WIDTH - tw) / 2, 160, buf, GREEN, BACKGROUND_COLOR);
  }
  drawTextCentered(160, 232, "тап   назад", GREY, BACKGROUND_COLOR);
}

void drawForecastGraph() {
  bgColor = weatherBgColor;
  const uint16_t dayColor = RGB565(255, 166, 70);
  const uint16_t nightColor = RGB565(86, 190, 255);
  const uint16_t gridColor = RGB565(53, 67, 91);
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 26, "Температура на 5 днів", CYAN, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  tft.fillCircle(93, 43, 3, dayColor);
  drawText(101, 47, "День", dayColor, BACKGROUND_COLOR);
  tft.fillCircle(183, 43, 3, nightColor);
  drawText(191, 47, "Ніч", nightColor, BACKGROUND_COLOR);

  if (fcCount <= 0) {
    drawTextCentered(160, 126, "Немає даних прогнозу", GREY, BACKGROUND_COLOR);
    drawTextCentered(160, 232, "тап — назад", GREY, BACKGROUND_COLOR);
    return;
  }

  int low = fcCache[0].tmin, high = fcCache[0].tmax;
  for (int i = 1; i < fcCount; i++) {
    low = min(low, fcCache[i].tmin);
    high = max(high, fcCache[i].tmax);
  }
  if (high - low < 4) { low -= 2; high += 2; }
  else { low--; high++; }

  const int graphTop = 64, graphBottom = 168;
  const int graphLeft = 34, graphRight = 300;
  for (int g = 0; g <= 4; g++) {
    int y = graphTop + (graphBottom - graphTop) * g / 4;
    int value = high - (high - low) * g / 4;
    tft.drawFastHLine(graphLeft, y, graphRight - graphLeft, gridColor);
    char label[8];
    snprintf(label, sizeof(label), "%d°", value);
    drawText(2, y + 4, label, GREY, BACKGROUND_COLOR);
  }

  int px[5], pyDay[5], pyNight[5];
  for (int i = 0; i < fcCount; i++) {
    px[i] = (fcCount == 1) ? (graphLeft + graphRight) / 2
                            : graphLeft + 10 + i * (graphRight - graphLeft - 20) / (fcCount - 1);
    pyDay[i] = graphBottom - (fcCache[i].tmax - low) * (graphBottom - graphTop) / (high - low);
    pyNight[i] = graphBottom - (fcCache[i].tmin - low) * (graphBottom - graphTop) / (high - low);
    if (i > 0) {
      tft.drawLine(px[i - 1], pyDay[i - 1], px[i], pyDay[i], dayColor);
      tft.drawLine(px[i - 1], pyNight[i - 1], px[i], pyNight[i], nightColor);
    }
    tft.drawLine(px[i], pyDay[i], px[i], pyNight[i], RGB565(110, 120, 145));
    tft.fillCircle(px[i], pyDay[i], 4, dayColor);
    tft.fillCircle(px[i], pyNight[i], 4, nightColor);
    char value[8];
    snprintf(value, sizeof(value), "%d°", fcCache[i].tmax);
    drawTextCentered(px[i], pyDay[i] - 6, value, dayColor, BACKGROUND_COLOR);
    snprintf(value, sizeof(value), "%d°", fcCache[i].tmin);
    drawTextCentered(px[i], pyNight[i] + 15, value, nightColor, BACKGROUND_COLOR);
    drawTextCentered(px[i], 190, fcCache[i].name, WHITE, BACKGROUND_COLOR);
  }
  drawTextCentered(160, 232, "тап — назад", GREY, BACKGROUND_COLOR);
}

// Рядок тісного списку (крок 13 пікс.)
void drawTextTight(int x, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  tft.fillRect(x, y - 10, tw + 2, 13, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

// Якість повітря: індекс AQI 1–5 від OpenWeatherMap (це не відсотки)
int lastAqi = 0;
float wxLat = 0, wxLon = 0;
bool wxHaveCoord = false;

const char* aqiWord(int a) {
  switch (a) {
    case 1: return "чисте";
    case 2: return "добре";
    case 3: return "помiрне";
    case 4: return "погане";
    case 5: return "дуже погане";
  }
  return "";
}

uint16_t aqiColor(int a) {
  return a <= 2 ? GREEN : a == 3 ? YELLOW
                        : a == 4 ? ORANGE
                                 : RED;
}

void drawAqiLine() {
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  tft.fillRect(TEMP_X, WX_Y_AQI - 10, 150, 13, BACKGROUND_COLOR);
  if (lastAqi <= 0) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "Повiтря: %d/5 %s", lastAqi, aqiWord(lastAqi));
  drawTextTight(TEMP_X, WX_Y_AQI, buf, aqiColor(lastAqi), BACKGROUND_COLOR);
}

void redrawMain() {
  bgColor = RGB565(mainBgR, mainBgG, mainBgB);
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  if (strlen(lastTemperature) > 0) {
    String tempStr = String(cfg.weatherTitle) + "  " + lastTemperature;
    drawText(1, 10, tempStr.c_str(), YELLOW, BACKGROUND_COLOR);
  }
  if (strlen(lastSunrise) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Схід: %s", lastSunrise);
    drawTextTight(TEMP_X, WX_Y_SUN, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastSunset) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Зах: %s", lastSunset);
    drawTextTight(TEMP_X + 70, WX_Y_SUN, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastPressure) > 0) {
    char buf[40];
    snprintf(buf, sizeof(buf), "Тиск: %s%s", lastPressure, cfg.pressMm ? " мм рт.ст." : " hPa");
    drawTextTight(TEMP_X, WX_Y_PRESS, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastHumidity) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Вологiсть: %s %%", lastHumidity);
    drawTextTight(TEMP_X, WX_Y_HUM, buf, WHITE, BACKGROUND_COLOR);
  }
  if (strlen(lastWindSpeed) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Вiтер: %s м/с", lastWindSpeed);
    drawTextTight(TEMP_X, WX_Y_WIND, buf, WHITE, BACKGROUND_COLOR);
  }
  if (lastAqi > 0) drawAqiLine();
  if (strlen(lastWeatherCondition) > 0) {
    displayWeatherIcon(lastWeatherCondition, TEMP_X + 1, TEMP_Y + 5);
  }

  for (int i = 0; i < fcCount; i++) {
    drawForecastItem(i, fcCache[i].cond, fcCache[i].tmin, fcCache[i].tmax, fcCache[i].name);
  }

  drawAlertStatus();
  static const uint16_t MENU_CELLS[9] = {
  0xF548, 0xEB29, 0xF548,
  0xEB29, 0xF548, 0x5E5A,
  0xF548, 0xFF7D, 0xEB29
};
  drawMenuButton();
  displayCalendar();
}

void returnToMain() {
  lastHours[0] = '\0';
  lastMinutes[0] = '\0';
  lastSeconds[0] = '\0';
  lastDayOfWeek[0] = '\0';
  redrawMain();
  lastCalendarUpdate = millis();
}

static void drawCalendarCorners(const tm* ptm) {
  const uint16_t window = RGB565(19, 43, 70);
  const uint16_t tick = RGB565(115, 145, 177);
  const char* weekdays[7] = { "НД", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
  const char* months[12] = { "СІЧ", "ЛЮТ", "БЕР", "КВІ", "ТРА", "ЧЕР", "ЛИП", "СЕР", "ВЕР", "ЖОВ", "ЛИС", "ГРУ" };

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  tft.fillRoundRect(5, 207, 54, 20, 5, window);
  tft.drawRoundRect(5, 207, 54, 20, 5, tick);
  drawTextCentered(32, 221, weekdays[ptm->tm_wday], RGB565(105, 220, 245), window);
  tft.fillRoundRect(261, 207, 54, 20, 5, window);
  tft.drawRoundRect(261, 207, 54, 20, 5, tick);
  drawTextCentered(288, 221, months[ptm->tm_mon], RGB565(255, 199, 105), window);
}
static void drawDialLabels() {
  const int cx = 160;
  const int cy = 120;
  const uint16_t dial = RGB565(10, 27, 52);
  const uint16_t silver = RGB565(190, 202, 214);
  const char* nums[12] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };
  const int nx[12] = { 160, 202, 231, 252, 231, 202, 160, 118, 89, 68, 89, 118 };
  const int ny[12] = { 39, 51, 80, 120, 161, 190, 201, 190, 161, 120, 80, 51 };

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  for (int i = 0; i < 12; i++) {
    if (i == 3) continue;
    uint16_t color = (i == 0 || i == 6) ? RGB565(255, 199, 105) : silver;
    drawTextCentered(nx[i], ny[i] + 7, nums[i], color, dial);
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(cx, 64, "CASIO", RGB565(170, 215, 238), dial);
  drawTextCentered(cx, 78, "QUARTZ", RGB565(135, 170, 198), dial);
}

static bool lineNearPoint(int x0, int y0, int x1, int y1, int px, int py, float radius) {
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  float len2 = dx * dx + dy * dy;
  float t = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
  t = constrain(t, 0.0f, 1.0f);
  float ex = x0 + t * dx - px, ey = y0 + t * dy - py;
  return ex * ex + ey * ey <= radius * radius;
}

static void restoreDialDetailsOnOldHands(int cx, int cy, int hx, int hy,
                                         int mx, int my, int sx, int sy) {
  const uint16_t dial = RGB565(10, 27, 52);
  const uint16_t silver = RGB565(190, 202, 214);
  const int nx[12] = { 160, 202, 231, 252, 231, 202, 160, 118, 89, 68, 89, 118 };
  const int ny[12] = { 39, 51, 80, 120, 161, 190, 201, 190, 161, 120, 80, 51 };
  const char* nums[12] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);

  for (int i = 0; i < 12; i++) {
    if (i == 3) continue;
    bool hit = lineNearPoint(cx, cy, hx, hy, nx[i], ny[i], 13) ||
               lineNearPoint(cx, cy, mx, my, nx[i], ny[i], 13) ||
               lineNearPoint(cx, cy, sx, sy, nx[i], ny[i], 13);
    if (hit) {
      uint16_t color = (i == 0 || i == 6) ? RGB565(255, 199, 105) : silver;
      drawTextCentered(nx[i], ny[i] + 7, nums[i], color, dial);
    }
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  if (lineNearPoint(cx, cy, hx, hy, 160, 64, 20) ||
      lineNearPoint(cx, cy, mx, my, 160, 64, 20) ||
      lineNearPoint(cx, cy, sx, sy, 160, 64, 20))
    drawTextCentered(160, 64, "CASIO", RGB565(170, 215, 238), dial);
  if (lineNearPoint(cx, cy, hx, hy, 160, 78, 22) ||
      lineNearPoint(cx, cy, mx, my, 160, 78, 22) ||
      lineNearPoint(cx, cy, sx, sy, 160, 78, 22))
    drawTextCentered(160, 78, "QUARTZ", RGB565(135, 170, 198), dial);
}
static void drawAnalogDial() {
  const int cx = 160;
  const int cy = 120;
  const uint16_t dial = RGB565(10, 27, 52);
  const uint16_t silver = RGB565(190, 202, 214);
  const uint16_t tick = RGB565(115, 145, 177);

  tft.fillScreen(BACKGROUND_COLOR);
  // Великий металевий обідок майже на всю висоту екрана.
  tft.fillCircle(cx, cy, 119, RGB565(80, 96, 116));
  tft.drawCircle(cx, cy, 118, WHITE);
  tft.fillCircle(cx, cy, 114, RGB565(28, 112, 66));  // зелений пояс
  tft.fillCircle(cx, cy, 110, RGB565(160, 48, 55));  // червоний середній обід
  tft.fillCircle(cx, cy, 106, RGB565(42, 56, 76));
  tft.fillCircle(cx, cy, 102, dial);

  // Хвилинна шкала та виразні годинні мітки.
  for (int i = 0; i < 60; i++) {
    float a = (i * 6.0f - 90.0f) * 0.01745329252f;
    int r1 = (i % 5 == 0) ? 91 : 97;
    int r2 = 101;
    int x1 = cx + lroundf(cosf(a) * r1);
    int y1 = cy + lroundf(sinf(a) * r1);
    int x2 = cx + lroundf(cosf(a) * r2);
    int y2 = cy + lroundf(sinf(a) * r2);
    tft.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? WHITE : tick);
  }

  drawDialLabels();

  // Тонке внутрішнє кільце.
  tft.drawCircle(cx, cy, 87, RGB565(55, 82, 112));
  tft.fillCircle(cx, cy, 4, silver);
}

static void drawClockHand(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  float dx = (float)(x1 - x0);
  float dy = (float)(y1 - y0);
  float length = sqrtf(dx * dx + dy * dy);
  if (length < 1.0f) return;

  float px = -dy / length;
  float py = dx / length;
  int half = thickness / 2;
  for (int offset = -half; offset <= half; offset++) {
    int ox = lroundf(px * offset);
    int oy = lroundf(py * offset);
    tft.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
  }
}
static void drawDateWindow(int dayOfMonth) {
  const int x = 229, y = 109, w = 37, h = 22;
  tft.fillRoundRect(x - 2, y - 2, w + 4, h + 4, 3, RGB565(180, 194, 208));
  tft.fillRect(x, y, w, h, RGB565(218, 226, 232));
  tft.drawRect(x, y, w, h, RGB565(35, 48, 65));
  char value[3];
  snprintf(value, sizeof(value), "%02d", dayOfMonth);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(x + w / 2, y + 17, value, RGB565(10, 24, 40), RGB565(218, 226, 232));
}

static void drawClockAnalogFull(bool force) {
  static bool faceDrawn = false;
  static bool handsDrawn = false;
  static int lastSecond = -1, lastMinute = -1, lastHour = -1, lastDay = -1;
  static int oldHx, oldHy, oldMx, oldMy, oldSx, oldSy;

  time_t t = (time_t)timeClient.getEpochTime();
  tm* ptm = gmtime(&t);
  if (!ptm) return;

  if (force || !faceDrawn) {
    drawAnalogDial();
    faceDrawn = true;
    handsDrawn = false;
    lastSecond = lastMinute = lastHour = lastDay = -1;
  }
  if (!force && ptm->tm_sec == lastSecond) return;

  const int cx = 160, cy = 120;
  const uint16_t dial = RGB565(10, 27, 52);
  const float deg = 0.01745329252f;
  bool dateChanged = (lastDay != ptm->tm_mday);
  if (dateChanged) {
    drawCalendarCorners(ptm);
    lastDay = ptm->tm_mday;
  }

  float hourA = (((ptm->tm_hour % 12) + ptm->tm_min / 60.0f) * 30.0f - 90.0f) * deg;
  float minA = (ptm->tm_min * 6.0f - 90.0f) * deg;
  float secA = (ptm->tm_sec * 6.0f - 90.0f) * deg;
  int hx = cx + lroundf(cosf(hourA) * 49), hy = cy + lroundf(sinf(hourA) * 49);
  int mx = cx + lroundf(cosf(minA) * 68), my = cy + lroundf(sinf(minA) * 68);
  int sx = cx + lroundf(cosf(secA) * 80), sy = cy + lroundf(sinf(secA) * 80);

  bool hourChanged = !handsDrawn || ptm->tm_hour != lastHour || ptm->tm_min != lastMinute;
  bool minuteChanged = !handsDrawn || ptm->tm_min != lastMinute;
  // Стрілки годин і хвилин не треба перемальовувати на кожному тіку.
  // Секундна стрілка проходить через їхню спільну вісь у центрі; втулка
  // перемальовується нижче, тому це перетин не потребує стирання стрілок.
  bool repaintHour = hourChanged;
  bool repaintMinute = minuteChanged;
  bool dateWasCovered = dateChanged || (handsDrawn && lineNearPoint(cx, cy, oldSx, oldSy, 247, 120, 24)) ||
                        (handsDrawn && hourChanged && lineNearPoint(cx, cy, oldHx, oldHy, 247, 120, 24)) ||
                        (handsDrawn && minuteChanged && lineNearPoint(cx, cy, oldMx, oldMy, 247, 120, 24));

  if (handsDrawn) {
    // Секундна стрілка змінюється щосекунди. Статичні стрілки стираються лише
    // коли змінилася хвилина/година, або відновлюються при рідкісному перетині.
    drawClockHand(cx, cy, oldSx, oldSy, dial, 1);
    if (hourChanged) drawClockHand(cx, cy, oldHx, oldHy, dial, 9);
    if (minuteChanged) drawClockHand(cx, cy, oldMx, oldMy, dial, 5);

    int restoreHx = (hourChanged || repaintHour) ? oldHx : cx;
    int restoreHy = (hourChanged || repaintHour) ? oldHy : cy;
    int restoreMx = (minuteChanged || repaintMinute) ? oldMx : cx;
    int restoreMy = (minuteChanged || repaintMinute) ? oldMy : cy;
    restoreDialDetailsOnOldHands(cx, cy, restoreHx, restoreHy, restoreMx, restoreMy, oldSx, oldSy);
  }

  uint16_t hourColor = RGB565(163, 107, 42);
  uint16_t hourHighlight = RGB565(255, 224, 157);
  uint16_t minuteColor = RGB565(39, 119, 173);
  uint16_t minuteHighlight = RGB565(225, 240, 248);
  uint16_t secondColor = RGB565(255, 112, 45);
  if (repaintHour) {
    drawClockHand(cx, cy, hx, hy, hourColor, 9);
    drawClockHand(cx, cy, hx, hy, hourHighlight, 5);
  }
  if (repaintMinute) {
    drawClockHand(cx, cy, mx, my, minuteColor, 5);
    drawClockHand(cx, cy, mx, my, minuteHighlight, 1);
  }
  drawClockHand(cx, cy, sx, sy, secondColor, 1);
  tft.fillCircle(cx, cy, 5, RGB565(190, 202, 214));
  tft.fillCircle(cx, cy, 2, secondColor);

  if (dateWasCovered) drawDateWindow(ptm->tm_mday);

  oldHx = hx; oldHy = hy; oldMx = mx; oldMy = my; oldSx = sx; oldSy = sy;
  lastSecond = ptm->tm_sec;
  lastMinute = ptm->tm_min;
  lastHour = ptm->tm_hour;
  handsDrawn = true;
}

static void drawClockViewButton() {
  const uint16_t bg = RGB565(18, 48, 72);
  tft.fillRoundRect(250, 5, 65, 29, 6, bg);
  tft.drawRoundRect(250, 5, 65, 29, 6, RGB565(255, 199, 105));
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(282, 24, "ВИГЛЯД", WHITE, bg);
}

static void drawClockDigitalFull(bool force) {
  static char lastHour[3] = "";
  static char lastMinute[3] = "";
  static char lastDate[16] = "";
  static int lastSecondTick = -1;
  static bool screenDrawn = false;
  static unsigned long colonOffAt = 0;
  static bool colonVisible = false;
  static bool colonDrawnOnScreen = false;

  time_t t = (time_t)timeClient.getEpochTime();
  tm* ptm = gmtime(&t);
  if (!ptm) return;

  if (force || !screenDrawn) {
    tft.fillScreen(BACKGROUND_COLOR);
    lastHour[0] = '\0';
    lastMinute[0] = '\0';
    lastDate[0] = '\0';
    lastSecondTick = -1;
    colonVisible = false;
    colonDrawnOnScreen = false;
    screenDrawn = true;
  }

  const uint16_t bg = BACKGROUND_COLOR;
  const uint16_t fg = RGB565(235, 241, 245);
  char hourText[3], minuteText[3], secondText[3];
  snprintf(hourText, sizeof(hourText), "%02d", ptm->tm_hour);
  snprintf(minuteText, sizeof(minuteText), "%02d", ptm->tm_min);
  snprintf(secondText, sizeof(secondText), "%02d", ptm->tm_sec);

  // Увесь час однакового великого розміру. Кожна пара цифр оновлюється
  // лише тоді, коли змінюється, тому години й хвилини не блимають щосекунди.
  u8g2Fonts.setFont(u8g2_font_logisoso62_tn);
  const int digitW = u8g2Fonts.getUTF8Width("00");
  const int sepW = 18;
  const int fullW = digitW * 3 + sepW * 2;
  const int startX = (320 - fullW) / 2;
  const int hourX = startX;
  const int hourColonX = hourX + digitW + (sepW - 4) / 2;
  const int minuteX = hourX + digitW + sepW;
  const int secondsColonX = minuteX + digitW + (sepW - 4) / 2;
  const int secondX = minuteX + digitW + sepW;
  const int timeBaseline = 121;

  bool hourChanged = strcmp(lastHour, hourText) != 0;
  bool minuteChanged = strcmp(lastMinute, minuteText) != 0;
  if (hourChanged) {
    tft.fillRect(hourX - 3, 35, digitW + 6, 94, bg);
    u8g2Fonts.setFont(u8g2_font_logisoso62_tn);
    u8g2Fonts.setBackgroundColor(bg);
    u8g2Fonts.setForegroundColor(fg);
    u8g2Fonts.setCursor(hourX, timeBaseline);
    u8g2Fonts.print(hourText);
    strlcpy(lastHour, hourText, sizeof(lastHour));
  }
  if (minuteChanged) {
    tft.fillRect(minuteX - 3, 35, digitW + 6, 94, bg);
    u8g2Fonts.setFont(u8g2_font_logisoso62_tn);
    u8g2Fonts.setBackgroundColor(bg);
    u8g2Fonts.setForegroundColor(fg);
    u8g2Fonts.setCursor(minuteX, timeBaseline);
    u8g2Fonts.print(minuteText);
    strlcpy(lastMinute, minuteText, sizeof(lastMinute));
  }
  bool secondChanged = (ptm->tm_sec != lastSecondTick);
  if (secondChanged) {
    tft.fillRect(secondX - 3, 35, digitW + 6, 94, bg);
    u8g2Fonts.setFont(u8g2_font_logisoso62_tn);
    u8g2Fonts.setBackgroundColor(bg);
    u8g2Fonts.setForegroundColor(RGB565(255, 199, 105));
    u8g2Fonts.setCursor(secondX, timeBaseline);
    u8g2Fonts.print(secondText);
    lastSecondTick = ptm->tm_sec;
    colonVisible = true;
    colonOffAt = millis() + 180UL;
  }

  // Двокрапка між годинами й хвилинами постійна.
  if (force || hourChanged || minuteChanged) {
    tft.fillRect(hourX + digitW, 57, sepW, 48, bg);
    tft.fillCircle(hourColonX + 2, 75, 3, RGB565(255, 199, 105));
    tft.fillCircle(hourColonX + 2, 94, 3, RGB565(255, 199, 105));
  }
  if (colonVisible && (long)(millis() - colonOffAt) >= 0) colonVisible = false;
  if (force || secondChanged || colonVisible != colonDrawnOnScreen) {
    tft.fillRect(minuteX + digitW, 57, sepW, 48, bg);
    if (colonVisible) {
      tft.fillCircle(secondsColonX + 2, 75, 3, RGB565(255, 199, 105));
      tft.fillCircle(secondsColonX + 2, 94, 3, RGB565(255, 199, 105));
    }
    colonDrawnOnScreen = colonVisible;
  }

  char dateText[16], dayText[3], monthText[3], yearText[5];
  snprintf(dayText, sizeof(dayText), "%02d", ptm->tm_mday);
  snprintf(monthText, sizeof(monthText), "%02d", ptm->tm_mon + 1);
  snprintf(yearText, sizeof(yearText), "%04d", ptm->tm_year + 1900);
  snprintf(dateText, sizeof(dateText), "%s.%s.%s", dayText, monthText, yearText);
  if (strcmp(lastDate, dateText) != 0) {
    tft.fillRect(20, 143, 280, 53, bg);
    u8g2Fonts.setFont(u8g2_font_logisoso38_tn);
    int dayW = u8g2Fonts.getUTF8Width(dayText);
    int dotW = u8g2Fonts.getUTF8Width(".");
    int monthW = u8g2Fonts.getUTF8Width(monthText);
    int yearW = u8g2Fonts.getUTF8Width(yearText);
    int dateW = dayW + dotW + monthW + dotW + yearW;
    int dateX = 160 - dateW / 2;
    u8g2Fonts.setBackgroundColor(bg);
    u8g2Fonts.setForegroundColor(RGB565(120, 185, 255));  // число місяця: інший колір, ніж секунди
    u8g2Fonts.setCursor(dateX, 184);
    u8g2Fonts.print(dayText);
    dateX += dayW;
    u8g2Fonts.setForegroundColor(RGB565(190, 202, 214));
    u8g2Fonts.setCursor(dateX, 184);
    u8g2Fonts.print(".");
    dateX += dotW;
    u8g2Fonts.setForegroundColor(RGB565(72, 205, 190));
    u8g2Fonts.setCursor(dateX, 184);
    u8g2Fonts.print(monthText);
    dateX += monthW;
    u8g2Fonts.setForegroundColor(RGB565(190, 202, 214));
    u8g2Fonts.setCursor(dateX, 184);
    u8g2Fonts.print(".");
    dateX += dotW;
    u8g2Fonts.setForegroundColor(RGB565(155, 180, 205));
    u8g2Fonts.setCursor(dateX, 184);
    u8g2Fonts.print(yearText);
    strlcpy(lastDate, dateText, sizeof(lastDate));
  }

  static int lastWeekday = -1;
  if (force || lastWeekday != ptm->tm_wday) {
    const char* weekdays[7] = { "НЕДІЛЯ", "ПОНЕДІЛОК", "ВІВТОРОК", "СЕРЕДА", "ЧЕТВЕР", "П'ЯТНИЦЯ", "СУБОТА" };
    tft.fillRoundRect(82, 207, 156, 25, 6, RGB565(18, 48, 72));
    tft.drawRoundRect(82, 207, 156, 25, 6, RGB565(75, 125, 155));
    u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
    drawTextCentered(160, 226, weekdays[ptm->tm_wday], RGB565(120, 220, 235), RGB565(18, 48, 72));
    lastWeekday = ptm->tm_wday;
  }
}

void drawClockFull(bool force) {
  bgColor = clockBgColor;
  if (clockAnalogView) drawClockAnalogFull(force);
  else drawClockDigitalFull(force);
  if (force) drawClockViewButton();
}
void restoreAfterAlarm() {
  if (calendarFullscreen) displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
  else if (clockFullscreen) { tft.fillScreen(BACKGROUND_COLOR); drawClockFull(true); }
  else if (forecastGraphScreen) drawForecastGraph();
  else if (alertHistScreen) drawAlertHist();
  else if (hourlyScreen) drawHourly();
  else if (menuState == 5) drawAlertMap(true);
  else if (menuState == 4) drawBgScreen();
  else if (menuState == 3) drawSystem();
  else if (menuState == 2) drawBrightness();
  else if (menuState == 1) drawMenu();
  else returnToMain();
}

static void wxField(char* last, size_t cap, const char* val, int x, int y,
                    uint16_t color, const char* pre, const char* post) {
  if (strcmp(last, val) == 0) return;
  char buf[72];
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  if (last[0]) {
    snprintf(buf, sizeof(buf), "%s%s%s", pre, last, post);
    drawTextTight(x, y, buf, BACKGROUND_COLOR, BACKGROUND_COLOR);  // затираємо старий текст
  }
  snprintf(buf, sizeof(buf), "%s%s%s", pre, val, post);
  drawTextTight(x, y, buf, color, BACKGROUND_COLOR);
  strlcpy(last, val, cap);
}

void updateWeather(unsigned long now) {
  lastWeatherUpdate = now;
  if (!WiFi.isConnected()) {
    lastWeatherUpdate = now - 3600000UL + 30000UL;
    return;
  }
  if (!httpsLock(25000)) {
    Serial.println("[https] погоду відкладено: зайняті запити тривог");
    lastWeatherUpdate = now - 3600000UL + 120000UL;
    return;
  }

  {
    String url = "https://api.openweathermap.org/data/2.5/weather?q=" + String(cfg.weatherCity)
                 + "&lang=ua&units=metric&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream())) {
        char cond[24], tempBuf[16], srBuf[6], ssBuf[6], pressBuf[8], humBuf[8], windBuf[8], title[40];
        strlcpy(cond, doc["weather"][0]["main"] | "", sizeof(cond));
        float temp = doc["main"]["temp"] | 0.0f;
        float wind = doc["wind"]["speed"] | 0.0f;
        float press = doc["main"]["pressure"] | 0.0f;
        int hum = doc["main"]["humidity"] | 0;
        wxLat = doc["coord"]["lat"] | 0.0f;  // для запиту якості повітря
        wxLon = doc["coord"]["lon"] | 0.0f;
        wxHaveCoord = (wxLat != 0.0f || wxLon != 0.0f);
        time_t sr = (time_t)doc["sys"]["sunrise"].as<long long>() + currentTZOffset();
        time_t ss = (time_t)doc["sys"]["sunset"].as<long long>() + currentTZOffset();
        strftime(srBuf, sizeof(srBuf), "%H:%M", gmtime(&sr));
        strftime(ssBuf, sizeof(ssBuf), "%H:%M", gmtime(&ss));
        snprintf(tempBuf, sizeof(tempBuf), "%.1f C", temp);
        snprintf(pressBuf, sizeof(pressBuf), "%.0f", cfg.pressMm ? press * 0.750062f : press);  // мм рт.ст. або гПа
        snprintf(humBuf, sizeof(humBuf), "%d", hum);
        snprintf(windBuf, sizeof(windBuf), "%.1f", wind);
        snprintf(title, sizeof(title), "%s  ", cfg.weatherTitle);

        wxField(lastTemperature, sizeof(lastTemperature), tempBuf, 1, 10, YELLOW, title, "");
        wxField(lastSunrise, sizeof(lastSunrise), srBuf, TEMP_X, WX_Y_SUN, MAGENTA, "Схід: ", "");
        wxField(lastSunset, sizeof(lastSunset), ssBuf, TEMP_X + 70, WX_Y_SUN, MAGENTA, "Зах: ", "");
        wxField(lastPressure, sizeof(lastPressure), pressBuf, TEMP_X, WX_Y_PRESS, MAGENTA, "Тиск: ", cfg.pressMm ? " мм рт.ст." : " hPa");
        wxField(lastHumidity, sizeof(lastHumidity), humBuf, TEMP_X, WX_Y_HUM, WHITE, "Вологiсть: ", " %");
        wxField(lastWindSpeed, sizeof(lastWindSpeed), windBuf, TEMP_X, WX_Y_WIND, WHITE, "Вiтер: ", " м/с");

        if (strcmp(cond, lastWeatherCondition) != 0) {
          tft.fillRect(TEMP_X, TEMP_Y + 5, IMG_WIDTH, IMG_HEIGHT, BACKGROUND_COLOR);
          displayWeatherIcon(cond, TEMP_X + 1, TEMP_Y + 5, featuresIsNight());
          strlcpy(lastWeatherCondition, cond, sizeof(lastWeatherCondition));
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  {
    String url = "https://api.openweathermap.org/data/2.5/forecast?q=" + String(cfg.weatherCity)
                 + "&lang=ua&units=metric&cnt=48&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument filter;
      filter["list"][0]["dt"] = true;
      filter["list"][0]["main"]["temp"] = true;
      filter["list"][0]["weather"][0]["main"] = true;
      filter["list"][0]["sys"]["pod"] = true;

      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream(), DeserializationOption::Filter(filter))) {
        JsonArray list = doc["list"].as<JsonArray>();
        long tz = currentTZOffset();
        time_t nowLocal = (time_t)timeClient.getEpochTime();
        tm* todayTm = gmtime(&nowLocal);
        char todayStr[11];
        snprintf(todayStr, sizeof(todayStr), "%04d-%02d-%02d",
                 todayTm->tm_year + 1900, todayTm->tm_mon + 1, todayTm->tm_mday);

        char lastDate[11] = "";
        time_t nowUtc = nowLocal - tz;
        int hrFill = 0;
        hrCount = 0;
        int dayCount = 0;
        fcCount = 0;
        float allMin = 1000.0f, allMax = -1000.0f;
        float nightMin = 1000.0f, daytimeMax = -1000.0f;
        char dayCond[16] = "";
        int bestCondPrio = -1;
        time_t firstDtOfDay = 0;

        for (JsonObject item : list) {
          if (dayCount >= 5) break;
          time_t dt = (time_t)item["dt"].as<long long>();
          time_t localDt = dt + tz;
          tm* ltm = gmtime(&localDt);
          char dateBuf[11];
          snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                   ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday);

          if (hrFill < 5 && dt > nowUtc) {
            strlcpy(hrCache[hrFill].cond, item["weather"][0]["main"] | "", sizeof(hrCache[hrFill].cond));
            hrCache[hrFill].temp = (int)round(item["main"]["temp"].as<float>());
            hrCache[hrFill].hh = ltm->tm_hour;
            hrCache[hrFill].mm = ltm->tm_min;
            const char* partOfDay = item["sys"]["pod"] | "d";
            hrCache[hrFill].night = (partOfDay[0] == 'n');
            hrFill++;
            hrCount = hrFill;
          }

          if (strcmp(dateBuf, todayStr) == 0) continue;

          float tmp = item["main"]["temp"].as<float>();
          const char* partOfDay = item["sys"]["pod"] | "d";
          char cond[16];
          strlcpy(cond, item["weather"][0]["main"] | "", sizeof(cond));

          if (lastDate[0] != '\0' && strcmp(dateBuf, lastDate) != 0) {
            time_t firstLocal = firstDtOfDay + tz;
            tm* outTm = gmtime(&firstLocal);
            int nightValue = (nightMin < 999.0f) ? (int)round(nightMin) : (int)round(allMin);
            int dayValue = (daytimeMax > -999.0f) ? (int)round(daytimeMax) : (int)round(allMax);
            displayForecastItem(dayCount++, dayCond, nightValue, dayValue,
                                shortDayName(outTm->tm_wday));
            allMin = nightMin = 1000.0f;
            allMax = daytimeMax = -1000.0f;
            bestCondPrio = -1;
          }
          if (lastDate[0] == '\0' || strcmp(dateBuf, lastDate) != 0) {
            strlcpy(lastDate, dateBuf, sizeof(lastDate));
            firstDtOfDay = dt;
          }
          if (tmp < allMin) allMin = tmp;
          if (tmp > allMax) allMax = tmp;
          if (partOfDay[0] == 'n' && tmp < nightMin) nightMin = tmp;
          if (partOfDay[0] == 'd' && tmp > daytimeMax) daytimeMax = tmp;
          int prio = condPriority(cond);
          if (prio > bestCondPrio) {
            bestCondPrio = prio;
            strlcpy(dayCond, cond, sizeof(dayCond));
          }
        }

        if (dayCount < 5 && lastDate[0] != '\0') {
          time_t firstLocal = firstDtOfDay + tz;
          tm* outTm = gmtime(&firstLocal);
          int nightValue = (nightMin < 999.0f) ? (int)round(nightMin) : (int)round(allMin);
          int dayValue = (daytimeMax > -999.0f) ? (int)round(daytimeMax) : (int)round(allMax);
          displayForecastItem(dayCount, dayCond, nightValue, dayValue,
                              shortDayName(outTm->tm_wday));
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  // ── 3. ЯКІСТЬ ПОВІТРЯ (OpenWeatherMap: індекс AQI 1–5) ──
  if (wxHaveCoord) {
    String url = "https://api.openweathermap.org/data/2.5/air_pollution?lat=" + String(wxLat, 4)
                 + "&lon=" + String(wxLon, 4) + "&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream())) {
        int aqi = doc["list"][0]["main"]["aqi"] | 0;
        if (aqi >= 1 && aqi <= 5 && aqi != lastAqi) {
          lastAqi = aqi;
          drawAqiLine();
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  httpsUnlock();

  if (!lastTemperature[0] || fcCount == 0) {
    lastWeatherUpdate = now - 3600000UL + 30000UL;
  }
}

// =====================================================================
// SETUP ТА LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.printf("[boot] reset=%s (%d)\n", resetReasonText(), (int)esp_reset_reason());
  esp_task_wdt_config_t wdtConfig;
  wdtConfig.timeout_ms = 60000;
  wdtConfig.idle_core_mask = (1U << 0) | (1U << 1);
  wdtConfig.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdtConfig);

  httpsLockInit();
  configureTrustedTls(weatherTlsClient);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (!BUZZER_ACTIVE) ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS init failed!");
  }

  cfgLoad();
  notesLoad();
  loadBrightness();
  loadBg();
  loadAlertLog();
  loadRestartReason();

  ledcAttach(BL_PIN, 5000, 8);
  featuresBegin();
  applyBrightness();

  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);

  tft.begin(20000000);
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  tft.setRotation(1);
  tft.fillScreen(BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
  drawTextCentered(160, 128, "CODE BY VERT", CYAN, BACKGROUND_COLOR);
  drawBootConnectingDots(0);
 
  // ── СТАБІЛЬНИЙ СТАРТ WI-FI З МАКСИМАЛЬНОЮ ПОТУЖНІСТЮ ──
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Примусово вмикаємо максимальну потужність передавача для пробиття перешкод
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.println("[WiFi] Встановлено максимальну потужність TX: 19.5 dBm");

  // Пауза для стабілізації радіомодуля в ядрі 3.x
  delay(300);

  String lastGoodSsid, lastGoodPassword;
  if (wpLoadLastGoodNetwork(lastGoodSsid, lastGoodPassword)) {
    Serial.printf("[wifi] запуск з останньою робочою мережею SSID='%s'\n", lastGoodSsid.c_str());
    WiFi.begin(lastGoodSsid.c_str(), lastGoodPassword.c_str());
  } else {
    WiFi.begin();  // сумісність зі збереженою мережею до появи резервного запису
    String legacySsid = WiFi.SSID();
    if (legacySsid.length()) {
      wpSaveLastGoodNetwork(legacySsid, WiFi.psk());
      Serial.printf("[wifi] перенесено старі дані мережі SSID='%s' у резерв\n", legacySsid.c_str());
    }
  }

  unsigned long lastSplashAnim = millis();
  uint8_t splashDots = 0;
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < (WIFI_CONNECT_TIMEOUT * 1000UL)) {
    if (millis() - lastSplashAnim >= 450UL) {
      lastSplashAnim = millis();
      splashDots = (splashDots + 1) & 3;
      drawBootConnectingDots(splashDots);
    }
    delay(40);
  }
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    wpRememberConnectedNetwork();
    wifiRememberGoodPending = false;
  }

  if (!connected) {
    snprintf(bootPortalPass, sizeof(bootPortalPass), "W%08lX", (unsigned long)esp_random());
    drawWifiConfigScreen(bootPortalPass, false);
    wpStart(bootPortalPass);
    unsigned long t0 = millis();
    while (millis() - t0 < 600000UL) {
      if (wpLoop() == WP_OK) {
        wpRememberConnectedNetwork();
        wifiRememberGoodPending = false;
        connected = true;
        break;
      }
      delay(2);
    }
    wpStop();
  }

  if (!connected) {
    showNoInternet();
    delay(15000);
    restartWithReason("немає Wi-Fi при стартi");
  }

  ts.begin();
  ts.setRotation(1);

  timeClient.begin();
  timeClient.setTimeOffset(TZ_WINTER);
  timeClient.forceUpdate();

  cachedTZ = isUkraineSummerTime((time_t)timeClient.getEpochTime() - TZ_WINTER) ? TZ_SUMMER : TZ_WINTER;
  timeClient.setTimeOffset(cachedTZ);
  updateNTPTime();

  calDisplayMonth = month();
  calDisplayYear = currentYear;

  returnToMain();  // заставка замінюється головним екраном без порожньої паузи

  xTaskCreatePinnedToCore(alertTask, "alert", 16384, NULL, 1, &alertTaskHandle, 0);
  if (!cfgComplete()) {
    menuState = 7;
    drawSettingsQr();
  }
}

void loop() {
  delay(2);
  unsigned long now = millis();
  if (wifiRememberGoodPending) {
    wifiRememberGoodPending = false;
    wpRememberConnectedNetwork();
  }
  featuresTick(now);  // світлодіод, нічний режим, автояскравість
  webLoop();  // обслуговуємо веб навіть під час тривоги
  notesReminderTick(now);  // нагадування працюють на будь-якому відкритому екрані
  minuteSilenceTick(now);

  if (alarmActive) {
    beepTick();
    alarmTick(now);
    if (ts.touched()) {
      delay(80);
      while (ts.touched()) { webLoop(); delay(10); }
      alarmActive = false;
      applyBrightness();
      restoreAfterAlarm();
    }
    return;
  }


  if (wifiPortalActive) {
    wpLoop();
    bool timedOut = (millis() - wifiPortalStartedMs >= WIFI_MANUAL_PORTAL_TIMEOUT * 1000UL);
    if (wpState == WP_OK || timedOut) closeWifiConfigPortal();
  }

  loopCountTmp++;
  if (now - prevLoopMs > loopMaxTmp) loopMaxTmp = now - prevLoopMs;
  prevLoopMs = now;
  if (now - lastRateMs >= 1000) {
    loopRate = loopCountTmp;
    loopMaxGap = loopMaxTmp;
    loopCountTmp = 0;
    loopMaxTmp = 0;
    lastRateMs = now;
  }

  if (alertEvent && !wifiPortalActive && menuState != 7 && !webConfigBusy()) {
    int ev = alertEvent;
    alertEvent = 0;
    if (ev == 1) startBeep(BEEP_ALERT, 1);
    else if (!featuresIsNight()) startBeep(BEEP_CLEAR, 1);  // вночі відбій без звуку
    if (!wifiPortalActive) {
      startAlarmScreen(ev);
      return;  // ВАЖЛИВО: у цьому проході більше нічого не малюємо поверх екрана тривоги
    }
  }
  if (!alarmActive && !beepPat && !minuteSilenceActive) {
    if (notesBeepRequest) {
      notesBeepRequest = false;
      uint8_t repeats = constrain((int)notesBeepRepeatsRequest, 1, 3);
      notesBeepRepeatsRequest = 0;
      startBeep(BEEP_NOTE_REMINDER, repeats);
    } else if (wifiConnectedBeepPending) {
      wifiConnectedBeepPending = false;
      startBeep(BEEP_WIFI_CONNECTED, 1);
    }
  }
  beepTick();

  // Wi-Fi.status() під час TLS інколи бреше "disconnected". Рахуємо обрив лише після
  // кількох перевірок підряд, і не рвемо сокети через disconnect().
  static unsigned long lastWifiCheck = 0, wifiDownSince = 0;
  static bool wifiWasUp = true;
  static uint8_t wifiFailStreak = 0;
  if (!wifiPortalActive && now - lastWifiCheck >= 6000UL) {
    lastWifiCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      wifiFailStreak = 0;
      if (!wifiWasUp) {
        if (menuState == 7) {
          webStop();
          webStart();
        }
        Serial.println("[WiFi] Мережа відновлена.");
      }
      wifiDownSince = 0;
      wifiWasUp = true;
    } else if (++wifiFailStreak < 3) {
      Serial.printf("[WiFi] статус %d, ще не обрив (%u/3)\n", (int)WiFi.status(), wifiFailStreak);
    } else {
      if (wifiWasUp) {
        wifiDrops++;
        wifiWasUp = false;
        Serial.println("[WiFi] Зв'язок втрачено.");
      }
      if (wifiDownSince == 0) wifiDownSince = now;

      static unsigned long lastKick = 0;
      if (now - wifiDownSince > 30000UL && now - lastKick > 30000UL) {
        lastKick = now;
        WiFi.reconnect();
      }

      if (now - wifiDownSince > 600000UL) restartWithReason("Wi-Fi вiдсутнiй 10 хв");
    }
  }

  static unsigned long touchLockUntil = 0;
  if (now - lastTouchCheck >= 25 && (long)(now - touchLockUntil) >= 0) {
    lastTouchCheck = now;
    static int onCnt = 0, offCnt = 0;
    static bool stableTouched = false;

    if (ts.touched()) {
      onCnt++;
      offCnt = 0;
    } else {
      offCnt++;
      onCnt = 0;
    }
    if (!stableTouched && onCnt >= 2) stableTouched = true;
    if (stableTouched && offCnt >= 2) stableTouched = false;

    if (stableTouched) {
      TS_Point p = ts.getPoint();
      if (!touchTracking) {
        touchTracking = true;
        touchStartRawX = p.x;
        touchStartRawY = p.y;
      }
      touchCurrRawX = p.x;
      touchCurrRawY = p.y;

      if (menuState == 2) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        setBrightnessFromTouch(sx, sy);
      } else if (menuState == 3) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        if (sysLastSy < 0) sysLastSy = sy;
        if (abs(sy - sysLastSy) >= 3) {
          sysScroll -= (sy - sysLastSy);
          sysLastSy = sy;
          drawSystem();
        }
      } else if (menuState == 4) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        setBgFromTouch(sx, sy);
      }
    } else if (touchTracking) {
      touchTracking = false;
      int delta = touchCurrRawX - touchStartRawX;

      if (calendarFullscreen) {
        if (abs(delta) >= SWIPE_THRESHOLD) {
          if (delta > 0) {
            if (++calDisplayMonth > 12) {
              calDisplayMonth = 1;
              calDisplayYear++;
            }
          } else {
            if (--calDisplayMonth < 1) {
              calDisplayMonth = 12;
              calDisplayYear--;
            }
          }
          calNotesPage = 0;
          displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
        } else {
          int tapX, tapY;
          rawToScreen(touchCurrRawX, touchCurrRawY, tapX, tapY);
          int noteCount = notesCountForCalendarMonth(calDisplayMonth, calDisplayYear);
          if (tapY >= 203 && noteCount > 3) {
            int pageCount = (noteCount + 2) / 3;
            calNotesPage = (calNotesPage + 1) % pageCount;
            displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
          } else {
            calendarFullscreen = false;
            returnToMain();
          }
        }
      } else if (clockFullscreen) {
        int sx, sy;
        rawToScreen(touchCurrRawX, touchCurrRawY, sx, sy);
        if (inRect(sx, sy, 248, 0, 319, 38)) {
          clockAnalogView = !clockAnalogView;
          drawClockFull(true);
        } else {
          clockFullscreen = false;
          returnToMain();
        }
      } else if (alertHistScreen) {
        alertHistScreen = false;
        returnToMain();
      } else if (hourlyScreen) {
        hourlyScreen = false;
        returnToMain();
      } else if (forecastGraphScreen) {
        forecastGraphScreen = false;
        returnToMain();
      } else if (menuState != 0) {
        sysLastSy = -1;
        int sx, sy;
        rawToScreen(touchCurrRawX, touchCurrRawY, sx, sy);
        bool dragged = (abs(touchCurrRawY - touchStartRawY) > 250 || abs(delta) > 250);
        if (!(menuState == 3 && dragged)) handleMenuTap(sx, sy);
      } else if (abs(delta) < SWIPE_THRESHOLD) {
        int sx, sy;
        rawToScreen(touchStartRawX, touchStartRawY, sx, sy);
        if (inRect(sx, sy, MENU_X1, MENU_Y1, MENU_X2, MENU_Y2)) {
          menuState = 1;
          drawMenu();
        } else if (inRect(sx, sy, CAL_X1, CAL_Y1, CAL_X2, CAL_Y2)) {
          calDisplayMonth = month();
          calDisplayYear = currentYear;
          calendarFullscreen = true;
          calNotesPage = 0;
          displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
        } else if (inRect(sx, sy, CLK_X1, CLK_Y1, CLK_X2, CLK_Y2)) {
          clockFullscreen = true;
          cfTime[0] = '\0';
          cfSec[0] = '\0';
          cfDow[0] = '\0';
          cfDate[0] = '\0';
          tft.fillScreen(BACKGROUND_COLOR);
          drawClockFull(true);
        } else if (sy >= FORECAST_Y && fcCount > 0) {
          forecastGraphScreen = true;
          drawForecastGraph();
        } else if (inRect(sx, sy, WX_X1, WX_Y1, WX_X2, WX_Y2)) {
          hourlyScreen = true;
          drawHourly();
        } else if (inRect(sx, sy, ALERT_X1, ALERT_Y1, ALERT_X2, ALERT_Y2)) {
          alertHistScreen = true;
          drawAlertHist();
        }
      }
      touchLockUntil = millis() + 180;
    }
  }

  static unsigned long settingsSince = 0;
  if (menuState == 7 && cfgComplete()) {
    if (settingsSince == 0) settingsSince = now;
    else if (now - settingsSince > 300000UL) {
      settingsSince = 0;
      menuState = 0;
      webConfigSessionActive = false;
      lastWeatherUpdate = 0;
      returnToMain();
    }
  } else {
    settingsSince = 0;
  }

  if (cfgDirty) {
    cfgDirty = false;
    lastWeatherUpdate = 0;
    featuresRecheck();  // нічний режим / автояскравість застосувати одразу
    shownAlertState = -1;
    bool onMain = (menuState == 0 && !calendarFullscreen && !clockFullscreen && !alertHistScreen && !hourlyScreen && !forecastGraphScreen && !alarmActive);
    if (menuState == 7 && cfgComplete()) {
      menuState = 0;
      webConfigSessionActive = false;
      onMain = true;
    }
    if (onMain) returnToMain();
  }

  if (menuState == 3 && !touchTracking && now - lastSysDraw >= 5000) {
    lastSysDraw = now;
    drawSystem();
  }

  if (alertHistScreen && !touchTracking && now - lastSysDraw >= 15000) {
    lastSysDraw = now;
    drawAlertHist();
  }

  updateAlertMapIfNeeded();

  if (calendarFullscreen || menuState != 0 || hourlyScreen || alertHistScreen || forecastGraphScreen) return;

  if (clockFullscreen) {
    if (now - lastTimeDraw >= 200) {
      lastTimeDraw = now;
      if (!minuteSilenceActive) timeClient.update();
      drawClockFull(false);
    }
    return;
  }

  if (alertState != shownAlertState) drawAlertStatus();
notesTick(now);
animateMenuButton(now);
  if (timeClient.valid()) {
    if (!minuteSilenceActive && (now - lastNTPUpdate >= 60000UL || lastNTPUpdate == 0)) {
      updateNTPTime();
      lastNTPUpdate = now;
    }
    if (!minuteSilenceActive && (now - lastDSTCheck >= 3600000UL || lastDSTCheck == 0)) {
      updateDSTCache();
      lastDSTCheck = now;
    }
  }

  if (now - lastCalendarUpdate >= 360000UL) {
    displayCalendar();
    lastCalendarUpdate = now;
  }

  if (timeClient.valid() && now - lastTimeDraw >= 150) {
    lastTimeDraw = now;
    if (!minuteSilenceActive) timeClient.update();
    time_t localNow = (time_t)timeClient.getEpochTime();
    setTime(localNow);

    tm* tp = gmtime(&localNow);
    char h[3], m[3], s[3];
    snprintf(h, sizeof(h), "%02d", tp->tm_hour);
    snprintf(m, sizeof(m), "%02d", tp->tm_min);
    snprintf(s, sizeof(s), "%02d", tp->tm_sec);
    u8g2Fonts.setFont(u8g2_font_logisoso38_tn);

    static unsigned long dotsOffUntil = 0;
    static bool lastShowDots = true;
    bool secondsChanged = (strcmp(s, lastSeconds) != 0);
    if (secondsChanged) dotsOffUntil = millis() + 150UL;
    bool showDots = (long)(millis() - dotsOffUntil) >= 0;

    int digitW = u8g2Fonts.getUTF8Width("00");
    const int separatorWidth = 12;
    int startX = (320 - (3 * digitW + 2 * separatorWidth)) / 2;

    int hourX = startX;
    int colon1X = hourX + digitW;
    int minuteX = colon1X + separatorWidth;
    int colon2X = minuteX + digitW;
    int secondX = colon2X + separatorWidth;

    bool hourChanged = (strcmp(h, lastHours) != 0);
    bool minuteChanged = (strcmp(m, lastMinutes) != 0);

    if (hourChanged) {
      tft.fillRect(hourX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(hourX, TIME_Y, h, RED, BACKGROUND_COLOR);
      strcpy(lastHours, h);
    }

    if (hourChanged || minuteChanged) {
      tft.fillRect(colon1X, TIME_Y - 30, separatorWidth, 30, BACKGROUND_COLOR);
      tft.fillCircle(colon1X + 6, TIME_Y - 22, 2, RED);
      tft.fillCircle(colon1X + 6, TIME_Y - 10, 2, RED);
    }

    if (minuteChanged) {
      tft.fillRect(minuteX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(minuteX, TIME_Y, m, RED, BACKGROUND_COLOR);
      strcpy(lastMinutes, m);
    }

    if (secondsChanged) {
      tft.fillRect(secondX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(secondX, TIME_Y, s, ORANGE, BACKGROUND_COLOR);
      strcpy(lastSeconds, s);
    }

    if (secondsChanged || showDots != lastShowDots) {
      tft.fillRect(colon2X, TIME_Y - 30, separatorWidth, 30, BACKGROUND_COLOR);
      if (showDots) {
        tft.fillCircle(colon2X + 6, TIME_Y - 22, 2, RED);
        tft.fillCircle(colon2X + 6, TIME_Y - 10, 2, RED);
      }
      lastShowDots = showDots;
    }

    tm* ptm = gmtime(&localNow);
    const char* dayNames[] = { "Недiля", "Понедiлок", "Вiвторок", "Середа", "Четвер", "П'ятниця", "Субота" };
    const char* dow = dayNames[ptm->tm_wday];
      if (!notesTickerActive() && strcmp(dow, lastDayOfWeek) != 0) {
      u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
      tft.fillRect(110, 2, 120, 15, BACKGROUND_COLOR);
      int textWidth = u8g2Fonts.getUTF8Width(dow);
      drawTextCentered(160, 13, dow, DARKGREEN, BACKGROUND_COLOR);
      strlcpy(lastDayOfWeek, dow, sizeof(lastDayOfWeek));
    }
  }

  if (!webConfigBusy() && !minuteSilenceActive && (now - lastWeatherUpdate >= 3600000UL || lastWeatherUpdate == 0) && cfg.owmKey[0] && cfg.weatherCity[0] && timeClient.valid()) {
    updateWeather(now);
  }
}
