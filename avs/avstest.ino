#include <Arduino.h>
#include <DWIN_Arduino.h>
#include <Ticker.h>     //https://github.com/sstaub/Ticker
#include <DHT.h>        //библиотека для подключения датчика температуры и влажности к плате arduino
#include <ThreeWire.h>  // Подключаем библиотеку ThreeWire
#include <RtcDS1302.h>  // Подключаем библиотеку RtcDS1302
#include <avr/eeprom.h>
#include <Timer.h>

#define DGUS_BAUD 9600
#define dwinSerial Serial2  // 17 (RX) 16 (TX)
DWIN hmi(dwinSerial, DGUS_BAUD);

#define DHTPin 31             //пин 12 для датчика температуры и влажности
#define DHTType DHT21         //тип датчика температуры и влажности
#define messageleak 0x22      //адрес иконки отображения состояния протечки
#define messagepressure 0x32  //адрес иконки отображения состояния давления
#define messagegas 0x42       //адрес иконки отображения состояния газа
#define leakpin A0            //аналоговый пин A0 для подключения датчика протечки
#define gaspin 4              //пин для подключения датчика газа
#define pressurepin A1        //аналоговый пин A1 для подключения датчика давления
#define RST_PIN 6
#define DAT_PIN 7
#define CLK_PIN 5

//~ Установка временных промежутков ~//
#define tickicon 1500

ThreeWire myWire(DAT_PIN, CLK_PIN, RST_PIN);  // Указываем вывода DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

int relayPins[4] = { 8, 9, 10, 11 };
int8_t relayState[5] = { 0, 0, 0, 0, 0 };
byte counter;
byte lastByteCopy;

void onHMIEvent(String address, int lastByte, String message, String response);

void iconUpdate();

void sendSensors();
void readSensors();
void TimeNow();
void HoursPlus();


Ticker timerIconTicker(iconUpdate, tickicon);

DHT dht(DHTPin, DHTType);  //инициализация библиотеки и датчика
int hum = 0;               // переменная для обозначения текущего показателя влажности
int hum0 = 0;              // переменная последнего выведенного показания влажности на дисплей
int hum1;                  // переменная с заданным пользователем показания влажности
int Temp;                  // переменная для обозначения текущего показателя температуры
int Temp0 = 0;             // переменная последнего выведенного показания температуры на дисплей
int Temp1;                 // переменная с заданным пользователем показания температуры
int leak = 0;              // переменная для обозначения текущего показателя протечки
int leak0 = 0;             // переменная последнего выведенного показания протечки на дисплей
int pressure = 1;          // переменная для обозначения текущего показателя давления
int pressure0 = 0;         // переменная последнего выведенного показания давления на дисплей
int deltat;                // дельта температ
int SystemWorkTime;        // общие часы работа системы
byte timerstate;           // управление системой автоматически по времени
byte modeState;            // охлаждение или нагрев (1 - нагрев, 0 - охлаждение)
int lastSystemWorkTime;


int startTimeHour;
int startTimeMinute;

int endTimeHour;
int endTimeMinute;

int currentTimeHour;
int currentTimeMinute;

byte gasvalue = 0;   // переменная для обозначения текущего показателя газа
byte gasvalue0 = 0;  // переменная последнего выведенного показания газа на дисплей

long adctimer;  // чтение и обновление данных только через некоторое время

String inputValue;
Timer timer;

void setup() {
  Serial.begin(9600);
  Rtc.Begin();
  hmi.restartHMI();
  delay(3000);
  counter = 0;
  while (counter < 4)  // Number of relays 4, make all outputs LOW
  {
    pinMode(relayPins[counter], OUTPUT);
    digitalWrite(relayPins[counter], LOW);
    counter++;
  }
  hmi.hmiCallBack(onHMIEvent);

  inputValue = String(hmi.getPage());
  hmi.setVP(0x1000, 0xFF);
  dht.begin();
  pinMode(leakpin, INPUT);      //установка режима пина протечки на получение данных
  pinMode(gaspin, INPUT);       //установка режима пина газа на получение данных
  pinMode(pressurepin, INPUT);  //установка режима пина давления на получение данных

  RtcDateTime now = Rtc.GetDateTime();
  currentTimeHour = now.Hour();
  currentTimeMinute = now.Minute();
  startTimeHour = eeprom_read_word(0);
  startTimeMinute = eeprom_read_word(2);
  endTimeHour = eeprom_read_word(4);
  endTimeMinute = eeprom_read_word(6);
  deltat = eeprom_read_word(8);
  Temp1 = eeprom_read_word(10);
  hum1 = eeprom_read_word(12);
  SystemWorkTime = eeprom_read_word(14);
  timerstate = eeprom_read_byte(16);
  modeState = eeprom_read_byte(17);
  lastSystemWorkTime = SystemWorkTime % 100;


  TimeNow();

  timer.every(1000, readSensors);
  timer.every(5000, sendSensors);
  timer.every(60000, TimeNow);
  timer.every(3600000, HoursPlus);
  //hmi.setBrightness(0x20);
  Serial.println("Климат-снаб ver. 0.6.1");
}

