#pragma once
// =====================================================================
// Notes.h — замітки календаря (до 50): дні народження та важливі дати.
//  • вводяться на сторінці налаштувань → "Замітки календаря"
//  • зберігаються в пам'яті NVS (переживають перезавантаження і оновлення прошивки)
//  • у календарі на весь екран — внизу, до 3 рядків
//  • на головному екрані — рядок, що біжить замість дня тижня (якщо дата в межах 7 днів)
// Підключати в скетчі ПІСЛЯ WebConfig.h.
// =====================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>

#define NOTES_MAX 50       // скільки заміток
#define NOTES_NEAR_DAYS 3  // за скільки днів до дати рядок з'являється на головному екрані
#define TICK_X 96         // зона рядка, що біжить (на місці дня тижня)
#define TICK_Y 1
#define TICK_W 152
#define TICK_H 14

struct Note {
  uint8_t used;   // 1 — замітка заповнена
  uint8_t day;
  uint8_t month;
  uint8_t pad;
  uint16_t year;  // 0 — повторювати щороку (день народження)
  char text[64];
  // Додані в кінці, щоб зберегти сумісність початкових полів старих заміток.
  uint8_t reminderEnabled;
  uint8_t reminderHour;
  uint8_t reminderMinute;
  uint8_t reminderPad;
  uint32_t reminderFiredDay;  // YYYYMMDD — не повторювати після перезавантаження цього дня
  uint8_t reminderRepeats;    // скільки разів програти сигнал: 1–3
  uint8_t reminderReserved[3];
};

struct NoteLegacyV1 {
  uint8_t used;
  uint8_t day;
  uint8_t month;
  uint8_t pad;
  uint16_t year;
  char text[64];
};

struct NoteLegacyV2 {
  uint8_t used;
  uint8_t day;
  uint8_t month;
  uint8_t pad;
  uint16_t year;
  char text[64];
  uint8_t reminderEnabled;
  uint8_t reminderHour;
  uint8_t reminderMinute;
  uint8_t reminderPad;
  uint32_t reminderFiredDay;
};

static Note notes[NOTES_MAX];

extern Adafruit_ILI9341 tft;            // визначено в скетчі
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;  // визначено в скетчі
extern uint16_t bgColor;
extern char lastDayOfWeek[24];
extern volatile bool notesBeepRequest;
extern volatile uint8_t notesBeepRepeatsRequest;

// ---------- збереження ----------
inline void notesSave();

inline void notesLoad() {
  memset(notes, 0, sizeof(notes));
  Preferences p;
  if (!p.begin("notes", false)) return;
  size_t stored = p.getBytesLength("n");
  bool migrated = false;
  if (stored == sizeof(notes)) {
    p.getBytes("n", notes, sizeof(notes));
  } else if (stored == sizeof(NoteLegacyV2) * NOTES_MAX) {
    static NoteLegacyV2 oldNotes[NOTES_MAX];
    if (p.getBytes("n", oldNotes, sizeof(oldNotes)) == sizeof(oldNotes)) {
      for (int i = 0; i < NOTES_MAX; i++) {
        notes[i].used = oldNotes[i].used;
        notes[i].day = oldNotes[i].day;
        notes[i].month = oldNotes[i].month;
        notes[i].year = oldNotes[i].year;
        memcpy(notes[i].text, oldNotes[i].text, sizeof(notes[i].text));
        notes[i].reminderEnabled = oldNotes[i].reminderEnabled;
        notes[i].reminderHour = oldNotes[i].reminderHour;
        notes[i].reminderMinute = oldNotes[i].reminderMinute;
        notes[i].reminderFiredDay = oldNotes[i].reminderFiredDay;
        notes[i].reminderRepeats = 1;
      }
      migrated = true;
    }
  } else if (stored == sizeof(NoteLegacyV1) * NOTES_MAX) {
    static NoteLegacyV1 oldNotes[NOTES_MAX];
    if (p.getBytes("n", oldNotes, sizeof(oldNotes)) == sizeof(oldNotes)) {
      for (int i = 0; i < NOTES_MAX; i++) {
        notes[i].used = oldNotes[i].used;
        notes[i].day = oldNotes[i].day;
        notes[i].month = oldNotes[i].month;
        notes[i].year = oldNotes[i].year;
        memcpy(notes[i].text, oldNotes[i].text, sizeof(notes[i].text));
        notes[i].reminderHour = 9;
        notes[i].reminderRepeats = 1;
      }
      migrated = true;
    }
  }
  p.end();
  for (int i = 0; i < NOTES_MAX; i++) {
    notes[i].text[sizeof(notes[i].text) - 1] = '\0';
    if (notes[i].reminderHour > 23) notes[i].reminderHour = 9;
    if (notes[i].reminderMinute > 59) notes[i].reminderMinute = 0;
    if (notes[i].reminderRepeats < 1 || notes[i].reminderRepeats > 3) notes[i].reminderRepeats = 1;
  }
  if (migrated) notesSave();
}

