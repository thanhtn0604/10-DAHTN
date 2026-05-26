#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "AudioFileSourceSD.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ========== QUEUES ==========
typedef struct {
  char uid[16];
} RFIDData;

typedef struct {
  char path[128];
  char data[512];
} FirebaseData;

QueueHandle_t uidQueue;
QueueHandle_t firebaseQueue;

// ========== CẤU HÌNH WiFi AP ==========
const char* ap_ssid = "ESP-SETUP";
const char* ap_password = "12345678";

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

String saved_ssid = "";
String saved_password = "";
bool wifi_configured = false;
bool apMode = false;

// ========== CẤU HÌNH FIREBASE ==========
#define FIREBASE_URL "https://web23-6a076-default-rtdb.asia-southeast1.firebasedatabase.app"

// Chân
#define SS_PIN 15
#define RST_PIN 21
MFRC522 rfid(SS_PIN, RST_PIN);

#define TFT_CS 5
#define TFT_DC 32
#define TFT_RST 4
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define SD_CS 13

// UID các thẻ
#define UID_MODE_LEARNING "321ACF05"
#define UID_MODE_TEST "8307CF05"
#define UID_MODE_MUSIC "BE7EC705"
#define UID_EXIT "BA58C805"
#define UID_RESET_WIFI "579EC805"

const char* letterUIDs[] = {
  "91D2C905", "E1DAC805", "75A0C805", "C6CFC905", "F671C905",
  "2A2ACF05", "86F8C705", "FE7DC805", "59AEC805", "06ACC805"
};
const char* letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};

// ========== BIẾN MODE ==========
int currentMode = 0;
bool isLearning = false;
unsigned long sessionStart = 0;
String currentSessionId = "";

bool isTesting = false;
struct Question {
  String letter;
  String uid;
};
Question questions[] = {
  {"A", "91D2C905"}, {"B", "E1DAC805"}, {"C", "75A0C805"}, 
  {"D", "C6CFC905"}, {"E", "F671C905"}, {"F", "2A2ACF05"},
  {"G", "86F8C705"}, {"H", "FE7DC805"}, {"I", "59AEC805"}, {"J", "06ACC805"}
};
const int totalQuestions = sizeof(questions) / sizeof(questions[0]);
int currentQuestionIndex = 0;
unsigned long questionStartTime = 0;
const unsigned long QUESTION_TIMEOUT = 10000;
bool waitingForAnswer = false;
bool answerProcessed = false;
int score = 0;
int totalAnswered = 0;

bool isMusic = false;

// ========== CACHE CHO TFT ==========
unsigned long lastScreenUpdate = 0;
String lastDisplayLetter = "";
int lastScore = -1;
int lastTotal = -1;

// ========== AUDIO ==========
AudioGeneratorWAV *wav = nullptr;
AudioFileSourceSD *fileSD = nullptr;
AudioFileSourceBuffer *fileBuff = nullptr;
AudioOutputI2S *out = nullptr;
bool isPlaying = false;

SPIClass spiSD(VSPI);

// ========== EEPROM ==========
void clearWiFiFromEEPROM() {
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();

  saved_ssid = "";
  saved_password = "";
  wifi_configured = false;

  // Xóa cấu hình WiFi trong flash ESP32
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(500);

  Serial.println("WiFi EEPROM + NVS CLEARED");
}

void saveWiFiToEEPROM(String ssid, String password) {
  EEPROM.begin(512);
  for (int i = 0; i < 200; i++) EEPROM.write(i, 0);
  for (int i = 0; i < ssid.length(); i++) EEPROM.write(i, ssid[i]);
  EEPROM.write(ssid.length(), '\0');
  for (int i = 0; i < password.length(); i++) EEPROM.write(100 + i, password[i]);
  EEPROM.write(100 + password.length(), '\0');
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Da luu WiFi: " + ssid);
}

