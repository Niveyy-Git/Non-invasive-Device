#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ssid = "project";
const char* password = "12345678";

WebServer server(80);
MAX30105 particleSensor;

#define BUFFER_SIZE 100

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2 = 0;
int32_t heartRate = 0;
float hemoglobin = 0.0;
String anemiaStatus = "Initializing";

int8_t validSPO2;
int8_t validHR;

unsigned long lastReadTime = 0;

// ===================== SETUP =====================
void setup() {

  Serial.begin(115200);
  Wire.begin(21, 22);

  // OLED INIT
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
    while (1);
  }

  showMessage("HB Monitoring", "Starting...");

  // WiFi
  WiFi.begin(ssid, password);
  showMessage("Connecting WiFi", "");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  showMessage("WiFi Connected", WiFi.localIP().toString());
  delay(2000);

  // MAX30102 INIT
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    showMessage("MAX30102", "Not Found!");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x24);
  particleSensor.setPulseAmplitudeIR(0x24);

  delay(2000);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/status", handleStatus);

  server.begin();
  Serial.println("HTTP Server Started");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();

  if (millis() - lastReadTime > 2000) {   // read every 2 seconds
    readSensor();
    lastReadTime = millis();
  }
}

// ================= SENSOR READING =================
void readSensor() {

  for (byte i = 0; i < BUFFER_SIZE; i++) {
    while (!particleSensor.available())
      particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i]  = particleSensor.getIR();
    particleSensor.nextSample();
  }

  if (irBuffer[0] < 5000) {   // Better finger detection
    anemiaStatus = "Place Finger";
    heartRate = 0;
    spo2 = 0;
    hemoglobin = 0;
    updateOLED();
    return;
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE,
    redBuffer,
    &spo2, &validSPO2,
    &heartRate, &validHR
  );

  if (validHR && validSPO2 &&
      heartRate > 50 && heartRate < 120 &&
      spo2 > 90 && spo2 <= 100) {

    hemoglobin = (0.15 * spo2) + 1.5;

if (hemoglobin >= 12.0) {
  anemiaStatus = "Normal";
}
else if (hemoglobin >= 9.0) {
  anemiaStatus = "Medium";
}
else {
  anemiaStatus = "Severe";
}
  }

  updateOLED();
}

// ================= OLED DISPLAY =================
void updateOLED() {

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  display.print("HR  : ");
  display.println(heartRate);

  display.print("SpO2: ");
  display.println(spo2);

  display.print("HB  : ");
  display.println(hemoglobin, 1);

  display.println("----------------");

  String rangeText = "";

  if (hemoglobin >= 12.0) {
    anemiaStatus = "Normal";
    rangeText = "HB>=12";
  }
  else if (hemoglobin >= 9.0) {
    anemiaStatus = "Medium";
    rangeText = "HB>=9.0";
  }
  else {
    anemiaStatus = "Severe";
    rangeText = "HB<9.0";
  }

  display.print("Status : ");
  display.println(anemiaStatus);

  display.print("Anemia range:");
  display.println(rangeText);

  display.display();
}
// ================= OLED MESSAGE =================
void showMessage(String line1, String line2) {

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  display.println(line2);
  display.display();
}

// ================= WEB PAGE =================
void handleRoot() {

  String html = "<html><body>";
  html += "<h2>Hemoglobin Monitoring</h2>";
  html += "<p><b>Heart Rate:</b> " + String(heartRate) + "</p>";
  html += "<p><b>SpO2:</b> " + String(spo2) + "</p>";
  html += "<p><b>Hemoglobin:</b> " + String(hemoglobin,1) + "</p>";
  html += "<p><b>Status:</b> " + anemiaStatus + "</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ================= JSON API =================
void handleData() {

  String json = "{";
  json += "\"heart_rate\":" + String(heartRate) + ",";
  json += "\"spo2\":" + String(spo2) + ",";
  json += "\"hemoglobin\":" + String(hemoglobin,1) + ",";
  json += "\"status\":\"" + anemiaStatus + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// ================= STATUS ONLY =================
void handleStatus() {
  server.send(200, "text/plain", anemiaStatus);
}