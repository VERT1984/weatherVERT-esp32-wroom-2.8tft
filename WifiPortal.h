#pragma once
// =====================================================================
// WifiPortal.h — власний портал підключення до Wi-Fi.
// Точка доступу з паролем (він у QR-коді) + DNS "усе на 192.168.4.1" + проста сторінка:
// телефон сам відкриває її як "вхід у мережу".
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

#define WP_AP_NAME "ESP32-WeatherStation"
#define WP_TRY_TIMEOUT_MS 20000UL

enum { WP_IDLE = 0, WP_TRYING = 1, WP_OK = 2, WP_FAIL = 3 };

static DNSServer wpDns;
static WebServer wpServer(80);
static bool wpRoutesSet = false;
static volatile int wpState = WP_IDLE;
static unsigned long wpTryStart = 0;
static String wpNets[14];
static int wpNetCount = 0;
static String wpOpenSsid = "";
static int wpOpenRssi = -128;
static bool wpScanPending = false;
static unsigned long wpLastScanStart = 0;
static unsigned long wpRecoveryWindowStartMs = 0;
static unsigned long wpNextRecoveryMs = 0;
static bool wpOpenFallbackTried = false;
static bool wpAutoOpenAttempt = false;
static bool wpAutoRecoveryAttempt = false;

inline bool wpLoadLastGoodNetwork(String& ssid, String& password) {
  Preferences p;
  if (!p.begin("wifigood", true)) return false;
  ssid = p.getString("ssid", "");
  password = p.getString("pass", "");
  p.end();
  return ssid.length() > 0;
}

inline void wpSaveLastGoodNetwork(const String& ssid, const String& password) {
  if (!ssid.length()) return;
  Preferences p;
  if (!p.begin("wifigood", false)) return;
  bool same = p.getString("ssid", "") == ssid && p.getString("pass", "") == password;
  if (!same) {
    p.putString("ssid", ssid);
    p.putString("pass", password);
  }
  p.end();
}

inline void wpRememberConnectedNetwork() {
  if (wpAutoOpenAttempt || WiFi.status() != WL_CONNECTED) return;
  wpSaveLastGoodNetwork(WiFi.SSID(), WiFi.psk());
}

static void wpStopScanForConnect() {
  if (!wpScanPending) return;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
  WiFi.scanDelete();
  wpScanPending = false;
}

static void wpBeginConnection(const String& ssid, const String& password, bool openFallback, bool automatic) {
  wpStopScanForConnect();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(250);
  wpAutoOpenAttempt = openFallback;
  wpAutoRecoveryAttempt = automatic;
  Serial.printf("[wifi] спроба підключення SSID='%s'%s\n", ssid.c_str(), openFallback ? " (відкрита мережа)" : "");
  WiFi.begin(ssid.c_str(), password.c_str());
  wpState = WP_TRYING;
  wpTryStart = millis();
}

static void wpStartScan() {
  if (wpScanPending || wpState == WP_TRYING) return;
  WiFi.scanDelete();
  int result = WiFi.scanNetworks(true);
  wpLastScanStart = millis();
  wpScanPending = (result == WIFI_SCAN_RUNNING || result >= 0);
  if (!wpScanPending && result < 0) {
    Serial.printf("[wifi-scan] could not start: result=%d status=%d mode=%d\n",
                  result, (int)WiFi.status(), (int)WiFi.getMode());
  }
}

static String wpHtmlEsc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