void readWiFiFromEEPROM() {
  EEPROM.begin(512);
  saved_ssid = "";
  saved_password = "";
  for (int i = 0; i < 100; i++) {
    char c = EEPROM.read(i);
    if (c == '\0') break;
    if (c < 32 || c > 126) break;
    saved_ssid += c;
  }
  for (int i = 100; i < 200; i++) {
    char c = EEPROM.read(i);
    if (c == '\0') break;
    if (c < 32 || c > 126) break;
    saved_password += c;
  }
  EEPROM.end();
  if (saved_ssid.length() > 0 && saved_ssid.length() < 32) {
    wifi_configured = true;
    Serial.println("Da doc WiFi: " + saved_ssid);
  } else {
    wifi_configured = false;
    Serial.println("Chua co WiFi trong EEPROM");
  }
}

// ========== WEB SERVER ==========
void handleRoot() {
  String html = "<!DOCTYPE html><html>";
  html += "<head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body{font-family:Arial;text-align:center;background:#0f172a;color:white;padding:20px;}";
  html += "input,select{width:90%;padding:12px;margin:10px 0;border-radius:8px;border:none;}";
  html += "button{background:#3b82f6;color:white;padding:12px 30px;border:none;border-radius:8px;font-size:16px;cursor:pointer;}";
  html += ".card{background:#1e293b;padding:20px;border-radius:15px;max-width:400px;margin:auto;}";
  html += ".wifi-list{background:#0f172a;padding:10px;border-radius:8px;margin:10px 0;text-align:left;max-height:200px;overflow-y:auto;}";
  html += ".wifi-item{padding:8px;border-bottom:1px solid #334155;cursor:pointer;}";
  html += ".wifi-item:hover{background:#334155;}";
  html += "</style>";
  html += "<script>";
  html += "function selectSSID(ssid){document.getElementById('ssid').value=ssid;}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='card'>";
  html += "<h1> SMART LEARNING</h1>";
  html += "<h3>Chon WiFi nha</h3>";
  
  html += "<div class='wifi-list'>";
  html += "<strong> Cac mang WiFi tim thay:</strong>";
  
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
    html += "<div>Dang quet...</div>";
  } else if (n > 0) {
    for (int i = 0; i < n; i++) {
      html += "<div class='wifi-item' onclick=\"selectSSID('" + WiFi.SSID(i) + "')\">";
      html += " " + WiFi.SSID(i);
      if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) html += " (Khong mat khau)";
      html += "</div>";
    }
  } else {
    html += "<div>Khong tim thay WiFi nao</div>";
  }
  html += "</div>";
  
  html += "<form action='/connect' method='POST'>";
  html += "<input type='text' name='ssid' id='ssid' placeholder='Hoac nhap ten WiFi' required><br>";
  html += "<input type='password' name='pass' placeholder='Mat khau (neu co)'><br>";
  html += "<button type='submit'>Ket noi</button>";
  html += "</form>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  
  saveWiFiToEEPROM(ssid, pass);
  
  server.send(200, "text/html", "<h1>Saved. Restarting...</h1>");
  
  delay(1000);
  ESP.restart();
}

void startAPMode() {
  // Xóa sạch trạng thái WiFi cũ
  WiFi.disconnect(true, true);
  delay(100);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress apIP = WiFi.softAPIP();
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  
  // Sửa lỗi lambda capture
  server.onNotFound([apIP]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  
  server.begin();
  
  apMode = true;
  
  Serial.println("AP MODE START");
  Serial.println(apIP);
  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(20, 50);
  tft.print("WIFI SETUP");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 100);
  tft.print("Connect to:");
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(20, 130);
  tft.print(ap_ssid);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 180);
  tft.print("Password: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(ap_password);
  tft.setCursor(20, 220);
  tft.print("Then open browser:");
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(20, 250);
  tft.print(apIP.toString());
}

