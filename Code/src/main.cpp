#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "homing.h"
#include "motor_control.h"
#include "position_control.h"
#include "stepper_driver.h"

// ============================================================
// WiFi Credentials - Router WiFi
// ============================================================
#define WIFI_SSID "Harendra"
#define WIFI_PASSWORD "Harendra1"

// ============================================================
// Web Server
// ============================================================
WebServer server(80);

// ============================================================
// WiFi / AP Settings
// ============================================================
String staSsid = WIFI_SSID;
String staPass = WIFI_PASSWORD;

String apSsid = "RetrofitSwitch";
String apPass = "12345678";

String tzString = "IST-5:30";
String ntpServerString = "pool.ntp.org";

// ============================================================
// Robot State
// ============================================================
float currentX = 0.0;
float currentY = 0.0;

bool motorsRunning = false;
bool apiHomingInProgress = false;
String lastHomeMessage = "Not homed";

// ============================================================
// Switch Config
// ============================================================
const int MAX_SWITCHES = 10;
const int MAX_SCHEDULES = 20;

struct SavedPos {
  bool saved;
  float x;
  float y;
};

struct SwitchConfig {
  String name;
  SavedPos on;
  SavedPos off;
};

SwitchConfig switches[MAX_SWITCHES];
int switchCount = 4;
int switchType = 0;

// ============================================================
// Schedule Config
// ============================================================
struct ScheduleItem {
  int index;
  int switch_index;
  String action;
  String type;
  int hour;
  int minute;
  int days_mask;
  String date;
  bool enabled;
  String last_fire_key;
};

ScheduleItem schedules[MAX_SCHEDULES];
int scheduleCount = 0;
int nextScheduleIndex = 0;

// ============================================================
// JSON Helpers
// ============================================================
template <typename T> void sendJsonDoc(T &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendOk(const String &message = "OK") {
  StaticJsonDocument<160> doc;
  doc["success"] = true;
  doc["message"] = message;
  sendJsonDoc(doc);
}

void sendError(const String &message, int code = 400) {
  StaticJsonDocument<256> doc;
  doc["success"] = false;
  doc["message"] = message;
  sendJsonDoc(doc, code);
}

bool parseBody(StaticJsonDocument<1024> &doc) {
  String body = server.arg("plain");

  if (body.length() == 0) {
    sendError("Empty JSON body");
    return false;
  }

  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    sendError("Bad JSON");
    return false;
  }

  return true;
}

String pathSegment(const String &path, int wantedIndex) {
  int start = path.startsWith("/") ? 1 : 0;

  for (int i = 0; i < wantedIndex; i++) {
    int slash = path.indexOf('/', start);
    if (slash < 0)
      return "";
    start = slash + 1;
  }

  int end = path.indexOf('/', start);
  if (end < 0)
    end = path.length();

  return path.substring(start, end);
}

// ============================================================
// SPIFFS Files
// ============================================================
void listSpiffsFiles(const char *dirname = "/") {
  Serial.println("SPIFFS file list:");

  File root = SPIFFS.open(dirname);

  if (!root) {
    Serial.println("Failed to open SPIFFS root");
    return;
  }

  if (!root.isDirectory()) {
    Serial.println("SPIFFS root is not a directory");
    return;
  }

  File file = root.openNextFile();

  while (file) {
    Serial.print("FILE: ");
    Serial.print(file.name());
    Serial.print("  SIZE: ");
    Serial.println(file.size());

    file = root.openNextFile();
  }

  Serial.println("End of SPIFFS file list.");
}

void sendFile(const char *path, const char *type) {
  Serial.print("Trying to open: ");
  Serial.println(path);

  if (!SPIFFS.exists(path)) {
    Serial.print("File not found: ");
    Serial.println(path);
    server.send(404, "text/plain", String("File not found: ") + path);
    return;
  }

  File file = SPIFFS.open(path, "r");

  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }

  server.streamFile(file, type);
  file.close();
}