// Результати фонового сканування (перевіряємо з wpLoop, нічого не блокує)
static void wpCollectScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  wpScanPending = false;
  if (n < 0) {
    WiFi.scanDelete();
    Serial.printf("[wifi] scan failed: %d\n", n);
    return;  // Повторна спроба запускається з wpLoop.
  }
  wpNetCount = 0;
  wpOpenSsid = "";
  wpOpenRssi = -128;
  for (int i = 0; i < n && wpNetCount < 14; i++) {
    String s = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    uint8_t auth = WiFi.encryptionType(i);
    Serial.printf("[wifi-scan] %02d SSID='%s' RSSI=%d dBm channel=%d auth=%d\n",
                  i + 1, s.length() ? s.c_str() : "<hidden>", rssi,
                  WiFi.channel(i), (int)auth);
    if (s.length() && auth == WIFI_AUTH_OPEN && rssi > wpOpenRssi) {
      wpOpenSsid = s;
      wpOpenRssi = rssi;
    }
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < wpNetCount; j++)
      if (wpNets[j] == s) dup = true;
    if (!dup) wpNets[wpNetCount++] = s;
  }
  Serial.printf("[wifi-scan] visible networks: %d\n", wpNetCount);
  WiFi.scanDelete();
}

static void wpRedirect() {
  String url = String("http://") + WiFi.softAPIP().toString() + "/";
  wpServer.sendHeader("Location", url, true);
  wpServer.sendHeader("Cache-Control", "no-store");
  wpServer.send(302, "text/plain", "");
}

static const char WP_HEAD[] PROGMEM =
  "<!doctype html><html lang=uk><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>Wi-Fi</title><style>"
  "body{font:17px system-ui,sans-serif;background:#0b1020;color:#e8ecf5;margin:0 auto;padding:18px;max-width:520px}"
  "h1{font-size:21px}label{display:block;margin:14px 0 5px;font-size:13px;color:#9aa7c7}"
  "input,select{width:100%;box-sizing:border-box;padding:13px;border-radius:10px;border:1px solid #2a3558;background:#121a33;color:#fff;font-size:17px}"
  "button{margin-top:24px;width:100%;padding:15px;border:0;border-radius:12px;background:#2f6bff;color:#fff;font-size:18px;font-weight:600}"
  "a{color:#8fb4ff}</style></head><body>";

static void wpRoot() {
  if (wpServer.hasArg("scan")) wpStartScan();
  String p;
  p.reserve(3600);
  p += FPSTR(WP_HEAD);
  p += "<h1>Підключення до Wi-Fi</h1><form method=post action=/connect>";
  p += "<label>Оберіть вашу мережу</label><select name=s>";
  if (wpNetCount == 0) p += "<option value=\"\">&#1052;&#1077;&#1088;&#1077;&#1078;&#1110; &#1097;&#1077; &#1085;&#1077; &#1079;&#1085;&#1072;&#1081;&#1076;&#1077;&#1085;&#1086;</option>";
  for (int i = 0; i < wpNetCount; i++) {
    String e = wpHtmlEsc(wpNets[i]);
    p += "<option value=\"" + e + "\">" + e + "</option>";
  }
  p += "<option value=\"\">— інша мережа (впишіть нижче) —</option></select>";
  p += "<p><button type=button onclick='wpScan()' style='margin:4px 0;padding:10px;font-size:15px'>&#1054;&#1085;&#1086;&#1074;&#1080;&#1090;&#1080; &#1089;&#1087;&#1080;&#1089;&#1086;&#1082; &#1084;&#1077;&#1088;&#1077;&#1078;</button> <span id=scanmsg></span></p>";
  p += "<label>Назва мережі, якщо її немає у списку</label><input name=m autocapitalize=off>";
  p += "<label>Пароль Wi-Fi</label><input name=p type=password autocapitalize=off>";
  p += "<button type=submit>Підключити</button></form><script>"
       "const sel=document.querySelector('select[name=s]'),msg=document.getElementById('scanmsg');"
       "function poll(){fetch('/scanstate').then(r=>r.json()).then(d=>{"
       "let old=sel.value;if(!d.pending){sel.innerHTML='';"
       "let first=document.createElement('option');first.value='';first.textContent=d.networks.length?'— оберіть мережу —':'Мережі не знайдено';sel.append(first);"
       "d.networks.forEach(n=>{let o=document.createElement('option');o.value=n;o.textContent=n;sel.append(o)});"
       "let other=document.createElement('option');other.value='';other.textContent='— інша мережа (впишіть нижче) —';sel.append(other);"
       "if([...sel.options].some(o=>o.value===old&&old!==''))sel.value=old;msg.textContent='';"
       "}else msg.textContent='Шукаю мережі…';setTimeout(poll,d.pending?1200:10000)"
       "}).catch(()=>setTimeout(poll,3000))}"
       "function wpScan(){msg.textContent='Шукаю мережі…';fetch('/scan').then(()=>poll())}poll();"
       "</script></body></html>";
  wpServer.send(200, "text/html; charset=utf-8", p);
}