bool connectToWiFi() {
  if (!wifi_configured || saved_ssid.length() == 0) {
    Serial.println("Chua co WiFi, vao AP mode");
    return false;
  }
  
  apMode = false;
  
  Serial.print("Ket noi WiFi: ");
  Serial.println(saved_ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.print("Connecting to:");
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, 140);
    tft.print(saved_ssid);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n WiFi OK");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.print("WiFi Connected!");
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 150);
    tft.print("IP: ");
    tft.print(WiFi.localIP());
    delay(2000);
    return true;
  }
  
  Serial.println("\n WiFi that bai");
  return false;
}

// ========== FIREBASE ==========
void initNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Dong bo thoi gian");
  int attempts = 0;
  time_t now = time(nullptr);
  while (now < 24 * 3600 && attempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  Serial.println(now >= 24 * 3600 ? " OK" : "Dung timestamp tam");
}

unsigned long getTimestamp() {
  time_t now = time(nullptr);
  if (now < 1000000000) return 1712000000 + (millis() / 1000);
  return now;
}

void sendToFirebase(String path, String data) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  FirebaseData fb;
  strcpy(fb.path, path.c_str());
  strcpy(fb.data, data.c_str());
  xQueueSend(firebaseQueue, &fb, 0);
}

void saveNumber(String path, long value) {
  sendToFirebase(path, String(value));
}

void saveString(String path, String value) {
  sendToFirebase(path, "\"" + value + "\"");
}

void startSession(String mode) {
  if ((mode == "LEARNING" && isLearning) ||
      (mode == "TEST" && isTesting) ||
      (mode == "MUSIC" && isMusic)) return;
  
  if (mode == "LEARNING") isLearning = true;
  if (mode == "TEST") isTesting = true;
  if (mode == "MUSIC") isMusic = true;
  
  sessionStart = getTimestamp();
  currentSessionId = mode + "_" + String(sessionStart);
  
  saveString("/sessions/" + currentSessionId + "/mode", mode);
  saveNumber("/sessions/" + currentSessionId + "/start", sessionStart);
  saveString("/sessions/" + currentSessionId + "/status", "active");
}

void endSession() {
  if (!isLearning && !isTesting && !isMusic) return;
  
  unsigned long duration = getTimestamp() - sessionStart;
  
  saveNumber("/sessions/" + currentSessionId + "/end", getTimestamp());
  saveNumber("/sessions/" + currentSessionId + "/duration", duration);
  saveString("/sessions/" + currentSessionId + "/status", "completed");
  
  if (isTesting && totalAnswered > 0) {
    String json = "{\"correct\":" + String(score) + 
                  ",\"total\":" + String(totalAnswered) +
                  ",\"percent\":" + String((score * 100) / totalAnswered) + "}";
    sendToFirebase("/sessions/" + currentSessionId + "/test_result", json);
  }
  
  isLearning = false;
  isTesting = false;
  isMusic = false;
}

void logCardScan(String uid, String letter) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  String timestamp = String(getTimestamp());
  String json = "{\"uid\":\"" + uid + "\",\"letter\":\"" + letter + 
                "\",\"time\":" + timestamp;
  if (isLearning) json += ",\"session\":\"" + currentSessionId + "\"";
  json += "}";
  
  sendToFirebase("/logs/learning/" + timestamp, json);
}

void logTestAnswer(String uid, String expectedLetter, bool isCorrect, int currentScore, int currentTotal) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  String timestamp = String(getTimestamp());
  String json = "{\"uid\":\"" + uid + "\",\"expected_letter\":\"" + expectedLetter + 
                "\",\"is_correct\":" + String(isCorrect ? "true" : "false") +
                ",\"score\":" + String(currentScore) +
                ",\"total_answered\":" + String(currentTotal) +
                ",\"time\":" + timestamp;
  if (isTesting) json += ",\"session\":\"" + currentSessionId + "\"";
  json += "}";
  
  sendToFirebase("/logs/test/" + timestamp, json);
}

// ========== AUDIO ==========
void initAudio() {
  if(out == nullptr) {
    out = new AudioOutputI2S();
    out->SetPinout(26, 25, 22);
    out->SetGain(1);
    out->SetOutputModeMono(true);
  }
  if(wav == nullptr) wav = new AudioGeneratorWAV();
}