// ============================================================
// Time
// ============================================================
void applyTimeSettings() {
  setenv("TZ", tzString.c_str(), 1);
  tzset();
  configTime(0, 0, ntpServerString.c_str());
}

bool timeSynced() {
  time_t now;
  time(&now);
  return now > 1700000000;
}

String getNowString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 50)) {
    return "";
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// ============================================================
// WiFi
// ============================================================
void startAccessPoint() {
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  Serial.print("AP SSID: ");
  Serial.println(apSsid);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void connectToRouterWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  startAccessPoint();

  Serial.print("Connecting to router WiFi: ");
  Serial.println(staSsid);

  WiFi.begin(staSsid.c_str(), staPass.c_str());

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Router WiFi connected!");
    Serial.print("ESP32 STA IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Router WiFi failed. AP mode still active.");
    Serial.print("Use AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

// ============================================================
// Motor / Homing Process
// ============================================================
void runHomingSequence() {
  Serial.println();
  Serial.println("================================");
  Serial.println("REAL HOMING SEQUENCE START");
  Serial.println("Calling homing::home()");
  Serial.println("================================");

  apiHomingInProgress = true;
  lastHomeMessage = "Homing running";

  homing::Result result = homing::home();

  if (result == homing::Result::OK) {
    position_control::setCurrent(0.0f, 0.0f);
    lastHomeMessage = "Homing success";

    Serial.println("================================");
    Serial.println("HOMING SUCCESS");
    Serial.println("================================");
  } else {
    lastHomeMessage = "Homing failed";

    Serial.println("================================");
    Serial.println("HOMING FAILED");
    Serial.println("================================");
  }

  apiHomingInProgress = false;
}

void homeTask(void *parameter) {
  Serial.println("HOME TASK STARTED");

  runHomingSequence();

  Serial.println("HOME TASK FINISHED");
  vTaskDelete(NULL);
}

bool moveToXY(float x, float y) { return position_control::moveToXY(x, y); }

bool jogXY(float dx, float dy) { return position_control::jogXY(dx, dy); }

bool pressSwitch(int idx, String action) {
  if (idx < 0 || idx >= switchCount)
    return false;

  action.toLowerCase();
  bool actuateSolenoid = (idx > 0);

  if (action == "on") {
    if (!switches[idx].on.saved)
      return false;

    if (actuateSolenoid)
      return position_control::pressAtXY(switches[idx].on.x,
                                         switches[idx].on.y);
    else
      return position_control::moveToXY(switches[idx].on.x, switches[idx].on.y);
  }

  if (action == "off") {
    if (!switches[idx].off.saved)
      return false;

    if (actuateSolenoid)
      return position_control::pressAtXY(switches[idx].off.x,
                                         switches[idx].off.y);
    else
      return position_control::moveToXY(switches[idx].off.x,
                                        switches[idx].off.y);
  }

  return false;
}

// ============================================================
// API: Status
// ============================================================
void handleStatus() {
  DynamicJsonDocument doc(1024);

  doc["success"] = true;
  doc["homed"] = homing::isHomed();
  doc["homing_in_progress"] = apiHomingInProgress;
  doc["home_message"] = lastHomeMessage;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["sta_connected"] = WiFi.status() == WL_CONNECTED;
  wifi["sta_ip"] =
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  wifi["ap_ip"] = WiFi.softAPIP().toString();

  JsonObject timeObj = doc.createNestedObject("time");
  timeObj["synced"] = timeSynced();
  timeObj["now"] = getNowString();

  JsonObject pos = doc.createNestedObject("position");
  pos["x"] = position_control::getX();
  pos["y"] = position_control::getY();

  sendJsonDoc(doc);
}

// ============================================================
// API: Home Button
// ============================================================
void handleHome() {
  Serial.println("================================");
  Serial.println("HOME BUTTON API CALLED");
  Serial.println("POST /api/home received");
  Serial.println("================================");

  if (apiHomingInProgress) {
    sendOk("Homing already running");
    return;
  }

  xTaskCreatePinnedToCore(homeTask, "homeTask", 8192, NULL, 1, NULL, 1);

  sendOk("Homing started");
}

// ============================================================
// API: Force Home (skip obstacle-seeking routine)
// ============================================================
void handleForceHome() {
  if (apiHomingInProgress) {
    sendError("Homing is currently running, wait for it to finish");
    return;
  }

  homing::forceHomeAtCurrentPosition();
  position_control::setCurrent(0.0f, 0.0f);
  resetMotorObstacleStops();

  lastHomeMessage = "Homed manually (obstacle-seek skipped)";

  sendOk("Marked as homed at current position");
}
void handleGetPosition() {
  StaticJsonDocument<384> doc;

  doc["success"] = true;
  doc["x"] = position_control::getX();
  doc["y"] = position_control::getY();
  doc["q1"] = position_control::getQ1();
  doc["q2"] = position_control::getQ2();
  doc["moving"] = position_control::isBusy();
  doc["obstacle_m1"] = motor1ObstacleStopped;
  doc["obstacle_m2"] = motor2ObstacleStopped;

  sendJsonDoc(doc);
}

void handleJog() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  float dx = body["dx"] | 0.0;
  float dy = body["dy"] | 0.0;

  if (!jogXY(dx, dy)) {
    String err = position_control::lastError();
    sendError(err.length() ? err : "Home first or target invalid");
    return;
  }

  sendOk("Jog started");
}

void handleMoveAbsolute() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  float x = body["x"] | position_control::getX();
  float y = body["y"] | position_control::getY();

  if (!moveToXY(x, y)) {
    String err = position_control::lastError();
    sendError(err.length() ? err : "Home first or target invalid");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["success"] = true;
  doc["message"] = "Move started";
  doc["x"] = x;
  doc["y"] = y;
  sendJsonDoc(doc);
}

// ============================================================
// API: Obstacle reset — clears the obstacle-stop latch and
// re-enables both drivers so jog/move can power the motors again.
// ============================================================
void handleResetObstacle() {
  resetMotorObstacleStops();
  sendOk("Obstacle flags cleared, motors re-enabled");
}

// ============================================================
// API: Switches
// ============================================================
void handleGetSwitches() {
  DynamicJsonDocument doc(4096);

  doc["success"] = true;
  doc["count"] = switchCount;
  doc["type"] = switchType;

  JsonArray arr = doc.createNestedArray("switches");

  for (int i = 0; i < switchCount; i++) {
    JsonObject sw = arr.createNestedObject();

    sw["index"] = i;
    sw["name"] = switches[i].name;

    JsonObject on = sw.createNestedObject("on");
    on["saved"] = switches[i].on.saved;
    on["x"] = switches[i].on.x;
    on["y"] = switches[i].on.y;

    JsonObject off = sw.createNestedObject("off");
    off["saved"] = switches[i].off.saved;
    off["x"] = switches[i].off.x;
    off["y"] = switches[i].off.y;
  }

  sendJsonDoc(doc);
}

void handleSetSwitchCount() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  int count = body["count"] | switchCount;
  int type = body["type"] | switchType;

  if (count < 1)
    count = 1;
  if (count > MAX_SWITCHES)
    count = MAX_SWITCHES;

  switchCount = count;
  switchType = type;

  for (int i = 0; i < switchCount; i++) {
    if (switches[i].name.length() == 0) {
      switches[i].name = "Switch " + String(i + 1);
    }
  }

  sendOk("Switch count saved");
}

void handleUpdateSwitchName(int idx) {
  if (idx < 0 || idx >= switchCount) {
    sendError("Invalid switch index", 404);
    return;
  }

  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  switches[idx].name = String((const char *)(body["name"] | ""));

  if (switches[idx].name.length() == 0) {
    switches[idx].name = "Switch " + String(idx + 1);
  }

  sendOk("Name saved");
}

void handleSaveSwitchPosition(int idx) {
  if (idx < 0 || idx >= switchCount) {
    sendError("Invalid switch index", 404);
    return;
  }

  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  String state = String((const char *)(body["state"] | ""));
  float x = body["x"] | position_control::getX();
  float y = body["y"] | position_control::getY();

  state.toLowerCase();

  if (state == "on") {
    switches[idx].on.saved = true;
    switches[idx].on.x = x;
    switches[idx].on.y = y;
  } else if (state == "off") {
    switches[idx].off.saved = true;
    switches[idx].off.x = x;
    switches[idx].off.y = y;
  } else {
    sendError("Invalid state");
    return;
  }

  sendOk("Position saved");
}

void handlePress(int idx, String action) {
  bool ok = pressSwitch(idx, action);

  if (!ok) {
    sendError("Switch position not saved or invalid action");
    return;
  }

  sendOk("Press started");
}

// ============================================================
// API: Schedules
// ============================================================
void handleGetSchedules() {
  DynamicJsonDocument doc(4096);

  doc["success"] = true;

  JsonArray arr = doc.createNestedArray("schedules");

  for (int i = 0; i < scheduleCount; i++) {
    JsonObject s = arr.createNestedObject();

    s["index"] = schedules[i].index;
    s["switch_index"] = schedules[i].switch_index;
    s["action"] = schedules[i].action;
    s["type"] = schedules[i].type;
    s["hour"] = schedules[i].hour;
    s["minute"] = schedules[i].minute;
    s["days_mask"] = schedules[i].days_mask;
    s["date"] = schedules[i].date;
    s["enabled"] = schedules[i].enabled;
  }

  sendJsonDoc(doc);
}

void handleAddSchedule() {
  if (scheduleCount >= MAX_SCHEDULES) {
    sendError("Schedule limit reached");
    return;
  }

  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  ScheduleItem &s = schedules[scheduleCount];

  s.index = nextScheduleIndex++;
  s.switch_index = body["switch_index"] | 0;
  s.action = String((const char *)(body["action"] | "on"));
  s.type = String((const char *)(body["type"] | "daily"));
  s.hour = body["hour"] | 8;
  s.minute = body["minute"] | 0;
  s.days_mask = body["days_mask"] | 0x7F;
  s.date = String((const char *)(body["date"] | ""));
  s.enabled = body["enabled"] | true;
  s.last_fire_key = "";

  scheduleCount++;

  sendOk("Schedule added");
}

int findScheduleByIndex(int idx) {
  for (int i = 0; i < scheduleCount; i++) {
    if (schedules[i].index == idx)
      return i;
  }

  return -1;
}

void handleUpdateSchedule(int idx) {
  int pos = findScheduleByIndex(idx);

  if (pos < 0) {
    sendError("Schedule not found", 404);
    return;
  }

  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  schedules[pos].switch_index =
      body["switch_index"] | schedules[pos].switch_index;
  schedules[pos].action =
      String((const char *)(body["action"] | schedules[pos].action.c_str()));
  schedules[pos].type =
      String((const char *)(body["type"] | schedules[pos].type.c_str()));
  schedules[pos].hour = body["hour"] | schedules[pos].hour;
  schedules[pos].minute = body["minute"] | schedules[pos].minute;
  schedules[pos].days_mask = body["days_mask"] | schedules[pos].days_mask;
  schedules[pos].date =
      String((const char *)(body["date"] | schedules[pos].date.c_str()));
  schedules[pos].enabled = body["enabled"] | schedules[pos].enabled;

  sendOk("Schedule updated");
}

void handleDeleteSchedule(int idx) {
  int pos = findScheduleByIndex(idx);

  if (pos < 0) {
    sendError("Schedule not found", 404);
    return;
  }

  for (int i = pos; i < scheduleCount - 1; i++) {
    schedules[i] = schedules[i + 1];
  }

  scheduleCount--;

  sendOk("Schedule deleted");
}

void checkSchedules() {
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck < 10000)
    return;
  lastCheck = millis();

  if (!timeSynced())
    return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50))
    return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int wday = timeinfo.tm_wday;

  char dateBuf[12];
  strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &timeinfo);
  String today = String(dateBuf);

  String fireKey = today + " " + String(hour) + ":" + String(minute);

  for (int i = 0; i < scheduleCount; i++) {
    ScheduleItem &s = schedules[i];

    if (!s.enabled)
      continue;
    if (s.hour != hour || s.minute != minute)
      continue;
    if (s.last_fire_key == fireKey)
      continue;

    bool shouldFire = false;

    if (s.type == "daily") {
      shouldFire = true;
    } else if (s.type == "days_of_week") {
      shouldFire = (s.days_mask & (1 << wday));
    } else if (s.type == "one_time") {
      shouldFire = (s.date == today);
    }

    if (shouldFire) {
      pressSwitch(s.switch_index, s.action);
      s.last_fire_key = fireKey;

      if (s.type == "one_time") {
        s.enabled = false;
      }
    }
  }
}

