#include "Arduino.h"
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFi.h>

// Pin Definitions
#define RXD2 16
#define TXD2 17
#define MOTION_PIN 5  // Motion sensor input pin
#define LED_PIN 4     // LED output pin (PWM supported)
#define ELEC_METRIC_PIN 13

int lightIntensity = 0;
int brightnessPercentage = 0;
String motion_status = "";

// Wi-Fi thông tin
const char* ssid = "IEC_2";            // Thay bằng tên Wi-Fi của bạn
const char* password = "123456789a@";  // Thay bằng mật khẩu Wi-Fi

const char* ntpServer = "asia.pool.ntp.org";  // NTP Server để đồng bộ
const long gmtOffset_sec = 7 * 3600;          // UTC +7 cho Việt Nam
const int daylightOffset_sec = 0;

int startHour, startMin, endHour, endMin;
bool scheduleSet = false;  // Biến cờ để kiểm tra nếu lịch trình đã được cài đặt

// Declare flags to track changes
bool statusChanged = false;
bool scheduleChanged = false;
bool brightnessChanged = false;
bool environmentChanged = false;

// Khởi tạo Serial và pin
void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(500);  // Cho thời gian để thiết lập

  pinMode(MOTION_PIN, INPUT);  // Motion sensor pin as input
  pinMode(LED_PIN, OUTPUT);    // LED pin as output

  // Kết nối Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Đã kết nối WiFi");

  // Cài đặt múi giờ và đồng bộ thời gian
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000);
}

// Lấy giờ và phút hiện tại từ NTP
void getCurrentTime(int& curHour, int& curMin) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    curHour = timeinfo.tm_hour;
    curMin = timeinfo.tm_min;
  } else {
    Serial.println("Không thể lấy thời gian từ NTP");
  }
}

// Điều khiển độ sáng đèn LED
void controlLED(int brightness) {
  analogWrite(LED_PIN, brightness);
}

void handleBrightness(int brightness) {
  scheduleSet = false;
  controlLED(brightness);
  lightIntensity = brightness;
  
  brightnessChanged = true; // Set flag to indicate a brightness change
  environmentChanged = false;
  statusChanged = false;
  scheduleChanged = false;
}

// Xử lý tín hiệu change_status và bật/tắt đèn
void handleStatus(String status) {
  scheduleSet = false;
  if (status == "on") {
    controlLED(255);  // Bật đèn
    lightIntensity = 255;
    sendDataToTransmitter();
  } else if (status == "off") {
    controlLED(0);  // Tắt đèn
    lightIntensity = 0;
    sendDataToTransmitter();
  }
  statusChanged = true; // Set flag to indicate a status change
  environmentChanged = false;
  scheduleChanged = false;
  brightnessChanged = false;
}

void handleSchedule(String schedule) {
  int separatorIndex = schedule.indexOf(';');
  String startTime = schedule.substring(0, separatorIndex);
  String endTime = schedule.substring(separatorIndex + 1);

  startHour = startTime.substring(0, 2).toInt();
  startMin = startTime.substring(3).toInt();
  endHour = endTime.substring(0, 2).toInt();
  endMin = endTime.substring(3).toInt();

  scheduleChanged = true; // Set flag to indicate a schedule change
  environmentChanged = false;
  statusChanged = false;
  brightnessChanged = false;
}

void checkSchedule() {
  if (!scheduleChanged) return;  // Nếu lịch trình chưa được cài đặt, bỏ qua kiểm tra

  int curHour, curMin;
  getCurrentTime(curHour, curMin);  // Lấy giờ và phút hiện tại

  Serial.println(String(curHour) + ":" + String(curMin));  // In ra giờ:phút hiện tại

  // Xác định nếu lịch trình đang hoạt động
  bool isScheduleActive = false;
  if (startHour < endHour || (startHour == endHour && startMin < endMin)) {
    // Lịch trình không qua đêm (VD: 08:00 -> 18:00)
    Serial.println("Jump in 1:----------------");
    isScheduleActive = (curHour > startHour || (curHour == startHour && curMin >= startMin)) && (curHour < endHour || (curHour == endHour && curMin < endMin));
  } else {
    // Lịch trình qua đêm (VD: 18:00 -> 06:00)
    Serial.println("Jump in 2:----------------");
    isScheduleActive = (curHour > startHour || (curHour == startHour && curMin >= startMin)) || (curHour < endHour || (curHour == endHour && curMin < endMin));
  }

  Serial.println(isScheduleActive);

  if (isScheduleActive) {
    controlLED(25);  // Đèn sáng 10%
    handleMotionSensor();
  } else {
    checkTransmitterEnvironmentData();
  }
}

