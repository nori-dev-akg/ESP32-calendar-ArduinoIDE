#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>

// ------- 初期設定ここから --------

// 使用するGFXのヘッダを有効にする
#include "myILI9488-ESP32.hpp"
//#include "myST7789-ESP32.hpp"
//#include "myILI9488-ESP32C6.hpp"
//#include "myST7789-ESP32C6.hpp"


const char *ssid = "your ssid";
const char *pass = "ssid password";

// ILI9488
#define DRAW_CALENDAR true // カレンダーを描画する
#define LCD_ROTATION 1 // 0-3
// ST7789
//#define DRAW_CALENDAR false // ST7789は小さいのでカレンダーを描画しない
//#define LCD_ROTATION 2 // 0-3

#define FOLDER_ID "zzzzzzzzzzzzzzzzzzzzzzzz" // 画像データ FOLDER_ID

#define COLOR_BACKGROUND_CAL "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" // 背景画像のFILE_ID 480x320
#define COLOR_BACKGROUND_IMG "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy" // 背景画像のFILE_ID 480x320

// ------- 初期設定ここまで --------

#define FONT_MONTHYEAR &fonts::Orbitron_Light_24 // 月と年のフォント
#define FONT_MONTHYEAR_SIZE 0.7 // 月と年のフォントサイズ
#define FONT_CALENDAR &fonts::Font2 // カレンダーのフォント
#define FONT_CALENDAR_SIZE 1 // カレンダーのフォントサイズ
#define FONT_TODAY &fonts::Orbitron_Light_24 // 今日のフォント
#define FONT_TODAY_HEIGHT (24.0*1.54) // 今日のフォント高さ
#define FONT_WEEKDAY_SIZE 1 // 曜日のフォントサイズ
#define FONT_DAY_SIZE 2.2 // 日付のフォントサイズ
#define MONTHYEAR_HEIGHT 30 // 月と年の高さ
#define COLOR_DAY_OF_SUNDAY 0xFBA0
#define COLOR_DAY_OF_SATURDAY TFT_GREEN
#define COLOR_DAY_OF_WEEKDAY TFT_WHITE
#define TYPE_CALENDAR 0 // カレンダー
#define TYPE_IMAGE 1 // イメージ

#define TOKEN_FILE "/access_token.txt"

#define DRAW_INTERVAL (10*60*1000) // 10分ごとに張替


#define WIFI_TIMEOUT 30000
bool init_all = false; // 初期化フラグ

LGFX lcd;

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--------------------------------");

  if (!init_wifi())
  {
    return;
  }

  if (!init_gdrive())
  {
    return;
  }

  if (!init_lcd())
  {
    return;
  }
  
  uint8_t *bg_cal = NULL; size_t bg_cal_size = 0;
  uint8_t *bg_img = NULL; size_t bg_img_size = 0;
  if((bg_cal_size = get_pic(COLOR_BACKGROUND_CAL, bg_cal)) > 0 && 
      (bg_img_size = get_pic(COLOR_BACKGROUND_IMG, bg_img)) > 0)
  {
    set_background(bg_cal, bg_cal_size, bg_img, bg_img_size); // 背景画像を設定
  }

  init_all = true;
}

void loop()
{
  if (!init_all)
  {
    delay(1000);
    return;
  }

  int interval = 100;
  static int pre_current_month = 0;
  
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  int current_year = timeinfo.tm_year + 1900;
  int current_month = timeinfo.tm_mon + 1;

  // 月が変わったら祝日を再取得
  if (pre_current_month != current_month) {
    pre_current_month = current_month;
    fetch_holidays(current_year, pre_current_month);
  }

  uint8_t *pic = NULL;
  int size;
  String pic_id, pic_name;
  if (get_random_pic(pic_id, pic_name, size)) // ランダムな画像を取得
  {
    Serial.printf("draw: ID: %s, Name: %s, Size: %d\n", pic_id.c_str(), pic_name.c_str(), size);
    if (size <= 50 * 1024) // ファイルサイズが50KB以下ならば
    {
      int16_t w, h;
      size = get_pic(pic_id.c_str(), pic); // ランダムな画像を取得
      if (size > 0)
      {
        if (get_jpeg_width_height(pic, size, &w, &h)) // 画像の幅と高さを取得
        {
          bool vertical = w < h;

          draw_background(vertical); // 背景を描画

          draw_picture(pic, size, w, h); // 画像を描画
          
          // カレンダーの描画
          if (DRAW_CALENDAR)
          {
            draw_calendar(vertical); // カレンダーを描画
          }

          // 今日、曜日の描画
          draw_today(vertical, DRAW_CALENDAR);

          interval = DRAW_INTERVAL; // 正常に描画できたときの間隔
        }
      }
    }
  }
  if(pic != NULL)
    free(pic);
  delay(interval);
}

