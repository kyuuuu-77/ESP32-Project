#include "HX711.h"
#include "SoftwareSerial.h"
#include "Preferences.h"
#include "Wire.h"
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"

// 핀 설정
#define buzz 27
#define DOUT 25  //엠프 데이터 아웃 핀 넘버 선언
#define CLK 26
#define BATTERY_PIN 14  // 배터리 전압을 측정할 핀 (ADC 포트)
#define CHARGE_PIN 13   // 충전 여부를 측정할 핀(ADC 포트)

// 가속도 센서 핀 설정
#define Acc_X 33
#define Acc_Y 34
#define Acc_Z 35

// LCD 사이즈 설정
#define SCREEN_WIDTH 128 // 너비
#define SCREEN_HEIGHT 64 // 높이

// esp32 동작에 필요한 전역 변수들
float calibration_factor = 11500;  //캘리브레이션 값
float bat_max;
float bat_min;
float voltage;  // 배터리 전압
int sensorValue;
int chargeValue;
int battery;            // 배터리 퍼센트
double weight = 0;      // 측정된 무게 값
bool charging = false;  // 충전 상태 여부
bool lockMode = false;  // 캐리어 잠금 여부

// 암호 설정 변수
String password = "qwerty1234";  // 초기 비밀번호
bool auth = false;            // 인증 여부

// 블루투스 설정
SoftwareSerial bluetooth(16, 17);

// 무게 센서 설정
HX711 scale(DOUT, CLK);  //엠프 핀 선언

// NVS에 데이터 저장
Preferences preferences;

// LCD 설정
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Serial.begin(115200);
  bluetooth.begin(9600);

  pinMode(buzz, OUTPUT);

  scale.set_scale();                    // 무게 측정 초기화
  scale.tare();                         // 무게 값을 0으로 초기화
  scale.set_scale(calibration_factor);  // 스케일 지정

  pinMode(BATTERY_PIN, INPUT);
  pinMode(CHARGE_PIN, INPUT);

  pinMode(Acc_X, INPUT);
  pinMode(Acc_Y, INPUT);
  pinMode(Acc_Z, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 Fault"));
    for (;;);
  }

  // NVS 초기화
  preferences.begin("nvs_storage", false);

  // NVS의 인증 암호
  String storedPassword = preferences.getString("password", "null");
  if (storedPassword != "null") {
    Serial.println("저장된 패스워드 " + storedPassword + " 를 불러옴!");
    password = storedPassword;
  } else {
    Serial.println("저장된 데이터가 없음...");
  }

  // NVS의 잠금 번호
  unsigned int storedLock = preferences.getUInt("lock", 0);
  Serial.println("저장된 잠금모드 " + String(storedLock) + " 를 불러옴!");
  if (storedLock == 1) {
    lockMode = true;
  } else {
    lockMode = false;
  }
  preferences.end();
}

// 가속도 값 저장 (배열)
int list_X[3] = {0, 0, 0};
int list_Y[3] = {0, 0, 0};
int list_Z[3] = {0, 0, 0};