// Kiểm tra và xử lý tín hiệu từ bộ phát
void checkTransmitterData() {
  if (Serial2.available()) {
    String receivedData = Serial2.readStringUntil('\n');
    Serial.println("Received Data: " + receivedData);
    receivedData.trim();

    if (receivedData.startsWith("change_status=")) {
      handleStatus(receivedData.substring(14));
    } else if (receivedData.startsWith("change_schedule=")) {
      handleSchedule(receivedData.substring(16));
    } else if (receivedData.startsWith("change_brightness=")) {
      int brightness = receivedData.substring(18).toInt();
      handleBrightness(brightness);
    } else if (receivedData.startsWith("change_environment=")) {
      sendDataToTransmitter();
    }
  }
}

// Gửi dữ liệu đến master
void sendDataToTransmitter() {
  brightnessPercentage = map(lightIntensity, 0, 255, 0, 100);

  DynamicJsonDocument doc(256);
  doc["lightIntensity"] = lightIntensity;
  doc["brightness"] = brightnessPercentage;
  doc["light_status"] = (lightIntensity > 0) ? "on" : "off";
  doc["motion_status"] = motion_status;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  Serial2.println(jsonBuffer);
  Serial.println("Send Data: " + String(jsonBuffer));
}

// Sửa đổi hàm `checkTransmitterEnvironmentData` để đặt `environmentChanged` khi có sự thay đổi
void checkTransmitterEnvironmentData() {
  if (Serial2.available()) {
    String receivedData = Serial2.readStringUntil('\n');
    Serial.println("Received Data Environment--------: " + receivedData);
    receivedData.trim();

    if (receivedData.startsWith("change_environment=")) {
      // Xử lý dữ liệu môi trường và đặt biến `environmentChanged` thành true
      if (receivedData.substring(19) == "0") {
        controlLED(0);
        lightIntensity = 0;
        motion_status = "No motion detected!";
        sendDataToTransmitter();
      } else {
        handleMotionSensor();
      }
    }
  }
}

// Xử lý cảm biến chuyển động
void handleMotionSensor() {
  if (digitalRead(MOTION_PIN) == 1) {
    controlLED(255);
    lightIntensity = 255;
    motion_status = "Motion detected!";
  } else {
    controlLED(25);
    lightIntensity = 25;
    motion_status = "No motion detected!";
  }
  sendDataToTransmitter();
}

int cnt = 0;
void loop() {
  Serial.println("Loop: -------" + String(cnt++));
  checkTransmitterData();  // Kiểm tra dữ liệu từ bộ phát
  delay(1000);
  checkSchedule();         // Kiểm tra thời gian với lịch trình
  // Gửi dữ liệu cập nhật khi có bất kỳ thay đổi nào
  // delay(1000);
  // checkTransmitterEnvironmentData();
  if (statusChanged || scheduleChanged || brightnessChanged || environmentChanged) {
    delay(1000);
    Serial.println("statusChanged:---------" + String(statusChanged));
    Serial.println("scheduleChanged:---------" + String(scheduleChanged));
    Serial.println("brightnessChanged:---------" + String(brightnessChanged));
    Serial.println("environmentChanged:---------" + String(environmentChanged));

    sendDataToTransmitter();  // Gửi trạng thái mới nhất đến bộ phát
  }
  
  delay(1000); // Delay để tránh kiểm tra quá thường xuyên

}
