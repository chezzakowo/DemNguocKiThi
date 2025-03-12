import urequests
import ujson
import utime
import time
from machine import I2C, Pin
import network
from esp8266_i2c_lcd import I2cLcd

# Config WiFi
SSID = "TÊN WIFI CỦA BẠN"  # Thay bằng tên Wi-Fi của bạn
PASSWORD = "MẬT KHẨU WIFI CỦA BẠN"  # Thay bằng mật khẩu Wi-Fi của bạn

# API Lịch thi
Lich_Thi_API = "https://raw.githubusercontent.com/chezzakowo/demnguockithiC3CanTho/refs/heads/main/api/demnguoc/lichthi.json"



# Thời gian bật/tắt màn hình
Turn_Off_Screen_Time = [""]
Turn_On_Screen_Time = [""]

# Cấu hình màn hình LCD
I2C_SCL_PIN = 22  # Chân SCL
I2C_SDA_PIN = 21  # Chân SDA
I2C_ADDR = 0x27   # Địa chỉ LCD
i2c = I2C(0, scl=Pin(I2C_SCL_PIN), sda=Pin(I2C_SDA_PIN))
lcd = I2cLcd(i2c, I2C_ADDR, 2, 16)

cached_time = None   # Lưu thời gian tạm thời
last_sync_time = 0   # Thời điểm lần cuối đồng bộ với API 
screen_state = True  # Trạng thái màn hình (True: bật, False: tắt)
screen_off = False   # Cờ trạng thái tắt màn hình

def connect_to_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    print("Đang kết nối Wi-Fi...")
    lcd.putstr("Dang ket noi Wi-Fi...")
    wlan.connect(SSID, PASSWORD)
    while not wlan.isconnected():
        time.sleep(1)
    print("Wi-Fi đã kết nối!")
    lcd.clear()

# Data trống nếu lịch thi lỗi (Format: (year, month, day, hour, minute, second))
ngay_thi = (1, 1, 1, 0, 0, 0)

def fetch_note():
    try:
        response = urequests.get("https://chezzakowo.github.io/demnguockithiC3CanTho/api/demnguoc/note.json")
        if response.status_code == 200:
            data = ujson.loads(response.text)
            response.close()
            print(f"Note / Ghi chú: {data}")
        else:
            print("Lỗi truy xuất dữ liệu ghi chú:", response.status_code)
            response.close()
    except Exception as e:
        print("Lỗi truy xuất dữ liệu ghi chú:", e)
        return None

def fetch_lich_thi(url):
    try:
        response = urequests.get(url)
        if response.status_code == 200:
            data = ujson.loads(response.text)
            response.close()
            return data
        else:
            print("Lỗi truy xuất dữ liệu:", response.status_code)
            response.close()
            return None
    except Exception as e:
        print("Lỗi truy xuất dữ liệu:", e)
        return None

def get_time_from_api():
    url = "https://www.timeapi.io/api/Time/current/zone?timeZone=Asia/Ho_Chi_Minh"
    try:
        response = urequests.get(url)
        if response.status_code == 200:
            data = ujson.loads(response.text)
            response.close()
            return (data["year"], data["month"], data["day"], data["hour"], data["minute"], data["seconds"])
        response.close()
    except Exception as e:
        print("Lỗi lấy thời gian từ API:", e)
    return None

def dem_ngay(ngay_thi, ngay_hien_tai):
    try:
        # Append dummy values for weekday and yearday required by mktime
        target_timestamp = utime.mktime(ngay_thi + (0, 0))
        current_timestamp = utime.mktime(ngay_hien_tai + (0, 0))
        countdown_seconds = target_timestamp - current_timestamp
        if countdown_seconds < 0:
            return 0, 0, 0, 0
        days = countdown_seconds // 86400
        hours = (countdown_seconds % 86400) // 3600
        minutes = (countdown_seconds % 3600) // 60
        seconds = countdown_seconds % 60
        return days, hours, minutes, seconds
    except Exception as e:
        print("Lỗi dem_ngay:", e)
        return None

def check_screen_status(current_time):
    global screen_state, screen_off 
    time_str = "{:02}:{:02}".format(current_time[3], current_time[4])
    
    if time_str in Turn_Off_Screen_Time and screen_state:
        utime.sleep(0.5)
        lcd.hal_backlight_off()
        print(f"Tắt màn hình lúc {time_str}")
        screen_state = False
        screen_off = True
    
    if time_str in Turn_On_Screen_Time and not screen_state:
        lcd.hal_backlight_on()
        print(f"Bật màn hình lúc {time_str}")
        screen_state = True
        screen_off = False

def main():
    global cached_time, last_sync_time, ngay_thi
    connect_to_wifi()
    
    # Fetch lichthi data from the server
    lichthi_data = fetch_lich_thi(Lich_Thi_API)
    if lichthi_data:
        ngay_thi = (
            lichthi_data.get("nam"),
            lichthi_data.get("thang"),
            lichthi_data.get("ngay"),
            lichthi_data.get("gio"),
            lichthi_data.get("phut"),
            lichthi_data.get("giay")
        )
        trang_thai = lichthi_data.get("trang_thai", "chinhthuc")
        if trang_thai == "chinh_thuc":
            screen_display = "Dem nguoc ki thi"
        elif trang_thai == "du_kien":
            screen_display = "(Du kien) Thi C3"
        else:
            screen_display = "Trang thai khong xac dinh"
    else:
        ngay_thi = (2025, 6, 5, 0, 0, 0)
        screen_display = "Dem nguoc ki thi"
    lcd.move_to(0, 0)
    lcd.putstr(screen_display)
    
    # Get initial time from API
    cached_time = get_time_from_api()
    if cached_time is None:
        print("Failed to get time from API initially.")
    
    last_sync_time = utime.time()
    
    while True:
        current_time = utime.time()
        if (current_time - last_sync_time) >= 1800:  # cập nhật mỗi 30 phút
            cached_time = get_time_from_api()
            last_sync_time = current_time
        
        if cached_time:
            countdown = dem_ngay(ngay_thi, cached_time)
            if countdown is not None:
                days, hours, minutes, seconds = countdown
            else:
                days = hours = minutes = seconds = 0

            check_screen_status(cached_time)

            # Format countdown components to two digits
            days_str = f"{days:02}d"
            hours_str = f"{hours:02}h"
            minutes_str = f"{minutes:02}m"
            seconds_str = f"{seconds:02}s"

            countdown_str = f"{days_str} {hours_str} {minutes_str} {seconds_str}"
            print(countdown_str)

            if not screen_off:  # chỉ cập nhật màn hình nếu đang bật
                lcd.move_to(0, 1)
                lcd.putstr(countdown_str)

            # Tăng thời gian bộ nhớ đệm lên 1 giây
            new_seconds = cached_time[5] + 1
            new_minutes = cached_time[4] + (new_seconds // 60)
            new_seconds %= 60
            new_hours = cached_time[3] + (new_minutes // 60)
            new_minutes %= 60
            cached_time = (cached_time[0], cached_time[1], cached_time[2], new_hours, new_minutes, new_seconds)

        utime.sleep(1)

if __name__ == "__main__":
    main()