// ============================================================
// API: WiFi
// ============================================================
void handleGetWifi() {
  DynamicJsonDocument doc(1024);

  doc["success"] = true;
  doc["sta_ssid"] = staSsid;
  doc["ap_ssid"] = apSsid;
  doc["sta_connected"] = WiFi.status() == WL_CONNECTED;
  doc["sta_ip"] =
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  doc["ap_ip"] = WiFi.softAPIP().toString();

  sendJsonDoc(doc);
}

void handleSetStaWifi() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  staSsid = String((const char *)(body["ssid"] | staSsid.c_str()));
  staPass = String((const char *)(body["pass"] | staPass.c_str()));

  WiFi.disconnect();
  delay(300);
  WiFi.begin(staSsid.c_str(), staPass.c_str());

  sendOk("Connecting to router WiFi");
}

void handleSetApWifi() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  apSsid = String((const char *)(body["ssid"] | apSsid.c_str()));
  apPass = String((const char *)(body["pass"] | apPass.c_str()));

  if (apPass.length() < 8) {
    sendError("AP password must be at least 8 characters");
    return;
  }

  WiFi.softAPdisconnect(true);
  delay(300);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  sendOk("Access point updated");
}

// ============================================================
// API: Time
// ============================================================
void handleGetTime() {
  DynamicJsonDocument doc(1024);

  doc["success"] = true;
  doc["ntp_server"] = ntpServerString;
  doc["tz"] = tzString;
  doc["now"] = getNowString();
  doc["synced"] = timeSynced();

  sendJsonDoc(doc);
}