bool init_lcd()
{
  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  lcd.fillScreen(TFT_BLACK);

  return true;
}

#define MAX_HOLIDAYS 31
int holidays[MAX_HOLIDAYS];
int holiday_count;
int holiday_month = 0;

void fetch_holidays(int year, int month)
{

  HTTPClient http;
  String month_str = String((month < 10) ? "0" : "") + String(month);  // 月は0埋めする必要がある
  String url = "https://api.national-holidays.jp/" + String(year) + "/" + month_str;
  Serial.println("Fetching holidays from: " + url);
  http.begin(url);

  int http_response_code = http.GET();
  if (http_response_code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      holiday_count = 0;
      for (JsonObject holiday : doc.as<JsonArray>()) {
        if (holiday_count < MAX_HOLIDAYS) {
          String date = holiday["date"].as<String>();
          int day = date.substring(date.lastIndexOf("-") + 1).toInt();
          holidays[holiday_count++] = day;
        } else {
          Serial.println("Too many holidays to store.");
          break;
        }
      }
    } else {
      Serial.println("Failed to parse holiday JSON.");
    }
  } else {
    Serial.printf("Failed to fetch holidays. HTTP code: %d\n", http_response_code);
  }

  http.end();
}

bool is_holiday(int day)
{
  for (int i = 0; i < holiday_count; i++) {
    if (holidays[i] == day) {
      return true; // 祝日
    }
  }
  return false; // 平日
}

bool get_jpeg_width_height(const uint8_t *data, size_t size, int16_t *width, int16_t *height)
{
  *width = -1;
  *height = -1;

  for (size_t i = 0; i < size - 1; ++i) {
    if (data[i] == 0xFF) {
      uint8_t marker = data[i + 1];

      // SOFマーカーをチェック (0xC0 - 0xC3, 0xC5 - 0xC7, 0xC9 - 0xCB, 0xCD - 0xCF)
      if ((marker >= 0xC0 && marker <= 0xCF) && 
          (marker != 0xC4 && marker != 0xC8 && marker != 0xCC)) {
        // マーカーの後に少なくとも8バイトが存在するか確認
        if (i + 8 < size) {
          *height = (data[i + 5] << 8) | data[i + 6];
          *width = (data[i + 7] << 8) | data[i + 8];
          return (*height > 0 && *width > 0); // 幅と高さが取得できたらtrueを返す
        }
      }

      // スキップ可能なマーカー (0xD0 - 0xD9など)
      if (marker == 0xD0 || marker == 0xD1 || marker == 0xD2 || marker == 0xD3 ||
          marker == 0xD4 || marker == 0xD5 || marker == 0xD6 || marker == 0xD7 ||
          marker == 0xD8 || marker == 0xD9) {
        continue;
      }

      // マーカーの長さを取得してスキップ
      if (i + 3 < size) {
        uint16_t segment_length = (data[i + 2] << 8) | data[i + 3];
        i += segment_length + 1; // マーカー全体をスキップ
      }
    }
  }

  return false; // 幅と高さが取得できなかった場合
}

uint8_t *bg_cal = NULL; size_t bg_cal_size = 0;
uint8_t *bg_img = NULL; size_t bg_img_size = 0;