static String wpJsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static void wpScanStateJson() {
  String p = "{\"pending\":";
  p += wpScanPending ? "true" : "false";
  p += ",\"networks\":[";
  for (int i = 0; i < wpNetCount; i++) {
    if (i) p += ',';
    p += '"'; p += wpJsonEsc(wpNets[i]); p += '"';
  }
  p += "]}";
  wpServer.sendHeader("Cache-Control", "no-store");
  wpServer.send(200, "application/json; charset=utf-8", p);
}

static void wpScanRequest() {
  wpStartScan();
  wpScanStateJson();
}

static void wpConnect() {
  // Вибрана мережа зі списку має пріоритет; ручне поле читаємо лише для "іншої мережі".
  String ssid = wpServer.arg("s");
  if (ssid.length() == 0) {
    ssid = wpServer.arg("m");
    ssid.trim();
  }
  String pwd = wpServer.arg("p");
  if (ssid.length() == 0 || ssid.length() > 32 || pwd.length() > 63) {
    wpRedirect();
    return;
  }
  // Після ручної спроби даємо користувачу час; потім портал повернеться до останньої робочої мережі.
  wpRecoveryWindowStartMs = millis();
  wpNextRecoveryMs = wpRecoveryWindowStartMs + 90000UL;
  wpOpenFallbackTried = false;
  wpAutoOpenAttempt = false;
  Serial.printf("[wifi] форма підключення: SSID='%s'\n", ssid.c_str());
  wpBeginConnection(ssid, pwd, false, false);  // пароль у штатному NVS Wi-Fi, добра мережа — окремо як резерв

  String p;
  p += FPSTR(WP_HEAD);
  p += "<h1 id=t>Підключаю…</h1><p id=m>Зачекайте до 20 секунд.</p>"
       "<script>function f(t,e){document.getElementById('t').textContent=t;"
       "document.getElementById('m').innerHTML=e;}"
       "function q(){fetch('/state').then(r=>r.json()).then(d=>{"
       "if(d.s==2)f('Підключено ✓','Дивіться на екран пристрою — там з\\'явиться QR для налаштувань.');"
       "else if(d.s==3)f('Не вдалось','Перевірте пароль. <a href=/>Спробувати ще раз</a>');"
       "else setTimeout(q,1500);}).catch(()=>f('Готово ✓','Схоже, пристрій уже підключився. Дивіться на його екран.'));}"
       "setTimeout(q,2000);</script></body></html>";
  wpServer.send(200, "text/html; charset=utf-8", p);
}

static void wpStateJson() {
  String s = "{\"s\":";
  s += (int)wpState;
  s += "}";
  wpServer.send(200, "application/json", s);
}