inline void notesSave() {
  Preferences p;
  if (!p.begin("notes", false)) return;
  p.putBytes("n", notes, sizeof(notes));
  p.end();
}

// Викликається з loop незалежно від відкритого екрана.
inline void notesReminderTick(unsigned long now) {
  (void)now;
  int y = year(), m = month(), d = day();
  if (y < 2020 || m < 1 || m > 12 || d < 1 || d > 31) return;

  uint32_t dayKey = (uint32_t)y * 10000UL + (uint32_t)m * 100UL + (uint32_t)d;
  bool changed = false;
  uint8_t requestedRepeats = 1;
  for (int i = 0; i < NOTES_MAX; i++) {
    Note& n = notes[i];
    if (!n.used || !n.reminderEnabled || n.reminderFiredDay == dayKey) continue;
    bool dateMatches = n.month == m && n.day == d && (n.year == 0 || n.year == y);
    if (!dateMatches || hour() != n.reminderHour || minute() != n.reminderMinute) continue;
    n.reminderFiredDay = dayKey;
    if (n.reminderRepeats > requestedRepeats) requestedRepeats = n.reminderRepeats;
    changed = true;
  }
  if (changed) {
    notesSave();
    if (requestedRepeats > notesBeepRepeatsRequest) notesBeepRepeatsRequest = requestedRepeats;
    notesBeepRequest = true;
  }
}

// ---------- допоміжні ----------
// Шрифти екрана не мають деяких українських літер — замінюємо найближчими
static void notesScreenText(const char* in, char* out, size_t cap) {
  size_t w = 0;
  for (const char* r = in; *r && w + 2 < cap;) {
    unsigned char a = (unsigned char)r[0], b = (unsigned char)r[1];
    char c = 0;
    if (a == 0xD1 && b == 0x96) c = 'i';       // і
    else if (a == 0xD0 && b == 0x86) c = 'I';  // І
    else if (a == 0xD1 && b == 0x97) c = 'i';  // ї
    else if (a == 0xD0 && b == 0x87) c = 'I';  // Ї
    else if (a == 0xD1 && b == 0x94) c = 'e';  // є
    else if (a == 0xD0 && b == 0x84) c = 'E';  // Є
    else if (a == 0xD2 && b == 0x91) c = 'g';  // ґ
    else if (a == 0xD2 && b == 0x90) c = 'G';  // Ґ
    if (c) {
      out[w++] = c;
      r += 2;
    } else {
      out[w++] = *r++;
    }
  }
  out[w] = '\0';
}