void set_background(uint8_t *cal, size_t cal_size, uint8_t *img, size_t img_size)
{
  if (bg_cal != NULL) {
    free(bg_cal); // 前の画像を解放
  }
  if (bg_img != NULL) {
    free(bg_img); // 前の画像を解放
  }
  bg_cal = cal;
  bg_cal_size = cal_size;
  
  bg_img = img;
  bg_img_size = img_size;
}

void draw_background(bool vertical)
{
  if (bg_cal == NULL || bg_img == NULL) {
    return; // 背景画像が設定されていない場合は何もしない
  }
  
  int lcd_width = lcd.width();
  int lcd_height = lcd.height();
  int image_width, image_height, image_x, image_y, image_off_x, image_off_y;
  int cal_width, cal_height, cal_x, cal_y, cal_off_x, cal_off_y;
  
  if (vertical) {
    image_width = lcd_width / 3 * 2;
    image_height = lcd_height;
    image_x = lcd_width / 3;
    image_y = 0;
    image_off_x = lcd_width / 3;
    image_off_y = 0;
    cal_width = lcd_width / 3;
    cal_height = lcd_height;
    cal_x = 0;
    cal_y = 0;
    cal_off_x = 0;
    cal_off_y = 0;
  } else {
    image_width = lcd_width;
    image_height = round((float)lcd_height / 3 * 2);
    image_x = 0;
    image_y = 0;
    image_off_x = 0;
    image_off_y = 0;
    cal_width = lcd_width;
    cal_height = round((float)lcd_height / 3);
    cal_x = 0;
    cal_y = round((float)lcd_height / 3 * 2);
    cal_off_x = 0;
    cal_off_y = round((float)lcd_height / 3 * 2);
  }
  lcd.drawJpg(bg_cal, bg_cal_size, cal_x, cal_y, cal_width, cal_height, cal_off_x, cal_off_y);
  lcd.drawJpg(bg_img, bg_img_size, image_x, image_y, image_width, image_height, image_off_x, image_off_y);
}

void draw_picture(const uint8_t *data, size_t size, int16_t width, int16_t height)
{
  int16_t original_width = width;
  int16_t original_height = height;

  int lcd_width = lcd.width();
  int lcd_height = lcd.height();
  int image_width, image_height, target_width, target_height;
  int cal_width, cal_height, image_x, image_y;
  int monthyear_height = 0;
 
  if (original_width < original_height)  { // 縦長 vertical
    image_width = lcd_width / 3 * 2;
    image_height = lcd_height;
    target_width = image_width - 20;
    target_height = image_height - 20;
    cal_width = 0;
    cal_height = 0;
    image_x = lcd_width / 3;
    image_y = 0;
  }
  else { // 横長 horizontal
    image_width = lcd_width;
    image_height = lcd_height / 3 * 2;
    target_width = image_width - 20;
    target_height = image_height - 20;
    cal_width = 0;
    cal_height = 0;
    image_x = 0;
    image_y = 0;
  }

  float scale_width = (float)target_width / original_width;
  float scale_height = (float)target_height / original_height;
  float scale = min(scale_width, scale_height); // 小さい方をscaleに
  int offset_x = (target_width - (int)(original_width * scale)) / 2 + image_x + 10;
  int offset_y = (target_height - (int)(original_height * scale)) / 2 + image_y + 10;
  int shadow_offset_x = offset_x + 5; // 影のXオフセット
  int shadow_offset_y = offset_y + 5; // 影のYオフセット

  // 影
  lcd.fillRect(shadow_offset_x, shadow_offset_y, original_width * scale, original_height * scale, TFT_DARKGREY);

  lcd.drawJpg(data, size, offset_x, offset_y, 0, 0, 0, 0, scale);

}

int get_day_color(int day_of_week, int day)
{
  if (day_of_week == 0 || is_holiday(day)) {
    return COLOR_DAY_OF_SUNDAY;
  } else if (day_of_week == 6) {
    return COLOR_DAY_OF_SATURDAY;
  } else {
    return COLOR_DAY_OF_WEEKDAY;
  }
}

