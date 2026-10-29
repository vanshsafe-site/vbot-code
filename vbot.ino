/*
  VBOT: autonomous ESP32 + SH1106 OLED desk buddy.
  Standalone sketch: open this file, or paste all of it into Arduino IDE.
  Board: ESP32 Dev Module (classic WROOM), Arduino ESP32 core 3.3.12.
  Partition: Huge APP (3MB No OTA/1MB SPIFFS), 4MB flash.
  Library: U8g2. Wiring: SDA 21, SCL 22, VCC 3V3, GND GND.
  Android setup: ESP Provisioning app; name and PIN appear on OLED.
  No buttons, microphone, sensors, or user input required.
  Face animation adapted from Robo Bot; original project: Tech Talkies.
*/
#include <Arduino.h>

// BEGIN PORTABLE NEWS PARSER
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Bounded RSS 2.0 title reader. No article bodies or entire feeds are retained.
namespace vbot {
constexpr size_t NEWS_COUNT = 8;
constexpr size_t TITLE_SIZE = 241;
struct Headlines {
  char titles[NEWS_COUNT][TITLE_SIZE] = {};
  size_t count = 0;
};

inline char displayChar(unsigned long c) {
  if (c >= 32 && c <= 126) return (char)c;
  if (c == 0x2018 || c == 0x2019) return '\'';
  if (c == 0x201c || c == 0x201d) return '"';
  if (c == 0x2013 || c == 0x2014) return '-';
  if (c == 0x2026) return '.';
  return ' ';
}

inline void plainTitle(const char *raw, char *out, size_t cap) {
  size_t n = 0;
  bool clipped = false;
  while (*raw) {
    unsigned long cp = (unsigned char)*raw++;
    if (cp == '&') {
      const char *end = strchr(raw, ';');
      if (end && end - raw < 12) {
        char entity[13] = {};
        memcpy(entity, raw, end - raw);
        if (!strcmp(entity, "amp")) cp = '&';
        else if (!strcmp(entity, "quot")) cp = '"';
        else if (!strcmp(entity, "apos") || !strcmp(entity, "rsquo")) cp = '\'';
        else if (!strcmp(entity, "lt")) cp = '<';
        else if (!strcmp(entity, "gt")) cp = '>';
        else if (!strcmp(entity, "nbsp")) cp = ' ';
        else if (entity[0] == '#') {
          char *tail;
          bool hex = entity[1] == 'x' || entity[1] == 'X';
          cp = strtoul(entity + (hex ? 2 : 1), &tail, hex ? 16 : 10);
          if (*tail) cp = ' ';
        } else cp = ' ';
        raw = end + 1;
      }
    } else if (cp >= 0xc0) {
      int extra = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
      cp &= extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07;
      while (extra-- && *raw && ((unsigned char)*raw & 0xc0) == 0x80)
        cp = (cp << 6) | ((unsigned char)*raw++ & 0x3f);
    }
    char c = displayChar(cp);
    if (c == ' ' && (!n || out[n - 1] == ' ')) continue;
    if (n + 1 >= cap) { clipped = true; break; }
    out[n++] = c;
  }
  while (n && out[n - 1] == ' ') --n;
  if (clipped && n >= 3) memcpy(out + n - 3, "...", 3);
  out[n] = 0;
}

class RssReader {
 public:
  Headlines result;
  void feed(const char *data, size_t len) {
    for (size_t i = 0; i < len && result.count < NEWS_COUNT; ++i) consume(data[i]);
  }
 private:
  char tag[96] = {}, raw[1024] = {};
  size_t tagLen = 0, rawLen = 0;
  bool inTag = false, inItem = false, inTitle = false, haveTitle = false;
  bool cdata = false, comment = false, overflow = false;
  char previous = 0, beforePrevious = 0;
  void append(char c) {
    if (!inTitle) return;
    if (rawLen + 1 < sizeof(raw)) raw[rawLen++] = c;
    else overflow = true;
  }
  void consume(char c) {
    if (comment) {
      if (beforePrevious == '-' && previous == '-' && c == '>') comment = false;
      beforePrevious = previous; previous = c; return;
    }
    if (cdata) {
      if (beforePrevious == ']' && previous == ']' && c == '>') {
        if (inTitle && rawLen >= 2 && !overflow) rawLen -= 2;
        cdata = false;
      } else append(c);
      beforePrevious = previous; previous = c; return;
    }
    if (!inTag) {
      if (c == '<') { inTag = true; tagLen = 0; }
      else append(c);
      return;
    }
    if (c != '>') {
      if (tagLen + 1 < sizeof(tag)) tag[tagLen++] = c;
      tag[tagLen] = 0;
      if (!strcmp(tag, "![CDATA[")) {
        cdata = true; inTag = false; previous = beforePrevious = 0;
      } else if (!strcmp(tag, "!--")) {
        comment = true; inTag = false; previous = beforePrevious = 0;
      }
      return;
    }
    inTag = false;
    if (!strcmp(tag, "item")) {
      inItem = true; inTitle = haveTitle = overflow = false; rawLen = 0;
    } else if (!strcmp(tag, "title") && inItem && !haveTitle) inTitle = true;
    else if (!strcmp(tag, "/title") && inTitle) { inTitle = false; haveTitle = true; }
    else if (!strcmp(tag, "/item") && inItem) {
      if (haveTitle) {
        raw[rawLen] = 0;
        char title[TITLE_SIZE];
        plainTitle(raw, title, sizeof(title));
        if (overflow && strlen(title) >= 3) memcpy(title + strlen(title) - 3, "...", 3);
        bool duplicate = false;
        for (size_t i = 0; i < result.count; ++i)
          if (!strcmp(title, result.titles[i])) duplicate = true;
        if (*title && !duplicate) strcpy(result.titles[result.count++], title);
      }
      inItem = inTitle = false;
    }
  }
};

// Word-wrapped lines used by both OLED messages and headline pages.
inline size_t wrap(const char *text, char rows[][25], size_t maxRows) {
  size_t count = 0;
  while (*text && count < maxRows) {
    while (*text == ' ') ++text;
    if (!*text) break;
    size_t len = strlen(text), take = len < 24 ? len : 24;
    if (len > 24 && text[24] != ' ') {
      for (size_t i = 24; i > 0; --i)
        if (text[i] == ' ') { take = i; break; }
    }
    memcpy(rows[count], text, take); rows[count++][take] = 0;
    text += take;
  }
  return count;
}
}  // namespace vbot
// END PORTABLE NEWS PARSER