static int notesDim(int y, int m) {
  if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

static time_t notesYmd(int y, int m, int d) {
  tmElements_t t;
  t.Year = CalendarYrToTm(y);
  t.Month = m;
  t.Day = d;
  t.Hour = 0;
  t.Minute = 0;
  t.Second = 0;
  return makeTime(t);
}

// Скільки днів до замітки (0 — сьогодні). Для одноразової, що минула, — від'ємне
static long notesDaysUntil(const Note& n) {
  time_t today0 = notesYmd(year(), month(), day());
  time_t t;
  if (n.year == 0) {  // щороку
    t = notesYmd(year(), n.month, n.day);
    if (t < today0) t = notesYmd(year() + 1, n.month, n.day);
  } else {
    t = notesYmd(n.year, n.month, n.day);
  }
  return (long)((t - today0) / 86400L);
}

struct NoteView {
  int idx;
  long days;
};

// Активні замітки, відсортовані за близькістю дати
static int notesCollect(NoteView* v) {
  int n = 0;
  for (int i = 0; i < NOTES_MAX; i++) {
    if (!notes[i].used) continue;
    long d = notesDaysUntil(notes[i]);
    if (notes[i].year != 0 && d < 0) continue;  // одноразова вже минула
    // У день нагадування прибираємо його з головного рядка після заданого часу.
    if (d == 0 && notes[i].reminderEnabled &&
        (hour() > notes[i].reminderHour ||
         (hour() == notes[i].reminderHour && minute() >= notes[i].reminderMinute))) continue;
    v[n].idx = i;
    v[n].days = d;
    n++;
  }
  for (int i = 1; i < n; i++) {  // сортування вставками
    NoteView x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j].days > x.days) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
  return n;
}

// ---------- календар на весь екран: рядки знизу ----------
inline int notesCountForCalendarMonth(int displayMonth, int displayYear) {
  int count = 0;

  for (int i = 0; i < NOTES_MAX; i++) {
    if (!notes[i].used) continue;
    if (notes[i].month != displayMonth) continue;

    // Рік 0 означає щорічну замітку.
    // Замітка з роком показується лише у відповідному році.
    if (notes[i].year != 0 && notes[i].year != displayYear) continue;

    count++;
  }

  return count;
}

inline void notesDrawCalendar(int displayMonth, int displayYear, int page) {
  int items[NOTES_MAX];
  int count = 0;

  for (int i = 0; i < NOTES_MAX; i++) {
    if (!notes[i].used) continue;
    if (notes[i].month != displayMonth) continue;
    if (notes[i].year != 0 && notes[i].year != displayYear) continue;

    items[count++] = i;
  }

  if (count == 0) return;

  // Сортуємо замітки місяця за числом.
  for (int i = 1; i < count; i++) {
    int item = items[i];
    int j = i - 1;

    while (j >= 0 && notes[items[j]].day > notes[item].day) {
      items[j + 1] = items[j];
      j--;
    }

    items[j + 1] = item;
  }

  int start = page * 3;
  if (start >= count) start = 0;
  int shown = count - start;
  if (shown > 3) shown = 3;

  tft.fillRect(0, 203, 320, 37, bgColor);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);

  int y = 238 - (shown - 1) * 12;

  for (int i = 0; i < shown; i++) {
    const Note& nt = notes[items[start + i]];
    char text[80];
    char line[110];

    notesScreenText(nt.text, text, sizeof(text));
    snprintf(line, sizeof(line), "%02d.%02d %s", nt.day, nt.month, text);

    u8g2Fonts.setBackgroundColor(bgColor);
    u8g2Fonts.setForegroundColor(0xFFE0);
    u8g2Fonts.setCursor(4, y);
    u8g2Fonts.print(line);

    y += 12;
  }
}

// ---------- головний екран: рядок, що біжить ----------
static GFXcanvas16* tkCanvas = nullptr;
static U8G2_FOR_ADAFRUIT_GFX tkFont;
static bool tkActive = false;
static char tkText[300] = "";
static uint16_t tkColor = 0x07FF;
static int tkTextW = 0;
static int tkOffset = 0;
static unsigned long tkLastFrame = 0, tkLastCalc = 0;

inline bool notesTickerActive() {
  return tkActive;
}

static bool notesTickerInit() {
  if (tkCanvas) return tkCanvas->getBuffer() != nullptr;
  tkCanvas = new GFXcanvas16(TICK_W, TICK_H);
  if (!tkCanvas || !tkCanvas->getBuffer()) return false;
  tkFont.begin(*tkCanvas);
  tkFont.setFont(u8g2_font_6x13_t_cyrillic);
  tkFont.setFontMode(1);
  return true;
}

