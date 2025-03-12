#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================== USER CONFIG ==================

// Your Wi-Fi credentials
#define WIFI_SSID     "MẬT KHẨU WIFI CỦA BẠN"  // Thay bằng mật khẩu Wi-Fi của bạn
#define WIFI_PASSWORD "MẬT KHẨU WIFI CỦA BẠN"  // Thay bằng mật khẩu Wi-Fi của bạn

// The API URL that returns JSON data for exam schedule
#define LICH_THI_API  "https://raw.githubusercontent.com/chezzakowo/demnguockithiC3CanTho/refs/heads/main/api/demnguoc/lichthi.json"

// LCD settings
#define I2C_SCL_PIN   22
#define I2C_SDA_PIN   21
#define LCD_I2C_ADDR  0x27

// Times of day to turn the screen OFF and ON
// (use 24-hour format, HH:MM)
const char* TURN_OFF_SCREEN_TIME[] = {""};
const char* TURN_ON_SCREEN_TIME[]  = {""};

// ================== GLOBALS ==================

// A simple DateTime struct for storing time
struct DateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

// We'll store the exam date/time and the current time here
DateTime ngayThi  = {2025, 6, 5, 0, 0, 0};  // Default fallback
DateTime cachedTime;

// This string will hold the message to display on the top row of the LCD
String screenDisplay = "Dem nguoc ki thi";

// Variables to track when we last synced time from API, etc.
unsigned long lastSyncTime = 0;
bool screenState = true;
bool screenOff   = false;

// Create an I2C LCD instance
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

// ================== WIFI & HTTP FUNCTIONS ==================

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcd.clear();
  lcd.print("Connecting WiFi...");
  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  lcd.clear();
  lcd.print("WiFi connected!");
  delay(1000);
  lcd.clear();
}

DateTime fetchTimeFromAPI() {
  // Attempt to get current time from Time API
  // Return a DateTime struct
  DateTime timeData = {0, 0, 0, 0, 0, 0};

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot fetch time.");
    return timeData;
  }

  HTTPClient http;
  http.begin("https://www.timeapi.io/api/Time/current/zone?timeZone=Asia/Ho_Chi_Minh");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      timeData.year   = doc["year"];
      timeData.month  = doc["month"];
      timeData.day    = doc["day"];
      timeData.hour   = doc["hour"];
      timeData.minute = doc["minute"];
      timeData.second = doc["seconds"];
    } else {
      Serial.println("JSON parse error for time API.");
    }
  } else {
    Serial.print("Time API GET error: ");
    Serial.println(httpCode);
  }
  http.end();

  return timeData;
}

// Fetch exam schedule from LICH_THI_API, parse JSON, update global vars
void fetchLichThi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot fetch exam schedule.");
    return;
  }

  HTTPClient http;
  http.begin(LICH_THI_API);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      // Update ngayThi from JSON
      ngayThi.year   = doc["nam"]   | 2025;
      ngayThi.month  = doc["thang"] | 6;
      ngayThi.day    = doc["ngay"]  | 5;
      ngayThi.hour   = doc["gio"]   | 0;
      ngayThi.minute = doc["phut"]  | 0;
      ngayThi.second = doc["giay"]  | 0;

      // Check "trang_thai" field for display message
      const char* trangThai = doc["trang_thai"] | "chinh_thuc";
      if (strcmp(trangThai, "chinh_thuc") == 0) {
        screenDisplay = "Dem nguoc ki thi";
      } else if (strcmp(trangThai, "du_kien") == 0) {
        screenDisplay = "(Du kien) Thi C3";
      } else {
        screenDisplay = "Trang thai khong xac dinh";
      }
    } else {
      Serial.println("JSON parse error for exam schedule.");
      // Fallback
      screenDisplay = "Dem nguoc ki thi";
    }
  } else {
    Serial.print("Exam schedule GET error: ");
    Serial.println(httpCode);
    // Fallback
    screenDisplay = "Dem nguoc ki thi";
  }
  http.end();
}

// ================== COUNTDOWN & SCREEN CONTROL ==================