void draw_calendar(bool vertical)
{
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  int current_year = timeinfo.tm_year + 1900;
  int current_month = timeinfo.tm_mon + 1;

  const char *months[] = {"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"};

  const char *days_of_week[] = {"S", "M", "T", "W", "T", "F", "S"};
  int current_day = timeinfo.tm_mday;
  int current_wday = timeinfo.tm_wday; // 0: Sunday, 1: Monday, ..., 6: Saturday
  int first_day_of_week = (current_wday - (current_day % 7) + 7) % 7;
  int days_in_month[] = {31, (current_year % 4 == 0 && (current_year % 100 != 0 || current_year % 400 == 0)) ? 
    29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = days_in_month[current_month - 1];
  
  int cal_width, cal_height;
  int cell_width, cell_height;
  int start_x = 0, start_y = 0;
  if (vertical) {
    cal_width = lcd.width() / 3;
    cal_height = lcd.height() / 2 - MONTHYEAR_HEIGHT;
    cell_width = (int)round((double)cal_width / 7);
    cell_height = (int)round((double)cal_height / 7);
    start_x = 0;
    start_y = 0 + MONTHYEAR_HEIGHT;
  } else {
    cal_width = lcd.width() / 2;
    cal_height = lcd.height() / 3;
    cell_width = (int)round((double)cal_width / 7);
    cell_height = (int)round((double)cal_height / 7);
    start_x = 0;
    start_y = lcd.height() / 3 * 2;
  }

  lcd.setFont(FONT_CALENDAR);
  lcd.setTextSize(FONT_CALENDAR_SIZE);

  for (int i = 0; i < 7 + days; i++) {
    if (i < 7) {
      lcd.setTextColor(get_day_color(i, 0));

      int day_text_width = lcd.textWidth(days_of_week[i]);
      int day_centered_x = start_x + i * cell_width + (cell_width - day_text_width) / 2;
      lcd.setCursor(day_centered_x, start_y);
      lcd.print(days_of_week[i]);
    } else {
      int d = i - 7 + 1;
      int column = (first_day_of_week + d ) % 7;
      int row = (first_day_of_week + d ) / 7;

      lcd.setTextColor(get_day_color(column, d));

      String day_string = String(d);
      int day_text_width = lcd.textWidth(day_string);
      int day_centered_x = start_x + column * cell_width + (cell_width - day_text_width) / 2;
      int day_centered_y = start_y + (row + 1) * cell_height;
      lcd.setCursor(day_centered_x, day_centered_y);
      lcd.print(d);

      if (d == current_day) {
        int font_height = lcd.fontHeight();
        int underline_y = day_centered_y + font_height + 1;
        lcd.drawFastHLine(day_centered_x, underline_y, day_text_width, TFT_CYAN);
      }
    }
  }
}

void draw_yearmonth(bool vertical, bool calendar)
{

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  int current_year = timeinfo.tm_year + 1900;
  int current_month = timeinfo.tm_mon + 1;

  const char *months[] = {"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"};
  
  int month_x, month_y;
  int month_width, month_height;
  if (vertical) {
    month_x = 0;
    month_y = 0;
    month_width = lcd.width() / 3;
    month_height = MONTHYEAR_HEIGHT;
  } else {
    if (calendar) {
      month_x = lcd.width() / 2;
      month_y = lcd.height() / 3 * 2;
      month_width = lcd.width() / 2;
      month_height = MONTHYEAR_HEIGHT;
    } else {
      month_x = 0;
      month_y = lcd.height() / 3 * 2;
      month_width = lcd.width() / 2;
      month_height = MONTHYEAR_HEIGHT;
    }
  }

  lcd.setFont(FONT_MONTHYEAR);
  lcd.setTextSize(FONT_MONTHYEAR_SIZE);
  String month_year = String(months[current_month - 1]) + " " + String(current_year);
  int text_width = lcd.textWidth(month_year);
  int font_height = lcd.fontHeight();
  int centered_x = max(month_x + (month_width - text_width) / 2, 0);
  int centered_y = max(month_y + (month_height - font_height) / 2, 0);

  // 影
  lcd.setTextColor(TFT_DARKGRAY);
  lcd.setCursor(centered_x+5, centered_y+5);
  lcd.print(month_year);

  lcd.setTextColor(TFT_WHITE);
  lcd.setCursor(centered_x, centered_y);
  lcd.print(month_year);
}

void draw_today(bool vertical, bool calendar)
{
  draw_yearmonth(vertical, calendar);

  int weekday_x, weekday_y, day_x, day_y;
  int weekday_width, weekday_height, day_width, day_height;
  if(calendar) {
    if (vertical) {
      weekday_x = 0;
      weekday_y = lcd.height() / 2;
      day_x = 0;
      day_y = lcd.height() / 6 * 4;
      weekday_width = lcd.width() / 3;
      weekday_height = lcd.height() / 6;
      day_width = lcd.width() / 3;
      day_height = lcd.height() / 6 * 2;
    } else {
      weekday_x = lcd.width() / 2;
      weekday_y = lcd.height() / 3 * 2 + MONTHYEAR_HEIGHT;
      day_x = lcd.width() / 6 * 4;
      day_y = lcd.height() / 3 * 2 + MONTHYEAR_HEIGHT;
      weekday_width = lcd.width() / 6;
      weekday_height = lcd.height() / 3 - MONTHYEAR_HEIGHT;
      day_width = lcd.width() / 6 * 2;
      day_height = lcd.height() / 3 - MONTHYEAR_HEIGHT;
    }
  } else {
    if (vertical) {
      weekday_x = 0;
      weekday_y = 0 + MONTHYEAR_HEIGHT;
      day_x = 0;
      day_y = lcd.height()/ 2;
      weekday_width = lcd.width() / 3;
      weekday_height = lcd.height() / 2 - MONTHYEAR_HEIGHT;
      day_width = lcd.width() / 3;
      day_height = lcd.height() / 2;
    } else {
      weekday_x = 0;
      weekday_y = lcd.height() / 3 * 2 + MONTHYEAR_HEIGHT;
      day_x = lcd.width() / 2;
      day_y = lcd.height() / 3 * 2;
      weekday_width = lcd.width() / 2;
      weekday_height = lcd.height() / 3 - MONTHYEAR_HEIGHT;
      day_width = lcd.width() / 2;
      day_height = lcd.height() / 3;
    }
  }


  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  const char *days_of_week_short[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int current_year = timeinfo.tm_year + 1900;
  int current_month = timeinfo.tm_mon + 1;
  int current_day = timeinfo.tm_mday;
  int current_wday = timeinfo.tm_wday; // 0: Sunday, 1: Monday, ..., 6: Saturday

  lcd.setFont(FONT_TODAY);
  
  // 曜日
  lcd.setTextSize(FONT_WEEKDAY_SIZE);
  int font_height = FONT_TODAY_HEIGHT * FONT_WEEKDAY_SIZE;
  String today_weekday = String(days_of_week_short[current_wday]);
  int text_width = lcd.textWidth(today_weekday);
  int centered_x = max(0, weekday_x + (weekday_width - text_width) / 2);
  int centered_y = max(0, weekday_y + (weekday_height - font_height) / 2);

  // 影
  lcd.setTextColor(TFT_DARKGRAY);
  lcd.setCursor(centered_x + 5, centered_y + 5);
  lcd.print(today_weekday);

  lcd.setTextColor(get_day_color(current_wday, current_day));
  lcd.setCursor(centered_x, centered_y);
  lcd.print(today_weekday);

  // 日付
  lcd.setTextSize(FONT_DAY_SIZE);
  font_height = FONT_TODAY_HEIGHT * FONT_DAY_SIZE;
  String today_day = String(current_day);
  text_width = lcd.textWidth(today_day);
  centered_x = max(0, day_x + (day_width - text_width) / 2);
  centered_y = max(0, day_y + (day_height - font_height) / 2);

  // 影
  lcd.setTextColor(TFT_DARKGRAY);
  lcd.setCursor(centered_x + 5, centered_y + 5);
  lcd.print(today_day);

  lcd.setTextColor(get_day_color(current_wday, current_day));
  lcd.setCursor(centered_x, centered_y);
  lcd.print(today_day);
}


// ファイル情報
struct FileInfo {
  String id;
  String name;
  long size;
};
#define MAX_FILES 100
FileInfo file_list[MAX_FILES];
int file_count = 0;

String global_access_token = "";

#define GDRIVE_RETRY 3

bool init_gdrive() {
  randomSeed(millis());

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return false;
  }
  
  global_access_token = reget_access_token();
  if (global_access_token == "") {
    write_spiffs("", "", ""); // Clear the token file if access token is empty
    global_access_token = reget_access_token();
    if (global_access_token == "") {
      return false;
    }
  }

  drive_files(global_access_token);

  return true;
}

String reget_access_token() {

  String client_id = "";
  String client_secret = "";
  String refresh_token = "";
  String access_token = "";

  if (!read_spiffs(client_id, client_secret, refresh_token)) {
    Serial.println("SPIFFS read error");
    return "";
  }

  if (refresh_token.length() == 0) {
    Serial.println("Refresh token is empty. Please authenticate.");
    while (!authenticate(client_id, client_secret, refresh_token, access_token)) {
      Serial.println("Authentication failed.");
      delay(1000);
    }
    write_spiffs(client_id, client_secret, refresh_token);
  }

  if (access_token == "") {
    if (!get_access_token(refresh_token, client_id, client_secret, access_token)) {
      Serial.println("Failed to get access token.");
      return "";
    }
  }
  return access_token;
}

// URLエンコード関数
String url_encode(String str) {
  String encoded_string = "";
  for (char c : str) {
    if (isalnum(c) || (c == '-') || (c == '_') || (c == '.') || (c == '~')) {
      encoded_string += c;
    } else if (c == ' ') {
      encoded_string += '+';
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded_string += buf;
    }
  }
  return encoded_string;
}

bool get_refresh_token(String &code, String &client_id, String &client_secret, String &refresh_token, String &access_token) {
  bool ret = false;
  HTTPClient http;
  http.begin("https://accounts.google.com/o/oauth2/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String post_data = String("grant_type=authorization_code") +
                    "&code=" + url_encode(code) +
                    "&client_id=" + client_id +
                    "&client_secret=" + url_encode(client_secret) +
                    "&redirect_uri=" + url_encode("urn:ietf:wg:oauth:2.0:oob");

  int http_response_code = http.POST(post_data);

  if (http_response_code > 0) {
    if (http_response_code == HTTP_CODE_OK) {
      String payload = http.getString();

      JsonDocument json_buffer;
      DeserializationError error = deserializeJson(json_buffer, payload);

      if (!error) {
        if (json_buffer["access_token"].is<String>()) {
          access_token = json_buffer["access_token"].as<String>();
          Serial.print("Access Token: ");
          Serial.println(access_token);
        } else {
          Serial.println("Access token not found in the response.");
        }
        if (json_buffer["refresh_token"].is<String>()) {
          refresh_token = json_buffer["refresh_token"].as<String>();
          Serial.print("Refresh Token: ");
          Serial.println(refresh_token);
          ret = true;
        }
      } else {
        Serial.print("JSON Deserialization failed: ");
        Serial.println(error.c_str());
        ret = false;
      }
    }
  } else {
    Serial.printf("HTTP POST request failed, error: %s\n", http.errorToString(http_response_code).c_str());
    ret = false;
  }

  http.end();

  return ret;
}

bool get_access_token(String &refresh_token, String &client_id, String &client_secret, String &access_token) {
  bool ret = false;

  String post_data = String("grant_type=refresh_token") +
                    "&refresh_token=" + url_encode(refresh_token) +
                    "&client_id=" + client_id +
                    "&client_secret=" + url_encode(client_secret);

  for (int i = 0; i < GDRIVE_RETRY && !ret; i++) {
    HTTPClient http;
    if (http.begin("https://accounts.google.com/o/oauth2/token")) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int http_response_code = http.POST(post_data);
      if (http_response_code == HTTP_CODE_OK) {
        String payload = http.getString();

        JsonDocument json_buffer;
        DeserializationError error = deserializeJson(json_buffer, payload);

        if (!error) {
          if (json_buffer["access_token"].is<String>()) {
            access_token = json_buffer["access_token"].as<String>();
            Serial.print("New Access Token: ");
            Serial.println(access_token);
            ret = true;
          } else {
            Serial.println("Access token not found in the refresh token response.");
          }
          if (json_buffer["refresh_token"].is<String>()) {
            String new_refresh_token = json_buffer["refresh_token"].as<String>();
            if (new_refresh_token != refresh_token) {
              Serial.print("New Refresh Token: ");
              Serial.println(new_refresh_token);
              refresh_token = new_refresh_token;
              write_spiffs(client_id, client_secret, refresh_token);
            }
          }
        } else {
          Serial.print("JSON Deserialization failed: ");
          Serial.println(error.c_str());
        }
      } else {
        Serial.printf("POST(): request failed, error: %s\n", http.errorToString(http_response_code).c_str());
      }
    } else {
      Serial.println("begin(): Connection failed");
    }

    http.end();

    if (!ret) {
      delay(1000); // wait before retrying
      Serial.printf("Retrying... (%d/%d)\n", i + 1, GDRIVE_RETRY);
    }
  }

  return ret;
}

void drive_files(String access_token) {
  HTTPClient https;
  String query = "?q=%27" + String(FOLDER_ID) + "%27%20in%20parents%20and%20mimeType%3D%27image%2Fjpeg%27&fields=files(id,name,size)";
  String url = "https://www.googleapis.com/drive/v3/files" + query;

  if (https.begin(url)) {
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + access_token);

    int http_response_code = https.GET();
    String body = "";
    if (http_response_code == HTTP_CODE_OK) {
      body = https.getString();
      Serial.printf("Received data length: %d\n", body.length());

      JsonDocument filter;
      JsonDocument filter_body;
      filter["files"][0]["id"] = true;
      filter["files"][0]["name"] = true;
      filter["files"][0]["size"] = true;
      deserializeJson(filter_body, body, DeserializationOption::Filter(filter));

      int i = 0;
      while (1) {
        String temp_id = filter_body["files"][i]["id"];
        String temp_name = filter_body["files"][i]["name"];
        long temp_size = filter_body["files"][i]["size"];

        if (temp_id.equals("null")) {
          break;
        }

        file_list[file_count].id = temp_id;
        file_list[file_count].name = temp_name;
        file_list[file_count].size = temp_size;
        file_count++;
        i++;
      }
      Serial.println("Total files: " + String(file_count));

      https.end();
    } else {
      Serial.printf("drive_files:HTTP GET failed, error: %s\n", https.errorToString(http_response_code).c_str());
    }
  } else {
    Serial.println("Connection failed");
  }
}