static void notesBuildTicker() {
  NoteView v[NOTES_MAX];
  int n = notesCollect(v);
  char text[300] = "";
  bool any = false, today = false;
  for (int i = 0; i < n; i++) {
    if (v[i].days > NOTES_NEAR_DAYS) continue;
    char scr[80], part[130];
    notesScreenText(notes[v[i].idx].text, scr, sizeof(scr));
    if (v[i].days == 0) {
      snprintf(part, sizeof(part), "Сьогодні: %s", scr);
      today = true;
    } else if (v[i].days == 1) {
      snprintf(part, sizeof(part), "Завтра: %s", scr);
    } else {
      snprintf(part, sizeof(part), "Через %ld дн.: %s", v[i].days, scr);
    }
    if (strlen(text) + strlen(part) + 8 >= sizeof(text)) break;
    if (any) strlcat(text, "   |   ", sizeof(text));
    strlcat(text, part, sizeof(text));
    any = true;
  }

  if (any && !notesTickerInit()) any = false;  // не вистачило пам'яті — лишаємо день тижня

  if (!any && tkActive) {  // рядок зник: стираємо зону і повертаємо день тижня
    tft.fillRect(TICK_X, TICK_Y, TICK_W, TICK_H, bgColor);
    lastDayOfWeek[0] = '\0';
  }
  if (any && strcmp(text, tkText) != 0) {
    strlcpy(tkText, text, sizeof(tkText));
    tkTextW = tkFont.getUTF8Width(tkText);
    tkOffset = 0;
  }
  tkColor = today ? 0xFFFF : 0x07FF;  // жовтий — сьогодні, блакитний — найближчими днями
  tkActive = any;
}

// Викликати з loop(), коли показано ГОЛОВНИЙ екран
inline void notesTick(unsigned long now) {
  if (tkLastCalc == 0 || now - tkLastCalc >= 10000UL) {
    tkLastCalc = now ? now : 1;
    notesBuildTicker();
  }
  if (!tkActive) return;
  bool scrolling = tkTextW > TICK_W;
  if (now - tkLastFrame < (scrolling ? 60UL : 1000UL)) return;
  tkLastFrame = now;

  tkCanvas->fillScreen(bgColor);
  tkFont.setBackgroundColor(bgColor);
  tkFont.setForegroundColor(tkColor);
  if (!scrolling) {
    tkFont.setCursor((TICK_W - tkTextW) / 2, 10);
    tkFont.print(tkText);
  } else {
    const int gap = 60;
    tkFont.setCursor(-tkOffset, 10);
    tkFont.print(tkText);
    tkFont.setCursor(-tkOffset + tkTextW + gap, 10);
    tkFont.print(tkText);
    tkOffset += 3;
    if (tkOffset >= tkTextW + gap) tkOffset = 0;
  }
  tft.drawRGBBitmap(TICK_X, TICK_Y, tkCanvas->getBuffer(), TICK_W, TICK_H);
}

// ---------- сторінка для телефона ----------
static String notesEsc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '&') o += "&amp;";
    else if (*s == '<') o += "&lt;";
    else if (*s == '>') o += "&gt;";
    else if (*s == '"') o += "&quot;";
    else if ((unsigned char)*s == 39) o += "&#39;";
    else o += *s;
  }
  return o;
}