void playWAV(const char* filename) {
  if(!SD.exists(filename)) return;
  
  if(wav && wav->isRunning()) wav->stop();
  
  if(fileBuff) { delete fileBuff; fileBuff = nullptr; }
  if(fileSD) { delete fileSD; fileSD = nullptr; }
  
  fileSD = new AudioFileSourceSD(filename);
  fileBuff = new AudioFileSourceBuffer(fileSD, 16384);
  
  if(wav->begin(fileBuff, out)) isPlaying = true;
}

void stopAudio() {
  if(wav && wav->isRunning()) wav->stop();
  isPlaying = false;
}

// ========== HIỂN THỊ ==========
void showMessage(String msg, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(color);
  tft.setTextSize(3);
  int x = 120 - (msg.length() * 18) / 2;
  tft.setCursor(x, 160);
  tft.print(msg);
}

void showLargeLetter(String letter) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(10);
  int x = 120 - (letter.length() * 30) / 2;
  tft.setCursor(x, 120);
  tft.print(letter);
}

void drawSmileFace() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillCircle(120, 160, 80, ST77XX_YELLOW);
  tft.fillCircle(80, 120, 12, ST77XX_BLACK);
  tft.fillCircle(160, 120, 12, ST77XX_BLACK);
  tft.fillCircle(80, 120, 5, ST77XX_WHITE);
  tft.fillCircle(160, 120, 5, ST77XX_WHITE);
  for(int i = 0; i <= 60; i++) {
    int x = 120 + 50 * cos(i * 3.14 / 180);
    int y = 200 + 30 * sin(i * 3.14 / 180);
    tft.drawPixel(x, y, ST77XX_BLACK);
  }
}

void drawCryFace() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillCircle(120, 160, 80, ST77XX_YELLOW);
  tft.fillCircle(80, 120, 12, ST77XX_BLACK);
  tft.fillCircle(160, 120, 12, ST77XX_BLACK);
  tft.fillCircle(80, 120, 5, ST77XX_WHITE);
  tft.fillCircle(160, 120, 5, ST77XX_WHITE);
  for(int i = 180; i <= 360; i++) {
    int x = 120 + 50 * cos(i * 3.14 / 180);
    int y = 210 + 30 * sin(i * 3.14 / 180);
    tft.drawPixel(x, y, ST77XX_BLACK);
  }
  tft.fillCircle(70, 140, 5, ST77XX_BLUE);
  tft.fillCircle(170, 140, 5, ST77XX_BLUE);
}

void showModeMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 30);
  tft.print("SMART LEARNING");
  tft.setTextSize(1);
  tft.setCursor(20, 90);
  tft.print("Scan card to select mode:");
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(30, 130);
  tft.print("1. LEARNING");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(30, 170);
  tft.print("2. TEST MODE");
  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(30, 210);
  tft.print("3. MUSIC MODE");
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(1);
  tft.setCursor(20, 270);
  tft.print("Scan RESET to clear WiFi");
}

void updateScoreDisplay() {
  if(lastScore != score || lastTotal != totalAnswered) {
    lastScore = score;
    lastTotal = totalAnswered;
    tft.fillRect(150, 0, 90, 40, ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(150, 20);
    tft.print("Score: ");
    tft.print(score);
    tft.print("/");
    tft.print(totalAnswered);
  }
}

void showQuestion(String letter, int remainingTime) {
  if(lastDisplayLetter != letter) {
    tft.fillScreen(ST77XX_BLACK);
    lastDisplayLetter = letter;
    
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(8);
    int x = 120 - (letter.length() * 24);
    tft.setCursor(x, 100);
    tft.print(letter);
    
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 280);
    tft.print("Find card: ");
    tft.print(letter);
    tft.setCursor(20, 295);
    tft.print("Scan EXIT card to quit");
  }
  
  tft.fillRect(0, 0, 120, 40, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print("Time: ");
  tft.print(remainingTime);
  tft.print("s");
  
  updateScoreDisplay();
}

void showResult(bool correct) {
  if(correct) {
    drawSmileFace();
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(3);
    tft.setCursor(40, 280);
    tft.print("CORRECT!");
    playWAV("/cor.wav");
    score++;
  } else {
    drawCryFace();
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(3);
    tft.setCursor(60, 280);
    tft.print("WRONG!");
    playWAV("/wr.wav");
  }
  totalAnswered++;
  updateScoreDisplay();
}

void showTestSummary() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 60);
  tft.print("TEST COMPLETE!");
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(40, 130);
  tft.print(score);
  tft.print("/");
  tft.print(totalAnswered);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(40, 200);
  int percent = (score * 100) / (totalAnswered > 0 ? totalAnswered : 1);
  tft.print("Score: ");
  tft.print(percent);
  tft.print("%");
  if(percent >= 70) drawSmileFace();
  else drawCryFace();
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(30, 270);
  tft.print("Scan EXIT to menu");
}

