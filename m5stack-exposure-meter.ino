#if defined(ARDUINO_M5Stack_Core_ESP32) || defined(ARDUINO_M5STACK_FIRE)
#include "M5Stack.h"
#elif defined(ARDUINO_M5STACK_Core2)  // M5Stack Core2
#include "M5Core2.h"
#endif
#include <M5_DLight.h>
#define DELAY 0
// #define debug

M5_DLight sensor;

// tables for AV, TV, ISO // 値のテーブル
const char *av_table_p[]{ "1.0", "1.4", "2.0", "2.8", "4.0", "5.6", "8.0", "11", "16", "22", "32" };
const char *av_table_n[] = { "1.0", "0.7", "0.5" };
const int av_table_p_size = 11;
const int av_table_n_size = 3;
const char *tv_table_p[] = { "1", "1:2", "1:4", "1:8", "1:15", "1:30", "1:60", "1:125", "1:250", "1:500", "1:1000", "1:2000", "1:4000" };
const char *tv_table_n[] = { "1", "2", "4", "8", "16" };
const int tv_table_p_size = 13;
const int tv_table_n_size = 5;
const char *iso_table_p[] = { "100", "200", "400", "800", "1600", "3200" };
const char *iso_table_n[] = { "100", "50" };
const int iso_table_p_size = 6;
const int iso_table_n_size = 2;

// 値の描画座標
const int pos_ev_x = 40;
const int pos_ev_y = 0;
const int pos_lx_x = 160;
const int pos_lx_y = 0;
const int pos_av_x = 40;
const int pos_av_y = 40;
const int pos_tv_x = 40;
const int pos_tv_y = 110;
const int pos_iso_x = 80;
const int pos_iso_y = 180;

// 設定モード
const int MODE_AV = 0;
const int MODE_TV = 1;
const int MODE_ISO = 2;
int mode = MODE_AV;

// 計算値
int ev = 0;
uint16_t lx = 2.5; // 0EV
int av = 0;
int tv = 0;
int iso = 0;

// 表示初期値
String av_disp = av_table_p[av];
String tv_disp = tv_table_p[tv];
String iso_disp = tv_table_p[iso];

// timer settings // タイマー設定
hw_timer_t *timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t isrCounter = 0;
bool intrrupted = false;


void IRAM_ATTR onTimer() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter++;
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(50);

  // LCD初期設定
  M5.Lcd.setTextFont(6);
  M5.Lcd.fillScreen(BLACK);
#ifdef debug
  M5.Lcd.fillScreen(WHITE);
#endif
  M5.Lcd.setTextColor(WHITE, BLACK);

  // DLIGHTセンサー初期設定
  sensor.begin();
  sensor.setMode(CONTINUOUSLY_H_RESOLUTION_MODE);

  timerSemaphore = xSemaphoreCreateBinary();
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 100000, true);
  timerAlarmEnable(timer);

  // 初期描画
  draw_mode();
  draw_value();
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.drawString("EV", pos_ev_x - 20, pos_ev_y, 2);
  M5.Lcd.drawString("lx", M5.Lcd.width() - 40, pos_ev_y, 2);
  M5.Lcd.setTextColor(WHITE, BLACK);
}