#include <Arduino.h>

struct NetworkSnapshot {
  bool wifi = false;
  bool provisioning = false;
  bool feedOk = false;
  bool setupError = false;
  char name[24] = {};
  char pin[9] = {};
  uint32_t fetchedAt = 0;
  uint32_t revision = 0;
  vbot::Headlines news;
};

bool startNetwork();
bool readNetwork(NetworkSnapshot &snapshot);

#include <WiFi.h>
#include <Preferences.h>
#include <atomic>
#include <time.h>
#include <esp_wifi.h>
#include <esp32-hal-alloc-ble-mem.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <network_provisioning/manager.h>
#include <network_provisioning/scheme_ble.h>
#include <freertos/queue.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "This wiring/build targets the classic ESP32 (WROOM/Dev Module), not S2/S3/C3. Adapt and test before using another board."
#endif
#if !CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1
#error "VBOT needs provisioning Security 1 with proof of possession."
#endif

namespace {
void onEvent(arduino_event_t *event);
bool beginSetup();
bool fetchHeadlines(vbot::Headlines &headlines);
void networkTask(void *);
constexpr char FEED_URL[] = "https://feeds.arstechnica.com/arstechnica/technology-lab";
constexpr uint32_t FETCH_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t RETRY_FEED_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t RECONNECT_MS = 30000;
constexpr uint32_t SETUP_AFTER_MS = 90000;
constexpr size_t MAX_FEED_BYTES = 256 * 1024;
QueueHandle_t snapshots;
NetworkSnapshot state;
std::atomic<bool> credentialsReceived{false}, credentialsFailed{false};
std::atomic<bool> provisioningEnded{false}, gotIp{false};
bool managerActive = false;
char savedSsid[33] = {}, savedPassword[65] = {};

void onEvent(arduino_event_t *event) {
  // Callbacks run on a different task. Never draw, write flash, or block here.
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_CRED_RECV: credentialsReceived = true; break;
    case ARDUINO_EVENT_PROV_CRED_FAIL: credentialsFailed = true; break;
    case ARDUINO_EVENT_PROV_END: provisioningEnded = true; break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: gotIp = true; break;
    default: break;
  }
}