void showMusicMode() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_BLUE);
  tft.setTextSize(3);
  tft.setCursor(20, 100);
  tft.print("MUSIC MODE");
  tft.setCursor(20, 150);
  tft.print("Playing...");
  tft.setCursor(20, 200);
  tft.print("diamon.wav");
  tft.setTextSize(1);
  tft.setCursor(20, 280);
  tft.print("Scan EXIT card to stop");
}

bool initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(200);
  spiSD.begin(18, 19, 23, SD_CS);
  delay(200);
  return SD.begin(SD_CS, spiSD, 4000000);
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ========== XỬ LÝ MODE ==========
void processLearningMode(String uid) {
  if(uid == UID_EXIT) {
    endSession();
    currentMode = 0;
    showModeMenu();
    stopAudio();
    return;
  }
  
  String letter = "?";
  for (int i = 0; i < 10; i++) {
    if (uid == letterUIDs[i]) {
      letter = letters[i];
      break;
    }
  }
  
  if(letter != "?") {
    logCardScan(uid, letter);
    showLargeLetter(letter);
    String audioFile = "/" + letter + ".wav";
    if (SD.exists(audioFile.c_str())) playWAV(audioFile.c_str());
  }
}

void processTestMode() {
  if(!waitingForAnswer && currentMode == 2) {
    currentQuestionIndex = random(totalQuestions);
    questionStartTime = millis();
    waitingForAnswer = true;
    answerProcessed = false;
    lastDisplayLetter = "";
    showQuestion(questions[currentQuestionIndex].letter, 10);
    return;
  }
  
  if(waitingForAnswer && !answerProcessed) {
    unsigned long elapsed = millis() - questionStartTime;
    int remainingTime = (QUESTION_TIMEOUT - elapsed) / 1000;
    
    if(elapsed >= QUESTION_TIMEOUT) {
      waitingForAnswer = false;
      answerProcessed = true;
      totalAnswered++;
      logTestAnswer("TIMEOUT", questions[currentQuestionIndex].letter, false, score, totalAnswered);
      showMessage("TIME OUT!", ST77XX_YELLOW);
    }
    else if(millis() - lastScreenUpdate > 200) {
      lastScreenUpdate = millis();
      showQuestion(questions[currentQuestionIndex].letter, remainingTime);
    }
  }
}

void processMusicMode() {
  showMusicMode();
  playWAV("/diamon.wav");
}

