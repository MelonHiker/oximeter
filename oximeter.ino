#include <SPI.h> 
#include <Wire.h> 
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h> 
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "ESP32Servo.h"


// ThingSpeak
char ssid[] = "pc02";
char password[] = "12345678";
String url = "http://api.thingspeak.com/update?api_key=8U5URTO2BGN3TZJM";
const int UPLOAD_PERIOD_MS = 3000; // 資料上傳間隔
int last_time = 0; // 紀錄最後一次上傳資料的時間

// 設定OLED
const int SCREEN_WIDTH = 128; // OLED 寬度像素
const int SCREEN_HEIGHT = 64; // OLED 高度像素
const int OLED_RESET = -1;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

MAX30105 particleSensor;
uint32_t tsLastReport = 0;

//計算心跳用變數
const byte RATE_SIZE = 10; // 多少平均數量
byte rates[RATE_SIZE]; // 心跳陣列
byte rateSpot = 0;
long lastBeat = 0; // 紀錄最後一次增測到心跳的時間
float beatsPerMinute = 0;
int beatAvg = 0;
bool first = true;

//計算血氧用變數
double avered = 0;
double aveir = 0;
double sumirrms = 0;
double sumredrms = 0;

double SpO2 = 0;
double ESpO2 = 90.0; // 初始值
double FSpO2 = 0.7; //filter factor for estimated SpO2
double frate = 0.95; //low pass filter for IR/red LED value to eliminate AC component
int cnt = 0;
int NUM = 30; // 取樣30次才計算1次
const int FINGER_ON = 7000; // 紅外線最小量（判斷手指有沒有上）
const int MINIMUM_SPO2 = 90.0;// 血氧最小量
const int MAXIMUM_SPO2 = 99.9; // 血氧最大值

//心跳小圖
static const unsigned char PROGMEM logo2_bmp[] =
{ 0x03, 0xC0, 0xF0, 0x06, 0x71, 0x8C, 0x0C, 0x1B, 0x06, 0x18, 0x0E, 0x02, 0x10, 0x0C, 0x03, 0x10,
  0x04, 0x01, 0x10, 0x04, 0x01, 0x10, 0x40, 0x01, 0x10, 0x40, 0x01, 0x10, 0xC0, 0x03, 0x08, 0x88,
  0x02, 0x08, 0xB8, 0x04, 0xFF, 0x37, 0x08, 0x01, 0x30, 0x18, 0x01, 0x90, 0x30, 0x00, 0xC0, 0x60,
  0x00, 0x60, 0xC0, 0x00, 0x31, 0x80, 0x00, 0x1B, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x04, 0x00,
};

//氧氣圖示
static const unsigned char PROGMEM O2_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x3f, 0xc3, 0xf8, 0x00, 0xff, 0xf3, 0xfc,
  0x03, 0xff, 0xff, 0xfe, 0x07, 0xff, 0xff, 0xfe, 0x0f, 0xff, 0xff, 0xfe, 0x0f, 0xff, 0xff, 0x7e,
  0x1f, 0x80, 0xff, 0xfc, 0x1f, 0x00, 0x7f, 0xb8, 0x3e, 0x3e, 0x3f, 0xb0, 0x3e, 0x3f, 0x3f, 0xc0,
  0x3e, 0x3f, 0x1f, 0xc0, 0x3e, 0x3f, 0x1f, 0xc0, 0x3e, 0x3f, 0x1f, 0xc0, 0x3e, 0x3e, 0x2f, 0xc0,
  0x3e, 0x3f, 0x0f, 0x80, 0x1f, 0x1c, 0x2f, 0x80, 0x1f, 0x80, 0xcf, 0x80, 0x1f, 0xe3, 0x9f, 0x00,
  0x0f, 0xff, 0x3f, 0x00, 0x07, 0xfe, 0xfe, 0x00, 0x0b, 0xfe, 0x0c, 0x00, 0x1d, 0xff, 0xf8, 0x00,
  0x1e, 0xff, 0xe0, 0x00, 0x1f, 0xff, 0x00, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x1f, 0xe0, 0x00, 0x00,
  0x0f, 0xe0, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


void initText() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(30,0);
    display.print("Please");
    display.setCursor(45,16);
    display.print("Put"); 
    display.setCursor(40,32);
    display.print("Your");
    display.setCursor(30,48);
    display.print("Finger");  
    display.display();
    delay(1000);
}


void upload() {
    Serial.println("啟動網頁連線");
    HTTPClient http;
    
    // 將心跳及血氧以http get參數方式補入網址後方
    String newUrl = url + "&field1=" + (int)beatAvg + "&field2=" + (int)ESpO2;
    
    // http client取得網頁內容
    http.begin(newUrl);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)      {
        // 讀取網頁內容到payload
        String payload = http.getString();
        Serial.print("網頁內容=");
        Serial.println(payload);
    } else {
        // 讀取失敗
        Serial.println("網路傳送失敗");
    }
    http.end();
}