inline void wpStart(const char* apPass) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);  // зупиняємо попереднє підключення перед портал-скануванням
    delay(150);
  }
  WiFi.mode(WIFI_AP_STA);
  wpRecoveryWindowStartMs = millis();
  wpNextRecoveryMs = wpRecoveryWindowStartMs + 90000UL;
  wpOpenFallbackTried = false;
  wpAutoOpenAttempt = false;
  wpNetCount = 0;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(WP_AP_NAME, apPass);
  delay(200);

  wpDns.setErrorReplyCode(DNSReplyCode::NoError);
  wpDns.start(53, "*", WiFi.softAPIP());

  if (!wpRoutesSet) {
    wpServer.on("/", HTTP_GET, wpRoot);
    wpServer.on("/connect", HTTP_POST, wpConnect);
    wpServer.on("/state", HTTP_GET, wpStateJson);
    wpServer.on("/scan", HTTP_GET, wpScanRequest);
    wpServer.on("/scanstate", HTTP_GET, wpScanStateJson);
    // адреси, якими телефони та комп'ютери перевіряють "чи є інтернет" → показуємо нашу сторінку
    const char* probes[] = { "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                             "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html", "/success.txt" };
    for (const char* u : probes) wpServer.on(u, HTTP_ANY, wpRedirect);
    wpServer.onNotFound(wpRedirect);
    wpRoutesSet = true;
  }
  wpServer.begin();
  wpState = WP_IDLE;
  wpStartScan();  // фонове сканування: екран одразу реагує на дотик
  Serial.printf("[wifi] портал запущено: %s, IP %s, мереж у списку: %d\n",
                WP_AP_NAME, WiFi.softAPIP().toString().c_str(), wpNetCount);
}

// Викликати часто (з loop). Повертає поточний стан підключення.
inline int wpLoop() {
  wpDns.processNextRequest();
  wpServer.handleClient();
  if (wpScanPending) wpCollectScan();
  else if (millis() - wpLastScanStart >= 30000UL) wpStartScan();
  if (wpState == WP_TRYING) {
    if (WiFi.status() == WL_CONNECTED) {
      wpState = WP_OK;
      WiFi.setAutoReconnect(true);
      wpAutoRecoveryAttempt = false;
      Serial.printf("[wifi] підключено до %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    } else if (millis() - wpTryStart > WP_TRY_TIMEOUT_MS) {
      bool failedAutomatically = wpAutoRecoveryAttempt;
      wpState = WP_FAIL;
      WiFi.disconnect(false);
      wpAutoOpenAttempt = false;
      wpAutoRecoveryAttempt = false;
      if (failedAutomatically) {
        unsigned long elapsed = millis() - wpRecoveryWindowStartMs;
        unsigned long untilOpenFallback = (elapsed < 120000UL) ? (120000UL - elapsed) : 120000UL;
        wpNextRecoveryMs = millis() + min(untilOpenFallback, 120000UL);
      } else {
        wpNextRecoveryMs = millis() + 90000UL;
      }
      Serial.println("[wifi] спроба не вдалася; портал лишається активним");
    }
  }

  // Після 90 секунд без успішного налаштування повертаємо останню мережу,
  // що раніше успішно отримувала IP. Після її невдачі пробуємо найсильнішу відкриту.
  unsigned long now = millis();
  if (wpState != WP_TRYING && wpState != WP_OK && WiFi.status() != WL_CONNECTED &&
      wpNextRecoveryMs && (long)(now - wpNextRecoveryMs) >= 0) {
    String goodSsid, goodPassword;
    bool haveGood = wpLoadLastGoodNetwork(goodSsid, goodPassword);
    unsigned long openDelay = haveGood ? 120000UL : 90000UL;
    bool openDue = wpOpenSsid.length() && !wpOpenFallbackTried &&
                   now - wpRecoveryWindowStartMs >= openDelay;

    if (openDue) {
      wpOpenFallbackTried = true;
      wpBeginConnection(wpOpenSsid, "", true, true);
    } else if (haveGood) {
      wpBeginConnection(goodSsid, goodPassword, false, true);
      wpNextRecoveryMs = now + 120000UL;
    } else if (wpOpenSsid.length()) {
      wpOpenFallbackTried = true;
      wpBeginConnection(wpOpenSsid, "", true, true);
    } else {
      wpNextRecoveryMs = now + 30000UL;  // ще раз перевіримо після наступного сканування
    }
  }
  return wpState;
}

inline void wpStop() {
  wpScanPending = false;
  WiFi.scanDelete();
  wpServer.stop();
  wpDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WiFi.status() != WL_CONNECTED) {
    String ssid, password;
    if (wpLoadLastGoodNetwork(ssid, password)) WiFi.begin(ssid.c_str(), password.c_str());
    else WiFi.reconnect();
  }
}