int get_pic_drive(String access_token, String image_id, uint8_t *&pic) {
  HTTPClient https;

  if (https.begin("https://www.googleapis.com/drive/v3/files/" + image_id + "?alt=media")) {
    https.addHeader("Authorization", "Bearer " + access_token);
    int http_response_code = https.GET();
    size_t size = https.getSize();
    if (http_response_code == HTTP_CODE_OK || http_response_code == HTTP_CODE_MOVED_PERMANENTLY) {
      WiFiClient *stream = https.getStreamPtr();
      pic = (uint8_t *)malloc(size);

      size_t offset = 0;
      while (https.connected()) {
        size_t len = stream->available();
        if (!len) {
          delay(1);
          continue;
        }

        stream->readBytes(pic + offset, len);
        offset += len;
        log_d("%d / %d", offset, size);
        if (offset >= size) {
          break;
        }
      }
    } else {
      Serial.printf("get_pic_drive:HTTP GET failed, error: %s\n", https.errorToString(http_response_code).c_str());
    }

    https.end();
    return size;
  } else {
    Serial.println("Connection failed");
  }
  return 0;
}

bool init_wifi() {
  Serial.print("Connecting WiFi.");
  WiFi.begin(ssid, pass);
  bool stat_wifi = false;

  int n;
  long timeout = millis();
  while ((n = WiFi.status()) != WL_CONNECTED && (millis() - timeout) < WIFI_TIMEOUT) {
    Serial.print(".");
    delay(500);
    if (n == WL_NO_SSID_AVAIL || n == WL_CONNECT_FAILED) {
      delay(1000);
      WiFi.reconnect();
    }
  }
  if (n == WL_CONNECTED) {
    stat_wifi = true;
    Serial.println(" connected.");

    configTime(9 * 3600L, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");
    while (time(nullptr) < 9 * 3600L * 2) {
      delay(1000);
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    Serial.printf("Current time: %04d/%02d/%02d %02d:%02d:%02d\n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  } else {
    stat_wifi = false;
    Serial.println("WiFi connection failed.");
  }
  return stat_wifi;
}

bool read_spiffs(String &client_id, String &client_secret, String &refresh_token) {
  File fd = SPIFFS.open(TOKEN_FILE, "r");
  if (!fd) {
    Serial.println("Config open error");
    return false;
  }
  client_id = fd.readStringUntil('\n');
  client_id.trim();
  Serial.println("Client ID:" + client_id);
  client_secret = fd.readStringUntil('\n');
  client_secret.trim();
  Serial.println("Client Secret:" + client_secret);
  refresh_token = fd.readStringUntil('\n');
  refresh_token.trim();
  Serial.println("Refresh token:" + refresh_token);
  fd.close();
  return true;
}

bool write_spiffs(String client_id, String client_secret, String refresh_token) {
  File fd = SPIFFS.open(TOKEN_FILE, "w");
  if (!fd) {
    Serial.println("Config open error");
    return false;
  }
  fd.println(client_id);
  fd.println(client_secret);
  fd.println(refresh_token);
  fd.close();
  Serial.println("Config write OK");
  return true;
}

bool authenticate(String &client_id, String &client_secret, String &refresh_token, String &access_token) {
  Serial.println("Client ID?");
  while (!Serial.available()) {
    delay(10);
  }
  client_id = Serial.readStringUntil('\n');
  client_id.trim();

  Serial.println("Please access to");
  Serial.println("https://accounts.google.com/o/oauth2/auth?client_id=" + client_id +
                 "&response_type=code&redirect_uri=urn:ietf:wg:oauth:2.0:oob&scope=https://www.googleapis.com/auth/drive");

  Serial.println("authorization code?");
  while (!Serial.available()) {
    delay(10);
  }
  String code = Serial.readStringUntil('\n');
  code.trim();

  Serial.println("Client secret?");
  while (!Serial.available()) {
    delay(10);
  }
  client_secret = Serial.readStringUntil('\n');
  client_secret.trim();

  Serial.println("OK");
  delay(1000);

  bool ret = get_refresh_token(code, client_id, client_secret, refresh_token, access_token);
  if (ret && refresh_token.length() > 0) {
    Serial.println("OK");
    return true;
  } else {
    Serial.println("NG");
    return false;
  }
}

int get_pic(const char *file_id, uint8_t *&pic) {
  int size = get_pic_drive(global_access_token, String(file_id), pic);
  if (size <= 0) {
    global_access_token = reget_access_token();
    size = get_pic_drive(global_access_token, String(file_id), pic);
  }
  return size;
}

bool get_random_pic(String &id, String &name, int &size) {
  if (file_count == 0) {
    Serial.println("File list is empty.");
    return false;
  }

  int random_index = random(file_count);

  name = file_list[random_index].name;
  id = file_list[random_index].id;
  size = file_list[random_index].size;

  return true;
}