void handleSetTime() {
  StaticJsonDocument<1024> body;
  if (!parseBody(body))
    return;

  tzString = String((const char *)(body["tz"] | tzString.c_str()));
  ntpServerString =
      String((const char *)(body["ntp_server"] | ntpServerString.c_str()));

  applyTimeSettings();

  sendOk("Time settings saved");
}

// ============================================================
// Dynamic API Routes
// ============================================================
void handleDynamicApiRoutes() {
  String uri = server.uri();
  HTTPMethod method = server.method();

  String s0 = pathSegment(uri, 0);
  String s1 = pathSegment(uri, 1);
  String s2 = pathSegment(uri, 2);
  String s3 = pathSegment(uri, 3);

  // POST /api/press/{idx}/{on/off}
  if (method == HTTP_POST && s0 == "api" && s1 == "press") {
    int idx = s2.toInt();
    String action = s3;
    handlePress(idx, action);
    return;
  }

  // PUT /api/switches/{idx}
  if (method == HTTP_PUT && s0 == "api" && s1 == "switches" &&
      s2.length() > 0 && s3.length() == 0) {
    int idx = s2.toInt();
    handleUpdateSwitchName(idx);
    return;
  }

  // PUT /api/switches/{idx}/position
  if (method == HTTP_PUT && s0 == "api" && s1 == "switches" &&
      s2.length() > 0 && s3 == "position") {
    int idx = s2.toInt();
    handleSaveSwitchPosition(idx);
    return;
  }

  // PUT /api/schedules/{idx}
  if (method == HTTP_PUT && s0 == "api" && s1 == "schedules" &&
      s2.length() > 0) {
    int idx = s2.toInt();
    handleUpdateSchedule(idx);
    return;
  }

  // DELETE /api/schedules/{idx}
  if (method == HTTP_DELETE && s0 == "api" && s1 == "schedules" &&
      s2.length() > 0) {
    int idx = s2.toInt();
    handleDeleteSchedule(idx);
    return;
  }

  sendError("Endpoint not found", 404);
}

