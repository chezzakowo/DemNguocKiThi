#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================== Config cơ bản ==================

// Thông tin Wi-Fi để ESP32 kết nối
#define WIFI_SSID     "MẬT KHẨU WIFI CỦA BẠN"  // Thay bằng mật khẩu Wi-Fi của bạn
#define WIFI_PASSWORD "MẬT KHẨU WIFI CỦA BẠN"  // Thay bằng mật khẩu Wi-Fi của bạn

// Thời gian tắt / bật màn hình
// (Cái này sử dụng định dạng 24 giờ, HH:MM)
const char* TURN_OFF_SCREEN_TIME[] = {""};
const char* TURN_ON_SCREEN_TIME[]  = {""};

// API URL Chứa lịch thi
#define LICH_THI_API  "https://raw.githubusercontent.com/chezzakowo/demnguockithiC3CanTho/refs/heads/main/api/demnguoc/lichthi.json"

// Cài đặt chân màn hình LCD I2C cho ESP32 (ĐỪNG CHỈNH NẾU BẠN KHÔNG BIẾT MÌNH NÓ GÌ HẾT) 
#define I2C_SCL_PIN   22
#define I2C_SDA_PIN   21
#define LCD_I2C_ADDR  0x27


// ================== Tên biến ==================

// Cấu trúc lưu thời gian
struct DateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

// Lưu thời gian
DateTime ngayThi  = {2025, 6, 5, 0, 0, 0};  // Lịch tạm đề phòng nếu nó truy xuất lỗi từ API
DateTime cachedTime;

// Cái này để hiện chữ
String screenDisplay = "Dem nguoc ki thi";

// CÁi này để đồng bộ với API thời gian.
unsigned long lastSyncTime = 0;
bool screenState = true;
bool screenOff   = false;

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcd.clear();
  lcd.print("Dang ket noi toi Wi-Fi...");
  Serial.println("Dang ket noi toi Wi-Fi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nDa ket noi!");
  lcd.clear();
  lcd.print("Da ket noi!");
  delay(1000);
  lcd.clear();
}

DateTime fetchTimeFromAPI() {
  // Lấy thông tin thời gian từ API timeapi.io
  // Cấu trúc lưu thời gian
  DateTime timeData = {0, 0, 0, 0, 0, 0};

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Chua ket noi toi Wi-FI!");
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
      Serial.println("Loi phan tich JSON tu API.");
    }
  } else {
    Serial.print("Loi: ");
    Serial.println(httpCode);
  }
  http.end();

  return timeData;
}

// Lấy thông tin lịch thi từ API
void fetchLichThi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Chua ket noi toi Wi-FI!");
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
      Serial.println("Loi phan tich API lichthi!.");
      // Fallback
      screenDisplay = "Dem nguoc ki thi";
    }
  } else {
    Serial.print("Loi lay lich thi: ");
    Serial.println(httpCode);
    // Fallback
    screenDisplay = "Dem nguoc ki thi";
  }
  http.end();
}

// ================== Đếm ngược ==================

unsigned long calculateCountdown(DateTime target, DateTime current) {
  // Đổi thời gian
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
    return target_time - current_time; // Từ đây -> ngày thi
  } else {
    return 0; // Đã qua ngày thi
  }
}

void checkScreenStatus(DateTime currentTime) {
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", currentTime.hour, currentTime.minute);

  for (int i = 0; i < sizeof(TURN_OFF_SCREEN_TIME) / sizeof(TURN_OFF_SCREEN_TIME[0]); i++) {
    if (strcmp(timeStr, TURN_OFF_SCREEN_TIME[i]) == 0 && screenState) {
      lcd.noBacklight();
      Serial.printf("Screen OFF at %s\n", timeStr);
      screenState = false;
      screenOff   = true;
    }
  }

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


void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.backlight();

  connectToWiFi();
  fetchLichThi();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(screenDisplay);

  cachedTime = fetchTimeFromAPI();
  if (cachedTime.year == 0) {
    Serial.println("Loi lay lich thi tu API. Dang su dung ngay du phong: 2025-06-05.");
    cachedTime = ngayThi; 
  }

  lastSyncTime = millis();
}

void loop() {
  // Đồng bộ lại thời gian sau mỗi 30 phút
  if (millis() - lastSyncTime >= 1800000UL) {
    cachedTime   = fetchTimeFromAPI();
    lastSyncTime = millis();
  }

  unsigned long countdown = calculateCountdown(ngayThi, cachedTime);
  displayCountdown(countdown);

  checkScreenStatus(cachedTime);

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
        // Cảm ơn ChatGPT vì dòng này nhưng mà tôi ngu C++ 🐧
      }
    }
  }

  delay(1000);
}