static void notesPage() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  int count = 0;
  for (int i = 0; i < NOTES_MAX; i++) if (notes[i].used) count++;
  String p;
  p.reserve(1800 + count * 220);
  p += F("<!doctype html><html lang=uk><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'><title>&#1047;&#1072;&#1084;&#1110;&#1090;&#1082;&#1080;</title><style>"
         ":root{color-scheme:dark}body{font:16px system-ui,sans-serif;background:#0b1020;color:#e8ecf5;margin:0 auto;padding:16px;max-width:560px}"
         "h1{font-size:22px}.card{background:#101733;border-radius:14px;padding:12px 14px;margin-top:10px}"
         "small{color:#7d8ab0}.date{color:#8fb4ff;font-size:14px}.note{margin:8px 0 12px;overflow-wrap:anywhere}"
         "a{color:#8fb4ff}.actions{display:flex;gap:12px;align-items:center}.del{margin:0}.del button{margin:0;padding:8px 12px;width:auto;background:#652b35;font-size:14px}"
         ".add{display:block;text-align:center;margin:16px 0;padding:14px;border-radius:12px;background:#2f6bff;color:white;text-decoration:none;font-weight:600}"
         "</style></head><body><h1>&#1047;&#1072;&#1084;&#1110;&#1090;&#1082;&#1080; &#1082;&#1072;&#1083;&#1077;&#1085;&#1076;&#1072;&#1088;&#1103;</h1>"
         "<small>&#1059; &#1082;&#1072;&#1083;&#1077;&#1085;&#1076;&#1072;&#1088;&#1110; &#1087;&#1086;&#1082;&#1072;&#1079;&#1091;&#1102;&#1090;&#1100;&#1089;&#1103; 3 &#1085;&#1072;&#1081;&#1073;&#1083;&#1080;&#1078;&#1095;&#1110; &#1079;&#1072;&#1084;&#1110;&#1090;&#1086;&#1082;.</small>");
  for (int i = 0; i < NOTES_MAX; i++) {
    if (!notes[i].used) continue;
    char date[20];
    if (notes[i].year)
      snprintf(date, sizeof(date), "%02d.%02d.%04d", notes[i].day, notes[i].month, notes[i].year);
    else
      snprintf(date, sizeof(date), "%02d.%02d", notes[i].day, notes[i].month);
    p += F("<div class=card><div class=date>");
    p += date;
    p += F("</div>");
    if (notes[i].reminderEnabled) {
      char reminderTime[6];
      snprintf(reminderTime, sizeof(reminderTime), "%02d:%02d", notes[i].reminderHour, notes[i].reminderMinute);
      p += F("<div class=date>&#128276; ");
      p += reminderTime;
      if (notes[i].reminderRepeats > 1) {
        p += F(" ×");
        p += String(notes[i].reminderRepeats);
      }
      p += F("</div>");
    }
    p += F("<div class=note>");
    p += notesEsc(notes[i].text);
    p += F("</div><div class=actions><a href='/notes/edit?pin=");
    p += cfg.pin;
    p += F("&id=");
    p += i;
    p += F("'>&#1056;&#1077;&#1076;&#1072;&#1075;&#1091;&#1074;&#1072;&#1090;&#1080;</a><form class=del method=post action=/notes/delete onsubmit=\"return confirm('&#1042;&#1080;&#1076;&#1072;&#1083;&#1080;&#1090;&#1080; &#1079;&#1072;&#1084;&#1110;&#1090;&#1082;&#1091;?')\"><input type=hidden name=pin value='");
    p += cfg.pin;
    p += F("'><input type=hidden name=id value='");
    p += i;
    p += F("'><button type=submit>&#1042;&#1080;&#1076;&#1072;&#1083;&#1080;&#1090;&#1080;</button></form></div></div>");
  }
  if (count == 0) p += F("<p class=card>&#1047;&#1072;&#1084;&#1110;&#1090;&#1086;&#1082; &#1097;&#1077; &#1085;&#1077;&#1084;&#1072;&#1108;.</p>");
  p += F("<a class=add href='/notes/edit?pin=");
  p += cfg.pin;
  p += F("&id=-1'>&#65291; &#1044;&#1086;&#1076;&#1072;&#1090;&#1080; &#1079;&#1072;&#1084;&#1110;&#1090;&#1082;&#1091;</a><p><a href='/?pin=");
  p += cfg.pin;
  p += F("'>&#1055;&#1072;&#1088;&#1072;&#1084;&#1077;&#1090;&#1088;&#1080;</a></p><script>setInterval(()=>fetch('/api/status'+location.search).catch(()=>{}),30000);</script></body></html>");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", p);
}