void processModeSelection(String uid) {
  if (uid == UID_RESET_WIFI) {
    clearWiFiFromEEPROM();
    showMessage("RESET WIFI", ST77XX_RED);
    delay(1000);
    ESP.restart();
    return;
  }
  
  if (uid == UID_MODE_LEARNING) {
    currentMode = 1;
    startSession("LEARNING");
    showMessage("LEARNING MODE", ST77XX_GREEN);
    delay(1000);
    showLargeLetter("Scan card");
  }
  else if (uid == UID_MODE_TEST) {
    currentMode = 2;
    score = 0;
    totalAnswered = 0;
    waitingForAnswer = false;
    answerProcessed = false;
    lastDisplayLetter = "";
    lastScore = -1;
    randomSeed(millis());
    startSession("TEST");
    showMessage("TEST MODE", ST77XX_YELLOW);
    delay(1000);
  }
  else if (uid == UID_MODE_MUSIC) {
    currentMode = 3;
    startSession("MUSIC");
    processMusicMode();
  }
  else if (uid == UID_EXIT && currentMode != 0) {
    if(currentMode == 2) {
      waitingForAnswer = false;
      answerProcessed = false;
    }
    endSession();
    currentMode = 0;
    if(totalAnswered > 0) {
      showTestSummary();
      delay(3000);
    }
    showModeMenu();
    stopAudio();
  }
}

// ========== FREERTOS TASKS ==========

void audioTask(void *pv) {
  initAudio();
  while(1) {
    if(isPlaying && wav && wav->isRunning()) {
      if(!wav->loop()) isPlaying = false;
    }
    vTaskDelay(1);
  }
}

void rfidTask(void *pv) {
  rfid.PCD_Init();
  rfid.PCD_AntennaOn();
  
  while(1) {
    if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uidStr = getUID();
      RFIDData data;
      strcpy(data.uid, uidStr.c_str());
      xQueueSend(uidQueue, &data, 0);
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
    vTaskDelay(1);
  }
}

void webTask(void *pv) {
  while(1) {
    if(apMode) {
      server.handleClient();
      dnsServer.processNextRequest();
    }
    vTaskDelay(10);
  }
}

void firebaseTask(void *pv) {
  FirebaseData fb;
  
  while(1) {
    if(xQueueReceive(firebaseQueue, &fb, portMAX_DELAY) == pdTRUE) {
      if(WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(FIREBASE_URL) + String(fb.path) + ".json";
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.PUT(fb.data);
        http.end();
      }
    }
  }
}

void uiTask(void *pv) {
  RFIDData data;
  
  while(1) {
    if(xQueueReceive(uidQueue, &data, 0) == pdTRUE) {
      String uid = String(data.uid);
      
      if (currentMode == 0) {
        processModeSelection(uid);
      } 
      else if (currentMode == 1) {
        processLearningMode(uid);
      }
      else if (currentMode == 2) {
        if (uid == UID_EXIT) {
          processModeSelection(uid);
        }
        else if(waitingForAnswer && !answerProcessed) {
          answerProcessed = true;
          waitingForAnswer = false;
          
          bool isCorrect = (uid == questions[currentQuestionIndex].uid);
          showResult(isCorrect);
          logTestAnswer(uid, questions[currentQuestionIndex].letter, isCorrect, score, totalAnswered);
          delay(2000);
        }
      }
      else if (currentMode == 3) {
        if (uid == UID_EXIT) processModeSelection(uid);
      }
    }
    
    if (currentMode == 2) processTestMode();
    
    vTaskDelay(1);
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  
  // Quan trọng: không cho ESP32 tự lưu WiFi vào NVS
  WiFi.persistent(false);
  
  Serial.println("\n=== SMART LEARNING SYSTEM (FINAL) ===");
  
  uidQueue = xQueueCreate(10, sizeof(RFIDData));
  firebaseQueue = xQueueCreate(20, sizeof(FirebaseData));
  
  SPI.begin(18, 19, 23);
  
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("BOOTING...");
  
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  initSD();
  
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  rfid.PCD_Init();
  rfid.PCD_AntennaOn();
  
  readWiFiFromEEPROM();
  
  if (wifi_configured && saved_ssid.length() > 0) {
    if (connectToWiFi()) {
      initNTP();
    } else {
      startAPMode();
    }
  } else {
    startAPMode();
  }
  
  showModeMenu();
  
  xTaskCreatePinnedToCore(rfidTask, "RFID", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(audioTask, "Audio", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(firebaseTask, "Firebase", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(webTask, "Web", 4096, NULL, 1, NULL, 1);
  
  vTaskDelete(NULL);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}