bool beginSetup() {
  network_prov_mgr_config_t config = {};
  config.scheme = network_prov_scheme_ble;
  // Keep BT memory available so setup can restart after a later network loss.
  config.scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
  if (network_prov_mgr_init(config) != ESP_OK) return false;
  static uint8_t uuid[16] = {
    0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02
  };
  if (network_prov_scheme_ble_set_service_uuid(uuid) != ESP_OK ||
      network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, state.pin, state.name, nullptr) != ESP_OK) {
    network_prov_mgr_deinit();
    return false;
  }
  managerActive = true;
  state.provisioning = true;
  Serial.printf("Bluetooth setup: %s; PIN is on the screen.\n", state.name);
  return true;
}

bool fetchHeadlines(vbot::Headlines &headlines) {
  // A valid clock is required for certificate validity checking. Never use insecure TLS.
  if (time(nullptr) < 1704067200) return false;
  esp_http_client_config_t config = {};
  config.url = FEED_URL;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 3500;
  config.buffer_size = 1024;
  config.user_agent = "VBOT/1.0 (personal OLED RSS reader)";
  config.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  esp_http_client_set_header(client, "Accept", "application/rss+xml, application/xml, text/xml");
  esp_http_client_set_header(client, "Accept-Encoding", "identity");
  bool ok = false;
  // Feed location is fixed; unexpected redirects/HTML fail safely and retry later.
  if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0 &&
      esp_http_client_get_status_code(client) == 200) {
    vbot::RssReader parser;
    char buffer[768];
    size_t total = 0;
    uint32_t began = millis();
    bool finished = false;
    while (millis() - began < 15000 && total < MAX_FEED_BYTES) {
      int count = esp_http_client_read(client, buffer, sizeof(buffer));
      if (count < 0) break;
      if (count == 0) { finished = esp_http_client_is_complete_data_received(client); break; }
      total += count;
      parser.feed(buffer, (size_t)count);
      if (parser.result.count == vbot::NEWS_COUNT) { finished = true; break; }
      vTaskDelay(1);
    }
    if (finished && parser.result.count) { headlines = parser.result; ok = true; }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

void networkTask(void *) {
  Preferences preferences;
  bool storage = preferences.begin("vbot", false);
  if (storage) {
    preferences.getString("ssid", savedSsid, sizeof(savedSsid));
    preferences.getString("password", savedPassword, sizeof(savedPassword));
    preferences.getString("pin", state.pin, sizeof(state.pin));
  }
  if (!*state.pin) {
    snprintf(state.pin, sizeof(state.pin), "%08lu", (unsigned long)(esp_random() % 100000000));
    if (storage) preferences.putString("pin", state.pin);
  }
  uint64_t mac = ESP.getEfuseMac();
  snprintf(state.name, sizeof(state.name), "PROV_VBOT_%06lX", (unsigned long)(mac & 0xffffff));
  WiFi.onEvent(onEvent);
  WiFi.persistent(false); // VBOT owns its last-known-good credentials in Preferences.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false); // Worker controls retry timing, including during provisioning.
  WiFi.setHostname("vbot");
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  if (*savedSsid) WiFi.begin(savedSsid, savedPassword);

  uint32_t disconnectedAt = millis(), reconnectAt = millis(), setupAttemptAt = 0;
  uint32_t fetchAt = 0, fetchWait = 0, credentialAt = 0, failedAt = 0;
  bool wasConnected = false, credentialBusy = false, failurePending = false;
  bool setupAttempted = false;
  for (;;) {
    uint32_t now = millis();
    if (credentialsReceived.exchange(false)) { credentialBusy = true; credentialAt = now; }
    if (credentialsFailed.exchange(false)) {
      failurePending = true; failedAt = now;
      Serial.println("Wi-Fi setup failed. Check password/network and retry in the app.");
    }
    if (provisioningEnded.exchange(false)) {
      // Arduino WiFiGeneric deinitializes the manager before dispatching this event.
      managerActive = state.provisioning = false;
      credentialBusy = failurePending = false;
    }
    bool connected = WiFi.status() == WL_CONNECTED;
    if (gotIp.exchange(false) && connected) {
      String ssid = WiFi.SSID(), password = WiFi.psk();
      if (ssid.length() && (strcmp(savedSsid, ssid.c_str()) || strcmp(savedPassword, password.c_str()))) {
        ssid.toCharArray(savedSsid, sizeof(savedSsid));
        password.toCharArray(savedPassword, sizeof(savedPassword));
        if (storage) {
          preferences.putString("ssid", savedSsid);
          preferences.putString("password", savedPassword);
        }
      }
      credentialBusy = false;
      Serial.println("Wi-Fi connected. VBOT runs without the phone now.");
    }
    if (!connected && wasConnected) { disconnectedAt = now; state.feedOk = false; }
    if (connected && !wasConnected) { fetchAt = now; fetchWait = 5000; }
    wasConnected = connected;
    state.wifi = connected;

    if (managerActive && failurePending && now - failedAt >= 10000) {
      // Leave time for the phone to read the failure, then allow another password attempt.
      network_prov_mgr_reset_wifi_sm_state_on_failure();
      failurePending = credentialBusy = false;
      reconnectAt = now;
    }
    if (credentialBusy && now - credentialAt >= 60000 && !failurePending) {
      // An interrupted setup must not permanently suppress reconnect attempts.
      network_prov_mgr_stop_provisioning();
      credentialBusy = false;
    }
    if (!connected && !managerActive &&
        (!*savedSsid || now - disconnectedAt >= SETUP_AFTER_MS) &&
        (!setupAttempted || now - setupAttemptAt >= 30000)) {
      setupAttempted = true; setupAttemptAt = now;
      state.setupError = !beginSetup();
      reconnectAt = now;
    }
    if (!connected && *savedSsid && !credentialBusy && !failurePending && now - reconnectAt >= RECONNECT_MS) {
      reconnectAt = now;
      WiFi.begin(savedSsid, savedPassword);
    }
    // Publish before doing blocking TLS work, so display can show setup/network changes.
    xQueueOverwrite(snapshots, &state);
    if (connected && !state.provisioning && now - fetchAt >= fetchWait) {
      state.feedOk = fetchHeadlines(state.news);
      fetchAt = millis();
      fetchWait = state.feedOk ? FETCH_MS : RETRY_FEED_MS;
      if (state.feedOk) { state.fetchedAt = fetchAt; ++state.revision; }
      Serial.println(state.feedOk ? "Tech headlines refreshed." : "News unavailable; offline personality continues.");
      xQueueOverwrite(snapshots, &state);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
} // namespace

bool startNetwork() {
  snapshots = xQueueCreate(1, sizeof(NetworkSnapshot));
  if (!snapshots) return false;
  // Priority 1 plus yielding leaves time for the display even on a single application core.
  if (xTaskCreate(networkTask, "vbot-net", 16384, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(snapshots); snapshots = nullptr; return false;
  }
  return true;
}

bool readNetwork(NetworkSnapshot &snapshot) {
  return snapshots && xQueueReceive(snapshots, &snapshot, 0) == pdTRUE;
}


#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

void centered(const char *text, int y);
uint32_t titleHash(const char *text);
bool chooseHeadline(uint32_t now);
void updateBuddy(uint32_t now);
void render(uint32_t now);
void setup();
void loop();

constexpr uint32_t NEWS_INTERVAL_MS = 3UL * 60000UL;
constexpr uint32_t AWAKE_MS = 4UL * 60000UL;
constexpr uint32_t REST_MS = 25000;
constexpr uint32_t PAGE_MS = 7000;
constexpr uint8_t BRIGHTNESS = 150;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);
NetworkSnapshot network;
bool networkStarted = false;

enum Mood { CALM, HAPPY, CURIOUS, SURPRISED, SLEEPY };
Mood mood = HAPPY;
const char *const phrases[] = {
  "Hey, I'm Vbot!", "Tiny bot. Big dreams.",
  "Let's make something!", "One step at a time.",
  "Time for a stretch?", "Rest your eyes a bit.",
  "A sip of water?", "Stay curious, human.",
  "Small steps still count.", "I'm your little desk pal.",
  "Thinking in pixels...", "Beep. You can do this!",
  "Taking a tiny pixel nap.", "Ideas need breaks too."
};
struct Eye {
  float x, y, w, h;
  void draw(float nx, float ny, float nw, float nh) {
    x += (nx - x) * .24f; y += (ny - y) * .24f;
    w += (nw - w) * .24f; h += (nh - h) * .38f;
    int iw = constrain((int)roundf(w), 2, 32), ih = constrain((int)roundf(h), 2, 30);
    oled.drawRBox((int)(x - iw / 2), (int)(y - ih / 2), iw, ih, max(0, min(iw, ih) / 2 - 1));
  }
};
Eye eyes[2] = {{40, 27, 25, 25}, {88, 27, 25, 25}};
uint32_t frameAt = 0, actionAt = 0, actionWait = 2500, blinkAt = 0, blinkWait = 2400;
uint32_t phraseAt = 0, phraseWait = 18000, messageAt = 0, newsAt = 0, awakeAt = 0;
uint32_t restAt = 0, headlineAt = 0, setupAt = 0;
bool blinking = false, resting = false, showingNews = false, showingSetup = false;
float gazeX = 0, gazeY = 0;
int lastPhrase = -1;
char message[49] = "Hey, I'm Vbot!";
char headlineRows[20][25] = {};
size_t headlineLines = 0;
bool headlineCached = false;
uint32_t seenHashes[32] = {};
size_t seenNext = 0;

void centered(const char *text, int y) {
  oled.drawStr(max(0, (128 - (int)oled.getStrWidth(text)) / 2), y, text);
}

uint32_t titleHash(const char *text) {
  uint32_t h = 2166136261UL;
  while (*text) { h ^= (uint8_t)*text++; h *= 16777619UL; }
  return h ? h : 1;
}

bool chooseHeadline(uint32_t now) {
  if (!network.news.count || now - network.fetchedAt > 6UL * 60UL * 60000UL) return false;
  size_t start = random(network.news.count);
  for (size_t n = 0; n < network.news.count; ++n) {
    const char *title = network.news.titles[(start + n) % network.news.count];
    uint32_t hash = titleHash(title);
    bool seen = false;
    for (uint32_t old : seenHashes) if (old == hash) seen = true;
    if (seen) continue;
    seenHashes[seenNext++ % 32] = hash;
    headlineLines = vbot::wrap(title, headlineRows, 20);
    headlineCached = !network.wifi || !network.feedOk || now - network.fetchedAt > 35UL * 60000UL;
    headlineAt = now; showingNews = true;
    return true;
  }
  return false;
}

void updateBuddy(uint32_t now) {
  readNetwork(network);
  if (!network.provisioning) showingSetup = false;
  // Setup instructions recur automatically; they never require a button press.
  if (network.provisioning && !showingNews && (!setupAt || now - setupAt >= 25000)) {
    showingSetup = true; setupAt = now;
  }
  if (showingSetup && now - setupAt >= 10000) showingSetup = false;
  if (showingNews && now - headlineAt >= ((headlineLines + 3) / 4) * PAGE_MS) showingNews = false;
  if (resting && now - restAt >= REST_MS) {
    resting = false; awakeAt = now;
    snprintf(message, sizeof(message), "Back from my pixel nap!"); messageAt = now;
  }
  if (!resting && !showingNews && !showingSetup && now - awakeAt >= AWAKE_MS) {
    resting = true; restAt = now;
  }
  if (!resting && !showingSetup && !showingNews && now - newsAt >= NEWS_INTERVAL_MS && now - messageAt >= 7000) {
    newsAt = now; chooseHeadline(now);
  }
  if (!resting && !showingSetup && !showingNews && now - phraseAt >= phraseWait) {
    int index;
    do { index = random(sizeof(phrases) / sizeof(phrases[0])); } while (index == lastPhrase);
    lastPhrase = index;
    snprintf(message, sizeof(message), "%s", phrases[index]);
    messageAt = phraseAt = now; phraseWait = random(18000, 40000);
  }
  if (now - actionAt >= actionWait) {
    actionAt = now; actionWait = random(2500, 6500);
    gazeX = random(-6, 7); gazeY = random(-2, 3);
    int choice = random(10);
    mood = choice < 5 ? CALM : choice < 8 ? HAPPY : choice == 8 ? CURIOUS : SURPRISED;
  }
  if (!blinking && now - blinkAt >= blinkWait) { blinking = true; blinkAt = now; }
  else if (blinking && now - blinkAt >= 130) { blinking = false; blinkAt = now; blinkWait = random(2000, 6000); }
}

void render(uint32_t now) {
  oled.clearBuffer(); oled.setFont(u8g2_font_5x8_tf);
  oled.setContrast(resting && !showingSetup ? 8 : BRIGHTNESS);
  if (showingSetup) {
    centered("PHONE WI-FI SETUP", 8);
    centered("ESP Provisioning app", 21);
    centered(network.name, 33);
    char pin[24]; snprintf(pin, sizeof(pin), "PIN: %s", network.pin);
    centered(pin, 45);
    centered("Choose 2.4 GHz Wi-Fi", 60);
  } else if (showingNews) {
    size_t page = (now - headlineAt) / PAGE_MS;
    oled.drawStr(2, 8, "ARS TECHNICA");
    char counter[12]; snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(page + 1), (unsigned)((headlineLines + 3) / 4));
    oled.drawStr(126 - oled.getStrWidth(counter), 8, counter);
    for (size_t i = 0; i < 4 && page * 4 + i < headlineLines; ++i)
      oled.drawStr(3, 21 + i * 10, headlineRows[page * 4 + i]);
    centered(headlineCached ? "CACHED HEADLINE" : "TECH HEADLINE", 63);
  } else if (resting) {
    int dx = (int)((now / 4000) % 9) - 4, dy = (int)((now / 9000) % 5) - 2;
    oled.drawRBox(27 + dx, 30 + dy, 23, 3, 0);
    oled.drawRBox(78 + dx, 30 + dy, 23, 3, 0);
    oled.drawStr(105 + dx, 21 + dy, (now / 1100) % 2 ? "z" : "Z");
  } else {
    oled.drawStr(2, 8, "VBOT");
    const char *status = !networkStarted || network.setupError ? "NET ERROR" :
      network.provisioning ? "SETUP" : !network.wifi ? "OFFLINE" : network.feedOk ? "NEWS OK" : "WI-FI";
    oled.drawStr(126 - oled.getStrWidth(status), 8, status);
    float bob = sinf(now / 850.f) * 1.2f;
    for (int i = 0; i < 2; ++i) {
      float h = mood == HAPPY ? 16 : mood == SURPRISED ? 29 : mood == CURIOUS && i ? 28 : 24;
      float w = mood == HAPPY ? 28 : mood == SURPRISED ? 20 : 25;
      eyes[i].draw((i ? 88 : 40) + gazeX, 27 + bob + gazeY, w, blinking ? 2 : h);
    }
    if (now - messageAt < 7000) {
      char rows[2][25] = {};
      size_t lines = vbot::wrap(message, rows, 2);
      oled.drawHLine(5, 46, 118);
      centered(rows[0], lines > 1 ? 54 : 59);
      if (lines > 1) centered(rows[1], 63);
    } else if (mood == SURPRISED) oled.drawCircle(64, 53, 4);
    else if (mood == HAPPY) {
      oled.drawLine(55, 50, 59, 54); oled.drawHLine(59, 54, 10); oled.drawLine(69, 54, 73, 50);
    } else oled.drawRBox(58, 51, 12, 3, 0);
  }
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  oled.setI2CAddress(0x3C << 1);
  oled.setBusClock(400000);
  oled.begin(); oled.setContrast(BRIGHTNESS);
  randomSeed(esp_random());
  awakeAt = messageAt = phraseAt = millis();
  render(millis());
  networkStarted = startNetwork();
  Serial.println(networkStarted ? "VBOT ready. Network worker started." : "Network worker failed; offline buddy still runs.");
}

void loop() {
  uint32_t now = millis();
  updateBuddy(now);
  if (now - frameAt >= (resting && !showingSetup ? 200UL : 33UL)) {
    frameAt = now; render(now);
  }
  delay(1);
}