static void notesEditPage() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  int idx = webServer.arg("id").toInt();
  if (idx == -1) {
    for (idx = 0; idx < NOTES_MAX && notes[idx].used; idx++) {}
  }
  if (idx < 0 || idx >= NOTES_MAX) {
    webServer.send(409, "text/plain; charset=utf-8", "No free note slots");
    return;
  }
  const Note& n = notes[idx];
  char date[16] = "";
  if (n.used) snprintf(date, sizeof(date), "%04d-%02d-%02d", n.year ? n.year : year(), n.month, n.day);
  char reminderTime[6] = "09:00";
  if (n.used && n.reminderEnabled)
    snprintf(reminderTime, sizeof(reminderTime), "%02d:%02d", n.reminderHour, n.reminderMinute);
  int reminderRepeats = n.used ? constrain((int)n.reminderRepeats, 1, 3) : 1;

  String p;
  p.reserve(2600);
  p += F("<!doctype html><html lang=uk><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>&#1047;&#1072;&#1084;&#1110;&#1090;&#1082;&#1072;</title><style>:root{color-scheme:dark}body{font:16px system-ui,sans-serif;background:#0b1020;color:#e8ecf5;margin:0 auto;padding:16px;max-width:560px}"
         "h1{font-size:22px}.card{background:#101733;border-radius:14px;padding:6px 14px 14px;margin-top:12px}label{display:block;margin:12px 0 4px;font-size:13px;color:#9aa7c7}"
         "input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #2a3558;background:#121a33;color:#fff;font-size:16px}input[type=checkbox]{width:auto;vertical-align:middle;margin-right:8px}"
         "button{margin-top:18px;width:100%;padding:15px;border:0;border-radius:12px;background:#2f6bff;color:#fff;font-size:17px;font-weight:600}a{color:#8fb4ff}</style></head><body>");
  p += n.used ? F("<h1>&#1056;&#1077;&#1076;&#1072;&#1075;&#1091;&#1074;&#1072;&#1090;&#1080; &#1079;&#1072;&#1084;&#1110;&#1090;&#1082;&#1091;</h1>") : F("<h1>&#1053;&#1086;&#1074;&#1072; &#1079;&#1072;&#1084;&#1110;&#1090;&#1082;&#1072;</h1>");
  p += F("<form class=card method=post action=/notes/save><input type=hidden name=pin value='");
  p += cfg.pin;
  p += F("'><input type=hidden name=id value='");
  p += idx;
  p += F("'><label>&#1044;&#1072;&#1090;&#1072;</label><input type=date name=date required value='");
  p += date;
  p += F("'><label>&#1063;&#1072;&#1089; &#1085;&#1072;&#1075;&#1072;&#1076;&#1091;&#1074;&#1072;&#1085;&#1085;&#1103;</label><input type=time name=reminderTime value='");
  p += reminderTime;
  p += F("'><label><input type=checkbox name=remind value=1");
  if (n.used && n.reminderEnabled) p += F(" checked");
  p += F(">&#1059;&#1074;&#1110;&#1084;&#1082;&#1085;&#1091;&#1090;&#1080; &#1079;&#1074;&#1091;&#1082;&#1086;&#1074;&#1077; &#1085;&#1072;&#1075;&#1072;&#1076;&#1091;&#1074;&#1072;&#1085;&#1085;&#1103;</label>");
  p += F("<label>&#1050;&#1110;&#1083;&#1100;&#1082;&#1110;&#1089;&#1090;&#1100; &#1089;&#1080;&#1075;&#1085;&#1072;&#1083;&#1110;&#1074;</label><select name=reminderRepeats><option value=1");
  if (reminderRepeats == 1) p += F(" selected");
  p += F(">1 &#1088;&#1072;&#1079;</option><option value=2");
  if (reminderRepeats == 2) p += F(" selected");
  p += F(">2 &#1088;&#1072;&#1079;&#1080;</option><option value=3");
  if (reminderRepeats == 3) p += F(" selected");
  p += F(">3 &#1088;&#1072;&#1079;&#1080;</option></select>"
         "<label>&#1055;&#1086;&#1074;&#1090;&#1086;&#1088;&#1102;&#1074;&#1072;&#1090;&#1080; &#1076;&#1072;&#1090;&#1091;</label><select name=repeat><option value=1");
  if (!n.used || n.year == 0) p += F(" selected");
  p += F(">&#1065;&#1086;&#1088;&#1086;&#1082;&#1091; (&#1076;&#1077;&#1085;&#1100; &#1085;&#1072;&#1088;&#1086;&#1076;&#1078;&#1077;&#1085;&#1085;&#1103;)</option><option value=0");
  if (n.used && n.year != 0) p += F(" selected");
  p += F(">&#1054;&#1076;&#1080;&#1085; &#1088;&#1072;&#1079;</option></select><label>&#1058;&#1077;&#1082;&#1089;&#1090; (&#1076;&#1086; 30 &#1089;&#1080;&#1084;&#1074;&#1086;&#1083;&#1110;&#1074;)</label><input name=text maxlength=30 required value='");
  p += notesEsc(n.text);
  p += F("'><button type=submit>&#1047;&#1073;&#1077;&#1088;&#1077;&#1075;&#1090;&#1080;</button></form><p><a href='/notes?pin=");
  p += cfg.pin;
  p += F("'>&#1053;&#1072;&#1079;&#1072;&#1076; &#1076;&#1086; &#1079;&#1072;&#1084;&#1110;&#1090;&#1086;&#1082;</a></p><script>setInterval(()=>fetch('/api/status'+location.search).catch(()=>{}),30000);</script></body></html>");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", p);
}