void showRate(){
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
      
    // 顯示心跳與血氧
    display.setCursor(0, 0);
    display.drawBitmap(5, 5, logo2_bmp, 24, 21, WHITE);
    display.setCursor(42, 10);
    display.print(beatAvg);
    display.println(" BPM");

    display.setCursor(42, 40);
    display.drawBitmap(0, 35, O2_bmp, 32, 32, WHITE);
    display.print(ESpO2);
    display.println("%");
    
    display.display();

    if (millis() - last_time >= UPLOAD_PERIOD_MS) {
        upload();
        last_time = millis();
    }
}


void setup() {
    Serial.begin(9600);
    Serial.print("開始連線到無線網路SSID:");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);
    }
    Serial.println("連線完成");
    
    // 偵測是否安裝好OLED了
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true) {}; // Don't proceed, loop forever
    }
  
    display.clearDisplay();
    initText();
  
    // 初始化 MAX30100, 若失敗就用無窮迴圈卡住
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println(F("MAX30100 allocation failed"));
        while (true) {}
    }
  
    byte ledBrightness = 0x7F; // 亮度Options: 0=Off to 255=50mA
    byte sampleAverage = 4; // Options: 1, 2, 4, 8, 16, 32
    byte ledMode = 2; // Options: 1 = Red only(心跳), 2 = Red + IR(血氧)
    // Options: 1 = IR only, 2 = Red + IR on MH-ET LIVE MAX30102 board
    int sampleRate = 800; // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
    int pulseWidth = 215; // Options: 69, 118, 215, 411
    int adcRange = 16384; // Options: 2048, 4096, 8192, 16384

    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    particleSensor.enableDIETEMPRDY();
  
    display.clearDisplay();
}


void loop() {
    long irValue = particleSensor.getIR();
    // 是否有放手指
    if (irValue > FINGER_ON) {
        // 是否有心跳
        if (checkForBeat(irValue) == true) {
            int delta = millis() - lastBeat; // 計算心跳差
            lastBeat = millis();
            beatsPerMinute = 60 / (delta / 1000.0); // 計算平均心跳
        }
        
        if (beatsPerMinute < 255 && beatsPerMinute > 20) {
            // 心跳必須再20-255之間
            rates[rateSpot++] = (byte)beatsPerMinute; // 儲存心跳數值陣列
            if (rateSpot >= RATE_SIZE) {
              rateSpot %= RATE_SIZE;
              first = false;
            }
            
            

            if (!first) {
                beatAvg = 0; // 計算平均值
                for (byte x = 0 ; x < RATE_SIZE ; x++) {
                    beatAvg += rates[x];
                }
                beatAvg /= RATE_SIZE;
                showRate();
            }
        }
        
        // 計算血氧
        particleSensor.check();
        
        if (!particleSensor.available()) {
          return;
        }
        
        cnt++;
        uint32_t red = particleSensor.getFIFOIR(); // 讀取紅光
        uint32_t ir = particleSensor.getFIFORed(); // 讀取紅外線

        double fred = (double)red;
        double fir = (double)ir;
        avered = avered * frate + fred * (1.0 - frate); // average red level by low pass filter
        aveir = aveir * frate + fir * (1.0 - frate); // average IR level by low pass filter
        sumredrms += (fred - avered) * (fred - avered); // square sum of alternate component of red level
        sumirrms += (fir - aveir) * (fir - aveir); // square sum of alternate component of IR level
        
        if ((cnt % NUM) != 0) {
          return;
        }
        
        double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
        SpO2 = -23.3 * (R - 0.4) + 100;
        ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2;

        // 血氧需介於 [90, 100) %
        if (ESpO2 <= MINIMUM_SPO2) {
            ESpO2 = MINIMUM_SPO2;
        }
        
        if (ESpO2 > MAXIMUM_SPO2) {
            ESpO2 = MAXIMUM_SPO2;
        }
        
        sumredrms = 0.0;
        sumirrms = 0.0;
        SpO2 = 0;
        cnt = 0;
        particleSensor.nextSample();
    } else {
        // 清除心跳數據
        for (byte rx = 0; rx < RATE_SIZE; rx++) {
            rates[rx] = 0;
        }

        first = true;
        beatAvg = 0;
        rateSpot = 0;
        lastBeat = 0;
        
        // 清除血氧數據
        avered = 0;
        aveir = 0;
        sumirrms = 0;
        sumredrms = 0;
        SpO2 = 0;
        ESpO2 = 90.0;

        display.clearDisplay();
        initText();
    }
}