void loop() {
  M5.update();

  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
    uint32_t isrCount = 0;
    portENTER_CRITICAL(&timerMux);
    isrCount = isrCounter;
    portEXIT_CRITICAL(&timerMux);
    intrrupted = true;
  }

  if (intrrupted) {
    lx = sensor.getLUX();
    if (lx == 0) {
      ev = 0;
    } else {
      ev = round(log(lx / 2.5) / log(2));
    }

#ifdef debug
    ev = 10;
#endif
    
    switch (mode) {
      case MODE_AV:
        // TV値設定
        tv = iso + ev - av;
        break;
      case MODE_TV:
        // AV値設定
        av = iso + ev - tv;
        break;
      case MODE_ISO:
        // ISO値設定
        // iso = ev - av - tv;
        // TV値設定
        tv = iso + ev - av;
        break;
    }

    draw_value();
    intrrupted = false;
  }

  if (M5.BtnA.wasReleased() || M5.BtnA.pressedFor(1000, 200)) {
    // モードボタン
    mode = (mode + 1) % 3;
    draw_mode();
  } else if (M5.BtnB.wasReleased() || M5.BtnB.pressedFor(1000, 200)) {
    // -(暗くする)ボタン
    switch (mode) {
      case MODE_AV:
        // AV値インクリメント(インデックスが増えるほうが絞られ暗くなる)
        av++;
        // テーブルから外れないように制限
        if (av >= av_table_p_size) {
          av = av_table_p_size - 1;
        }
        // TV値設定
        tv = iso + ev - av;
        break;
      case MODE_TV:
        // TV値インクリメント(インデックスが増えるほうが速くなり暗くなる)
        tv++;
        // テーブルから外れないように制限
        if (tv >= tv_table_p_size) {
          tv = tv_table_p_size - 1;
        }
        // AV値設定
        av = iso + ev - tv;
        break;
      case MODE_ISO:
        // ISO値デクリメント(インデックスが減るほうが感度が下がり暗くなる)
        iso--;
        // テーブルから外れないように制限
        if (iso <= -iso_table_n_size) {
          iso = -iso_table_n_size + 1;
        }
        // TV値設定
        tv = iso + ev - av;
        break;
    }
    draw_value();
  } else if (M5.BtnC.wasReleased() || M5.BtnC.pressedFor(1000, 200)) {
    // -(明るくする)ボタン
    switch (mode) {
      case MODE_AV:
        // AV値デクリメント(インデックスが減るほうが開かれ明るくなる)
        av--;
        // テーブルから外れないように制限
        if (av <= -av_table_n_size) {
          av = -av_table_n_size + 1;
        }
        // TV値設定
        tv = ev - av;
        break;
      case MODE_TV:
        // TV値デクリメント(インデックスが減るほうが遅くなり明るくなる)
        tv--;
        // テーブルから外れないように制限
        if (tv <= -tv_table_n_size) {
          tv = -tv_table_n_size + 1;
        }
        // AV値設定
        av = ev - tv;
        break;
      case MODE_ISO:
        // ISO値インクリメント(インデックスが増えるほうが感度が上がり明るくなる)
        iso++;
        // テーブルから外れないように制限
        if (iso >= iso_table_p_size) {
          iso = iso_table_p_size - 1;
        }
        // TV値設定
        tv = iso + ev - av;
        break;
    }
    draw_value();
  } else if (M5.BtnB.wasReleasefor(700)) {
  }
}


