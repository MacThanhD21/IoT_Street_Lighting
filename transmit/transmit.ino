#include "Arduino.h"
#include "Wire.h"
#include "BH1750.h"
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Pin Definitions
#define RXD2 16
#define TXD2 17
#define LED_PIN 13  // LED output pin

// WiFi & MQTT Connection Details
const char* ssid = "IEC_2";
const char* password = "123456789a@";
// MQTT Cloud
const char* mqtt_server = "a290b9bc05ee4afdbc18a2fd30f92d28.s1.eu.hivemq.cloud";
const char* mqtt_username = "thanh";
const char* mqtt_password = "123";
const int mqtt_port = 8883;

WiFiClientSecure espClient;
PubSubClient client(espClient);
BH1750 lightMeter;
String receivedDataBuffer = "";

// Kết nối tới WiFi
void setup_wifi() {
  delay(10);
  Serial.print("\nConnecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected\nIP address: ");
  Serial.println(WiFi.localIP());
}

// Kết nối tới MQTT broker
void setup_mqtt() {
  client.setServer(mqtt_server, mqtt_port);
}

// Hàm kiểm tra và kết nối lại MQTT nếu mất kết nối
void check_mqtt_connection() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");
      client.subscribe("esp32/led_1/change_status");
      client.subscribe("esp32/led_1/change_schedule");
      client.subscribe("esp32/led_1/change_brightness");

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Lắng nghe các topic MQTT và gửi dữ liệu tới slave
void callback(char* topic, byte* payload, unsigned int length) {
  String receivedData;
  for (int i = 0; i < length; i++) {
    receivedData += (char)payload[i];
  }
  Serial.println("----------------------:" + receivedData);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, receivedData);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.f_str());
    return;
  }

  if (String(topic) == "esp32/led_1/change_status") {
    String status = doc["status"].as<String>();
    Serial.println("Received change_status: " + status);
    Serial2.println("change_status=" + status);
  } else if (String(topic) == "esp32/led_1/change_schedule") {
    String startTime = doc["start_time"].as<String>();
    String endTime = doc["end_time"].as<String>();
    String scheduleData = startTime + ";" + endTime;
    Serial.println("Received change_schedule: " + scheduleData);
    Serial2.println("change_schedule=" + scheduleData);
  } else if (String(topic) == "esp32/led_1/change_brightness") {
    String brightness = doc["brightness"];
    Serial.println("Received change_brightness: " + String(brightness));
    Serial2.println("change_brightness=" + brightness);  // Chỉ gửi giá trị độ sáng
  }
}

// Đọc dữ liệu ánh sáng từ cảm biến BH1750
float readLightLevel() {
  return lightMeter.readLightLevel();
}

// Công bố thông điệp MQTT
void publishMessage(const char* topic, const String& payload) {
  if (client.publish(topic, payload.c_str(), true)) {
    Serial.println("Message published [" + String(topic) + "]: " + payload);
  }
}

// Tạo thông điệp JSON và gửi qua MQTT
void sendLightData(float lux) {
  DynamicJsonDocument doc(1024);
  doc["light"] = lux;
  char mqtt_message[128];
  serializeJson(doc, mqtt_message);
  publishMessage("esp32/Light_Data_BH1750", mqtt_message);
}


// Điều khiển đèn LED theo cường độ ánh sáng
void controlLED(float lux) {
  if (lux > 200) {
    digitalWrite(LED_PIN, LOW);
    Serial2.println("change_environment=0");  // Gửi tín hiệu ánh sáng môi trường cao
  } else {
    digitalWrite(LED_PIN, HIGH);
    Serial2.println("change_environment=1");  // Gửi tín hiệu ánh sáng môi trường thấp
  }
}


// Function to send all LED-related data in one MQTT message
void sendLedData(String status, int brightness, int lightIntensity, String motionStatus) {
  DynamicJsonDocument doc(1024);
  doc["status"] = status;
  doc["brightness"] = brightness;
  doc["lightIntensity"] = lightIntensity;
  doc["motionStatus"] = motionStatus;

  char mqtt_message[256];
  serializeJson(doc, mqtt_message);
  publishMessage("esp32/led_1", mqtt_message);  // Publish all data in one topic
}
// Receiver data handling
void receiveDataFromReceiver() {
  if (Serial2.available()) {
    String receivedData = Serial2.readString();
    Serial.println("Received Data: " + receivedData);

    // Phân tích chuỗi JSON nhận được
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, receivedData);

    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.f_str());
      return;
    }

    // Lấy dữ liệu từ JSON
    int lightIntensity = doc["lightIntensity"];
    int brightness = doc["brightness"];
    String lightStatus = doc["light_status"];
    String motionStatus = doc["motion_status"];

    // Gửi tất cả dữ liệu qua MQTT trong một thông điệp duy nhất
    sendLedData(lightStatus, brightness, lightIntensity, motionStatus);
  }
}


void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(500);

  Wire.begin();
  lightMeter.begin();
  pinMode(LED_PIN, OUTPUT);

  setup_wifi();
  espClient.setInsecure();
  setup_mqtt();
  client.setCallback(callback);
}
int cnt = 0;
void loop() {
  Serial.println("Loop---------------" + String(cnt++));
  check_mqtt_connection();
  client.loop();

  float lux = readLightLevel();
  sendLightData(lux);
  controlLED(lux);
  receiveDataFromReceiver();

  delay(2500);  // Delay to avoid rapid data transmission
}