unsigned long calculateCountdown(DateTime target, DateTime current) {
  // Convert the DateTime to 'time_t' (seconds since epoch)
  struct tm target_tm = {0};
  target_tm.tm_year   = target.year - 1900;
  target_tm.tm_mon    = target.month - 1;
  target_tm.tm_mday   = target.day;
  target_tm.tm_hour   = target.hour;
  target_tm.tm_min    = target.minute;
  target_tm.tm_sec    = target.second;
  time_t target_time  = mktime(&target_tm);

  struct tm current_tm = {0};
  current_tm.tm_year   = current.year - 1900;
  current_tm.tm_mon    = current.month - 1;
  current_tm.tm_mday   = current.day;
  current_tm.tm_hour   = current.hour;
  current_tm.tm_min    = current.minute;
  current_tm.tm_sec    = current.second;
  time_t current_time  = mktime(&current_tm);

  if (target_time > current_time) {
    return target_time - current_time; // seconds until target
  } else {
    return 0; // already passed
  }
}

void checkScreenStatus(DateTime currentTime) {
  // Construct current HH:MM string
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", currentTime.hour, currentTime.minute);

  // Check if we should turn off
  for (int i = 0; i < sizeof(TURN_OFF_SCREEN_TIME) / sizeof(TURN_OFF_SCREEN_TIME[0]); i++) {
    if (strcmp(timeStr, TURN_OFF_SCREEN_TIME[i]) == 0 && screenState) {
      lcd.noBacklight();
      Serial.printf("Screen OFF at %s\n", timeStr);
      screenState = false;
      screenOff   = true;
    }
  }

  // Check if we should turn on
  for (int i = 0; i < sizeof(TURN_ON_SCREEN_TIME) / sizeof(TURN_ON_SCREEN_TIME[0]); i++) {
    if (strcmp(timeStr, TURN_ON_SCREEN_TIME[i]) == 0 && !screenState) {
      lcd.backlight();
      Serial.printf("Screen ON at %s\n", timeStr);
      screenState = true;
      screenOff   = false;
    }
  }
}

void displayCountdown(unsigned long countdownSeconds) {
  int days    = countdownSeconds / 86400;
  int hours   = (countdownSeconds % 86400) / 3600;
  int minutes = (countdownSeconds % 3600) / 60;
  int seconds = countdownSeconds % 60;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02dd %02dh %02dm %02ds", days, hours, minutes, seconds);

  if (!screenOff) {
    lcd.setCursor(0, 1);
    lcd.print(buf);
  }
}

// ================== ARDUINO SETUP & LOOP ==================

void setup() {
  Serial.begin(115200);

  // Initialize I2C and LCD
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();

  // Connect to WiFi
  connectToWiFi();

  // Fetch the exam schedule from the API
  fetchLichThi();

  // Display the message from 'trang_thai' logic on top row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(screenDisplay);

  // Get initial time from time API
  cachedTime = fetchTimeFromAPI();
  if (cachedTime.year == 0) {
    Serial.println("Failed to fetch initial time from API. Using fallback time = 2025-06-05.");
    // fallback if you want
    cachedTime = ngayThi; 
  }

  lastSyncTime = millis();
}

void loop() {
  // Resync time from API every 30 minutes
  if (millis() - lastSyncTime >= 1800000UL) {
    cachedTime   = fetchTimeFromAPI();
    lastSyncTime = millis();
  }

  // Calculate countdown
  unsigned long countdown = calculateCountdown(ngayThi, cachedTime);
  displayCountdown(countdown);

  // Check whether we should turn screen on/off
  checkScreenStatus(cachedTime);

  // Increment our cached time by 1 second
  cachedTime.second++;
  if (cachedTime.second >= 60) {
    cachedTime.second = 0;
    cachedTime.minute++;
    if (cachedTime.minute >= 60) {
      cachedTime.minute = 0;
      cachedTime.hour++;
      if (cachedTime.hour >= 24) {
        cachedTime.hour = 0;
        cachedTime.day++;
        // NOTE: This does not handle month rollover or leap years.
        // For a robust solution, you might re-fetch from API daily.
      }
    }
  }

  delay(1000);
}