// ============================================================
// Routes
// ============================================================
void setupRoutes() {
  // UI files from data/www/
  server.on("/", HTTP_GET, []() { sendFile("/www/index.html", "text/html"); });

  server.on("/index.html", HTTP_GET,
            []() { sendFile("/www/index.html", "text/html"); });

  server.on("/style.css", HTTP_GET,
            []() { sendFile("/www/style.css", "text/css"); });

  server.on("/app.js", HTTP_GET,
            []() { sendFile("/www/app.js", "application/javascript"); });

  // Main APIs
  server.on("/api/status", HTTP_GET, handleStatus);

  // This is the route used by Home motors button
  server.on("/api/home", HTTP_POST, handleHome);
  server.on("/api/home/force", HTTP_POST, handleForceHome);

  server.on("/api/position", HTTP_GET, handleGetPosition);
  server.on("/api/jog", HTTP_POST, handleJog);
  server.on("/api/move", HTTP_POST, handleMoveAbsolute);
  server.on("/api/obstacle/reset", HTTP_POST, handleResetObstacle);

  server.on("/api/switches", HTTP_GET, handleGetSwitches);
  server.on("/api/switches", HTTP_POST, handleSetSwitchCount);

  server.on("/api/schedules", HTTP_GET, handleGetSchedules);
  server.on("/api/schedules", HTTP_POST, handleAddSchedule);

  server.on("/api/wifi", HTTP_GET, handleGetWifi);
  server.on("/api/wifi/sta", HTTP_POST, handleSetStaWifi);
  server.on("/api/wifi/ap", HTTP_POST, handleSetApWifi);

  server.on("/api/time", HTTP_GET, handleGetTime);
  server.on("/api/time", HTTP_POST, handleSetTime);

  server.onNotFound(handleDynamicApiRoutes);
}

// ============================================================
// Defaults
// ============================================================
void initDefaults() {
  for (int i = 0; i < MAX_SWITCHES; i++) {
    switches[i].name = "Switch " + String(i + 1);

    switches[i].on.saved = false;
    switches[i].on.x = 0.0;
    switches[i].on.y = 0.0;

    switches[i].off.saved = false;
    switches[i].off.x = 0.0;
    switches[i].off.y = 0.0;
  }
}

// ============================================================
// Setup / Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Retrofit Switch starting...");

  initDefaults();

  // Initialize motor hardware and drivers
  stepper_driver::init();
  position_control::init();
  setupMotors();

  // Important: initialize your homing system
  homing::init();

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
  } else {
    Serial.println("SPIFFS mounted.");
    listSpiffsFiles("/");
  }

  connectToRouterWiFi();
  applyTimeSettings();

  setupRoutes();

  server.begin();

  Serial.println("HTTP server started.");
  Serial.println("Open this in browser:");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("http://");
    Serial.println(WiFi.softAPIP());
  }
}

void loop() {
  server.handleClient();
  checkSchedules();

  runMotors();
  position_control::update();
}