void loop() {
  hmi.listen();
  if (timerIconTicker.state() == RUNNING) {  // if Ticker is running update it
    timerIconTicker.update();
  }
  if (lastSystemWorkTime == 100) {
    hmi.setPage(0);
    hmi.setBrightness(0);
  }
  timer.update();
}

void onHMIEvent(String address, int lastByte, String message, String response) {
  Serial.println("OnEvent : [ A : " + address + " | D : " + String(lastByte, HEX) + " | M : " + message + " | R : " + response + " ]");
  if (address == "1000" && (lastByte <= 0xFF)) {
    delay(200);
    counter = 0;
    while (counter < 6) {
      ((lastByte >> counter) & 1) ? relayState[counter] = 0 : relayState[counter] = 1;
      counter++;
    }
    (relayState[4] == 1) ? eeprom_update_byte(16, relayState[4]) : eeprom_update_byte(16, relayState[4]);  //кнопка времени
    timerstate = relayState[4];
    (relayState[5] == 1) ? eeprom_update_byte(17, relayState[5]) : eeprom_update_byte(17, relayState[5]);  //кнопка режима охлад/нагрев
    modeState = relayState[5];
    // ((relayState[1] == 1) & (relayState[2] == 1)) ? digitalWrite(relayPins[1], HIGH) : digitalWrite(relayPins[1], LOW);
    // ((relayState[0] == 1) & (relayState[2] == 1)) ? digitalWrite(relayPins[0], HIGH) : digitalWrite(relayPins[0], LOW);
    lastByteCopy = lastByte;
    timerIconTicker.start();
    timerIconTicker.interval(tickicon);
    Serial.println(lastByte);
    Serial.println(relayState[4]);
  }
  if (address == "2000") {
    if (message == "5689") {
      hmi.setPage(3);
      hmi.setText(0x5000, String(Temp1));
      hmi.setText(0x6000, String(hum1));
      hmi.setText(0x7200, String(deltat));
      // (timerstate == 0) ? hmi.setVP(0x1000, 0xFF) : hmi.setVP(0x1000, 239);
      if (timerstate == 0) {
        (modeState == 0) ? hmi.setVP(0x1000, 252) : hmi.setVP(0x1000, 220);
      } else {
        (modeState == 0) ? hmi.setVP(0x1000, 236) : hmi.setVP(0x1000, 204);
      }
    }
  }

  if (address == "5000") {
    Temp1 = message.toInt();
    eeprom_update_word(10, Temp1);
  }
  if (address == "6000") {
    hum1 = message.toInt();
    eeprom_update_word(12, hum1);
  }
  if (address == "7200") {
    deltat = message.toInt();
    eeprom_update_word(8, deltat);
  }
  if (address == "8100") {
    startTimeHour = message.toInt();
    if (startTimeHour > 23) {
      hmi.setText(0x8100, "00");
      startTimeHour = 0;
    }
    eeprom_update_word(0, startTimeHour);
  }

  if (address == "8200") {
    startTimeMinute = message.toInt();
    if (startTimeMinute > 59) {
      hmi.setText(0x8200, "00");
      startTimeMinute = 0;
    }
    eeprom_update_word(2, startTimeMinute);
  }

  if (address == "8300") {
    endTimeHour = message.toInt();
    if (endTimeHour > 23) {
      hmi.setText(0x8300, "00");
      endTimeHour = 0;
    }
    eeprom_update_word(4, endTimeHour);
  }

  if (address == "8400") {
    endTimeMinute = message.toInt();
    if (endTimeMinute > 59) {
      hmi.setText(0x8400, "00");
      endTimeMinute = 0;
    }
    eeprom_update_word(6, endTimeMinute);
  }

  if (address == "8800") {
    currentTimeHour = message.toInt();
    if (currentTimeHour > 23) {
      hmi.setText(0x8800, "00");
      currentTimeHour = 0;
    }
    TimeChange();
  }

  if (address == "8900") {
    currentTimeMinute = message.toInt();
    if (currentTimeMinute > 59) {
      hmi.setText(0x8900, "00");
      currentTimeMinute = 0;
    }
    TimeChange();
  }
}
void iconUpdate() {
  static byte oldIconStatus;
  if (lastByteCopy != oldIconStatus) {
    oldIconStatus = lastByteCopy;
    hmi.setVP(0x1000, lastByteCopy);
    Serial.println("icon update");
  }
  timerIconTicker.stop();
}