void loop() {

  sensorValue = analogRead(BATTERY_PIN);  // 아날로그 값 읽기 (ADC 34번 핀)
  sensorValue += 72;

  chargeValue = analogRead(CHARGE_PIN);  // 아날로그 값 읽기 (ADC 35번 핀)

  bat_max = 4095 * (2.0 / 3.3);  // 4.0V -> 완충
  bat_min = 4095 * (1.6 / 3.3);  // 3.2V -> 방전

  voltage = 6.6 * (sensorValue / 4095.);
  battery = (int)((sensorValue - bat_min) / (bat_max - bat_min) * 100);

  display.clearDisplay();   // 디스플레이 초기화
  display.setTextSize(1);   // 텍스트 크기 설정
  display.setTextColor(SSD1306_WHITE); // 텍스트 색상 설정
  display.setCursor(0, 0);  // 텍스트 시작 위치 설정

  display.println(F("ESP32 OLED 128x64"));
  display.print("Voltage : ");
  display.println(String(voltage) + "V");
  display.print("Battery : ");
  display.println(String(battery) + "%");

  if (lockMode) {
    int Val_X = analogRead(Acc_X);
    int Val_Y = analogRead(Acc_Y);
    int Val_Z = analogRead(Acc_Z);

    Serial.print("X : " + String(Val_X));
    Serial.print(" | Y : " + String(Val_Y));
    Serial.println(" | Z : " + String(Val_Z));

    display.println("Accerlator : ");
    display.println(String(Val_X) + "|" + String(Val_Y) + "|" + String(Val_Z));

    for (int i = 1; i < 3; i++) {
      list_X[i] = list_X[i-1];
      list_Y[i] = list_Y[i-1];
      list_Z[i] = list_Z[i-1];
    }
    list_X[0] = Val_X;
    list_Y[0] = Val_Y;
    list_Z[0] = Val_Z;

    if ((abs(list_X[0] - list_X[1]) > 50 && list_X[1] != 0) || (abs(list_Y[0] - list_Y[1]) > 50 && list_Y[1] != 0) || (abs(list_Z[0] - list_Z[1]) > 50 && list_Z[1] != 0)) {
      digitalWrite(27, HIGH);
      delay(500);
      digitalWrite(27, LOW);
      delay(500);
      Serial.println("벨울림");
      display.println("Ring Bell!!!!");
    }
  } else {
    Serial.println(String(sensorValue) + " => " + String(battery) + " %, " + String(voltage) + " V");
  }

  if (!auth) {  // 인증전
    if (bluetooth.available() > 0) {
      String readData = bluetooth.readStringUntil('\n');  // 개행 문자까지 읽음
      Serial.println(readData);
      display.print("BLE : ");
      display.println(String(readData));

      if (readData.startsWith("auth_")) {
        Serial.println("auth 진입!");
        if (readData == "auth_" + password) {
          auth = true;
          Serial.println("인증 성공!!!");
          bluetooth.println("auth_suc");
        } else {
          auth = false;
          Serial.println("인증 실패!!!");
          bluetooth.println("auth_fail");
        }
        delay(200);
      } else if (readData.startsWith("menu")) {  // 앱에서 인증하고 다시 시도하게 해야함
        auth = true;
        Serial.println("menu 진입!");
        bluetooth.println("auth_suc");
      }
    }
  } else {  // 인증후
    if (bluetooth.available() > 0) {
      String readData = bluetooth.readStringUntil('\n');  // 개행 문자까지 읽음
      Serial.println(readData);
      display.print("BLE : ");
      display.println(String(readData));

      if (readData.startsWith("DISCONNECT")) {
        Serial.println("인증 취소!!!");
        auth = false;
      } else if (readData.startsWith("auth_")) {
        Serial.println("auth 진입!");
        if (readData == "auth_" + password) {
          auth = true;
          Serial.println("인증 성공!!!");
          bluetooth.println("auth_suc");
        } else {
          auth = false;
          Serial.println("인증 실패!!!");
          bluetooth.println("auth_fail");
        }
        delay(200);
      } else if (readData.startsWith("change")) {
        password = readData.substring(7);
        Serial.println("새로운 인증번호 => " + password);
        bluetooth.println("change_suc");

        preferences.begin("nvs_storage", false);
        preferences.putString("password", password);
        Serial.println("NVS에 저장 성공! 저장된 값 => " + preferences.getString("password", "null"));
        preferences.end();
      }

      if (readData == "menu 1") {  // 벨 울리기
        bluetooth.println("ring_suc");
        delay(200);
        while (1) {
          digitalWrite(27, HIGH);
          delay(500);
          digitalWrite(27, LOW);
          delay(500);
          Serial.println("벨울림");

          readData = bluetooth.readStringUntil('\n');
          if (readData == "menu 2") {  // 벨 울리기 중지
            bluetooth.println("ring_stop");
            Serial.println("벨꺼짐");
            break;
          }
        }
        delay(200);
      }

      if (readData == "menu 3") {             // 무게 값을 전송
        scale.set_scale(calibration_factor);  //캘리브레이션 값 적용
        weight = scale.get_units();

        Serial.println(String(weight) + " Kg");
        bluetooth.println(String(weight));
        delay(200);
      }

      if (readData == "menu 4") {  // 배터리 상태 전송
        if (battery > 100) {
          battery = 100;
        } else if (battery < 0) {
          battery = 0;
        }
        if (chargeValue >= 1500) {
          charging = true;
          Serial.println("충전중...");
          Serial.println(String(battery) + "+" + "/" + String(voltage));
          bluetooth.println(String(battery) + "+" + "/" + String(voltage));
        } else {
          charging = false;
          Serial.println("충전중 아님!!!");
          Serial.println(String(battery) + "/" + String(voltage));
          bluetooth.println(String(battery) + "/" + String(voltage));
        }
      }

      if (readData == "menu 5") {
        if (lockMode) {
          Serial.println("lock_on");
          bluetooth.println("lock_on");
        } else {
          Serial.println("lock_off");
          bluetooth.println("lock_off");
        }
      }
      if (readData == "lock_on") {
        lockMode = true;
        Serial.println("캐리어 잠금!");
        bluetooth.println("lock_suc");

        preferences.begin("nvs_storage", false);
        preferences.putUInt("lock", 1);
        Serial.println("NVS에 저장 성공! 저장된 값 => " + preferences.getUInt("lock", 0));
        preferences.end();
      }
      if (readData == "lock_off") {
        lockMode = false;
        Serial.println("캐리어 잠금해제!");
        bluetooth.println("unlock_suc");

        preferences.begin("nvs_storage", false);
        preferences.putUInt("lock", 0);
        Serial.println("NVS에 저장 성공! 저장된 값 => " + preferences.getUInt("lock", 0));
        preferences.end();
      }
    }
  }
  display.display();
  delay(500);
}