void draw_value() {
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  // EV値表示
  // 描画
  M5.Lcd.setTextPadding((M5.Lcd.width() - pos_ev_x - pos_ev_x) / 2);
  M5.Lcd.drawNumber(ev, pos_ev_x, pos_ev_y, 2);
  Serial.print("ev: ");
  Serial.println(ev);

  // lx値表示
  // 描画
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.setTextPadding((M5.Lcd.width() - pos_ev_x - pos_ev_x) / 2);
  M5.Lcd.drawNumber(lx, pos_lx_x, pos_lx_y, 2);
  Serial.print("lx: ");
  Serial.println(lx);

  M5.Lcd.setTextColor(WHITE, BLACK);
  // AV値表示
  // テーブル選択
  int av_tmp = av;
  if (av_tmp >= 0) {
    // AV値がテーブルのインデックスを外れた場合は赤文字で表示
    if (av_tmp >= av_table_p_size) {
      av_tmp = av_table_p_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    av_disp = av_table_p[av_tmp];
  } else if (av_tmp < 0) {
    // AV値がテーブルのインデックスを外れた場合は赤文字で表示
    if (av_tmp <= -av_table_n_size) {
      av_tmp = av_table_n_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    av_disp = av_table_n[abs(av_tmp)];
  }
  // 描画
  M5.Lcd.setTextPadding(M5.Lcd.width() - pos_av_x - pos_av_x);
  M5.Lcd.drawString(av_disp, pos_av_x, pos_av_y);

  Serial.print("av: ");
  Serial.println(av);
  Serial.print("F");
  Serial.println(av_disp);

  // TV値表示
  // テーブル選択
  int tv_tmp = tv;
  if (tv_tmp >= 0) {
    // TV値がテーブルのインデックスを外れた場合は赤文字で表示
    if (tv_tmp >= tv_table_p_size) {
      tv_tmp = tv_table_p_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    tv_disp = tv_table_p[tv_tmp];
  } else if (tv < 0) {
    // AV値がテーブルのインデックスを外れた場合は赤文字で表示
    if (tv_tmp <= -tv_table_n_size) {
      tv_tmp = tv_table_n_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    tv_disp = tv_table_n[abs(tv_tmp)];
  }
  // 描画
  M5.Lcd.setTextPadding(M5.Lcd.width() - pos_tv_x - pos_tv_x);
  M5.Lcd.drawString(tv_disp, pos_tv_x, pos_tv_y);

  Serial.print("tv: ");
  Serial.println(tv);
  Serial.print(tv_disp);
  Serial.println(" s");

  // ISO値表示
  // テーブル選択
  int iso_tmp = iso;
  if (iso_tmp >= 0) {
    // ISO値がテーブルのインデックスを外れた場合は赤文字で表示
    if (iso_tmp >= iso_table_p_size) {
      iso_tmp = iso_table_p_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    iso_disp = iso_table_p[iso_tmp];
  } else if (iso < 0) {
    // ISO値がテーブルのインデックスを外れた場合は赤文字で表示
    if (iso_tmp <= -iso_table_n_size) {
      iso_tmp = iso_table_n_size - 1;
      M5.Lcd.setTextColor(RED, BLACK);
    }
    iso_disp = iso_table_n[abs(iso_tmp)];
  }
  // 描画
  M5.Lcd.setTextPadding(M5.Lcd.width() - pos_iso_x - pos_iso_x);
  M5.Lcd.drawString(iso_disp, pos_iso_x, pos_iso_y, 4);

  Serial.print("iso: ");
  Serial.println(iso);
  Serial.print("ISO ");
  Serial.println(iso_disp);
}

void draw_mode() {
  Serial.println("draw_mode()");
  Serial.printf("mode: %d\n", mode);
  M5.Lcd.setTextPadding(100);
  M5.Lcd.drawString("mode", 50, 224, 2);

  switch (mode) {
    case MODE_AV:
      M5.Lcd.drawString("-(smaller)", 126, 224, 2);
      M5.Lcd.drawString("+(larger)", 225, 224, 2);
      M5.Lcd.setTextPadding(0);
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.drawChar('F', pos_av_x - 20, pos_av_y + 16, 4);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.drawChar('s', pos_tv_x + 240, pos_tv_y + 16, 4);
      M5.Lcd.drawString("ISO", pos_iso_x - 60, pos_iso_y, 4);
      break;
    case MODE_TV:
      M5.Lcd.drawString("-(faster)", 130, 224, 2);
      M5.Lcd.drawString("+(slower)", 224, 224, 2);
      M5.Lcd.setTextPadding(0);
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.drawChar('s', pos_tv_x + 240, pos_tv_y + 16, 4);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.drawChar('F', pos_av_x - 20, pos_av_y + 16, 4);
      M5.Lcd.drawString("ISO", pos_iso_x - 60, pos_iso_y, 4);
      break;
    case MODE_ISO:
      M5.Lcd.drawString("-(lower)", 132, 224, 2);
      M5.Lcd.drawString("+(higher)", 225, 224, 2);
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.setTextPadding(0);
      M5.Lcd.drawString("ISO", pos_iso_x - 60, pos_iso_y, 4);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.drawChar('F', pos_av_x - 20, pos_av_y + 16, 4);
      M5.Lcd.drawChar('s', pos_tv_x + 240, pos_tv_y + 16, 4);
      break;
  }
  M5.Lcd.setTextPadding(0);
}