void TimeChange() {
  RtcDateTime now = Rtc.GetDateTime();
  // Установка даты и времени вручную
  // Формат: год, месяц, день, час, минута, секунда
  RtcDateTime manualSetTime(now.Year(), now.Month(), now.Day(), currentTimeHour, currentTimeMinute, 0);  // Установите нужные значения
  Rtc.SetDateTime(manualSetTime);                                                                        // Установка времени
}

void TimeNow() {
  RtcDateTime now = Rtc.GetDateTime();
  hmi.setText(0x8800, String(now.Hour()));
  hmi.setText(0x8900, String(now.Minute()));
  hmi.setText(0x8100, String(startTimeHour));
  hmi.setText(0x8200, String(startTimeMinute));
  hmi.setText(0x8300, String(endTimeHour));
  hmi.setText(0x8400, String(endTimeMinute));
  String timework = String(eeprom_read_word(14)) + "h";
  hmi.setText(0x7100, timework);
}

void HoursPlus() {
  SystemWorkTime = SystemWorkTime + 1;
  eeprom_update_word(14, SystemWorkTime);
  lastSystemWorkTime = lastSystemWorkTime + 1;
}

void relayControl() {
  int sysPower;
  RtcDateTime now = Rtc.GetDateTime();

  if (timerstate == 1) {
    if (now.Hour() > startTimeHour || (now.Hour() == startTimeHour && now.Minute() >= startTimeMinute)) {
      if (now.Hour() < endTimeHour || (now.Hour() == startTimeHour && now.Minute() <= endTimeHour)) {
        sysPower = 1;
      } else {
        sysPower = 0;
      }
    } else {
      sysPower = 0;
    }
  } else {
    sysPower = 1;
  }

  if (sysPower == 1) {
    (relayState[3] == 1) ? digitalWrite(relayPins[3], HIGH) : digitalWrite(relayPins[3], LOW);
    if (relayState[2] == 1) {
      digitalWrite(relayPins[2], HIGH);
      if (modeState == 1) {
        if (Temp - Temp1 > deltat) {
          digitalWrite(relayPins[0], HIGH);
          digitalWrite(relayPins[1], LOW);
        } else {
          digitalWrite(relayPins[0], LOW);
          digitalWrite(relayPins[1], LOW);
        }
      }
      if (modeState == 0) {
        if (Temp1 - Temp > deltat) {
          digitalWrite(relayPins[1], HIGH);
          digitalWrite(relayPins[0], LOW);
        } else {
          digitalWrite(relayPins[0], LOW);
          digitalWrite(relayPins[1], LOW);
        }
      }
    } else {
      digitalWrite(relayPins[0], LOW);
      digitalWrite(relayPins[1], LOW);
      digitalWrite(relayPins[2], LOW);
    }
  } else {
    digitalWrite(relayPins[0], LOW);
    digitalWrite(relayPins[1], LOW);
    digitalWrite(relayPins[2], LOW);
    digitalWrite(relayPins[3], LOW);
  }
}

void readSensors() {                   // чтение данных с датчиков
  hum = dht.readHumidity();            //считывание влажности с датчика температуры и влажности в переменную
  Temp = dht.readTemperature();        //считывание температуры с датчика температуры и влажности в переменную
  leak = analogRead(leakpin);          // получение данных с датчика протечки
  pressure = analogRead(pressurepin);  // считываение данных с датчика
  gasvalue = digitalRead(gaspin);      // получение значение с датчика газа
  relayControl();
}

void sendSensors() {                 // отправка данных на экран dwin
  if (millis() - adctimer > 5010) {  // таймер в 5 секунд на отправку данных

    if (hum != hum0) {                   // Компаратор прошлых и текущих данных по влажности
      hmi.setText(0x6100, String(hum));  // Отправление данных влажности
      hum0 = hum;                        // Замена значений сравнения
    }

    if (Temp != Temp0) {                  // Компаратор прошлых и текущих данных по температуре
      hmi.setText(0x5100, String(Temp));  // Отправление данных температуры
      Temp0 = Temp;                       // Замена значений сравнения
    }

    if (leak > 500) {  // условие для отображения показателя "всё в норме"
      hmi.setVP(0x2200, 0x00);
    } else {  // условие для отображения показателя "проблема"
      hmi.setVP(0x2200, 0xFF);
    }

    if (pressure == 1023) {  // условие для отображения показателя "всё в норме"
      hmi.setVP(0x3200, 0x00);
    } else {  // условие для отображения показателя "проблема"
      hmi.setVP(0x3200, 0xFF);
    }

    if (gasvalue == 1) {  // условие для отображения показателя "всё в норме"
      hmi.setVP(0x4200, 0x00);
    } else {  // условие для отображения показателя "проблема"
      hmi.setVP(0x4200, 0xFF);
    }

    adctimer = millis();  // обнуление таймера
  }
}