static void notesSaveHandler() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  int idx = webServer.arg("id").toInt();
  String date = webServer.arg("date");
  String text = webServer.arg("text");
  String reminderTime = webServer.arg("reminderTime");
  bool reminderEnabled = webServer.arg("remind") == "1";
  int reminderRepeats = webServer.arg("reminderRepeats").toInt();
  if (reminderRepeats == 0) reminderRepeats = 1;  // сумісність зі старою відкритою формою
  text.trim();
  int y = 0, m = 0, d = 0;
  int reminderHour = 9, reminderMinute = 0;
  bool reminderTimeValid = true;
  if (reminderEnabled) {
    reminderTimeValid = reminderTime.length() == 5 &&
                        sscanf(reminderTime.c_str(), "%d:%d", &reminderHour, &reminderMinute) == 2 &&
                        reminderHour >= 0 && reminderHour <= 23 && reminderMinute >= 0 && reminderMinute <= 59;
  }
  bool valid = idx >= 0 && idx < NOTES_MAX &&
               sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) == 3 &&
               y >= 2000 && y <= 2100 && m >= 1 && m <= 12 &&
               d >= 1 && d <= notesDim(y, m) && text.length() > 0 && reminderTimeValid &&
               reminderRepeats >= 1 && reminderRepeats <= 3;
  if (!valid) {
    webServer.send(400, "text/plain; charset=utf-8", "Invalid note");
    return;
  }
  memset(&notes[idx], 0, sizeof(Note));
  notes[idx].used = 1;
  notes[idx].day = (uint8_t)d;
  notes[idx].month = (uint8_t)m;
  notes[idx].year = webServer.arg("repeat").toInt() ? 0 : (uint16_t)y;
  notes[idx].reminderEnabled = reminderEnabled ? 1 : 0;
  notes[idx].reminderHour = (uint8_t)reminderHour;
  notes[idx].reminderMinute = (uint8_t)reminderMinute;
  notes[idx].reminderRepeats = (uint8_t)reminderRepeats;
  cfgCopyUtf8(notes[idx].text, sizeof(notes[idx].text), text.c_str());
  notesSave();
  tkLastCalc = 0;
  String url = String("/notes?pin=") + cfg.pin;
  webServer.sendHeader("Location", url, true);
  webServer.send(303, "text/plain", "Saved");
}

static void notesDeleteHandler() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  int idx = webServer.arg("id").toInt();
  if (idx < 0 || idx >= NOTES_MAX || !notes[idx].used) {
    webServer.send(400, "text/plain; charset=utf-8", "Invalid note");
    return;
  }
  memset(&notes[idx], 0, sizeof(Note));
  notesSave();
  tkLastCalc = 0;
  String url = String("/notes?pin=") + cfg.pin;
  webServer.sendHeader("Location", url, true);
  webServer.send(303, "text/plain", "Deleted");
}

// Registered from webStart() in WebConfig.h.
void notesWebRoutes() {
  webServer.on("/notes", HTTP_GET, notesPage);
  webServer.on("/notes/edit", HTTP_GET, notesEditPage);
  webServer.on("/notes/save", HTTP_POST, notesSaveHandler);
  webServer.on("/notes/delete", HTTP_POST, notesDeleteHandler);
}
