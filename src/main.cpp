// ============================================================
// 正点原子 ATK-DNESP32S3-BOX V1.1 网络收音机
// 基于 zhuhai-esp/ESP32-S3-AmourK08-4Keys :: K08-4Keys-NetRadio 修改
//
// 硬件适配:
//   屏幕 ST7789 320x240 i8080 并口  -> include/lgfx.h
//   音频 ES8311 (I2C)              -> include/atk_hardware.h
//   IO   XL9555 (功放/背光/按键)    -> include/atk_hardware.h
//   电台 喜马拉雅开放流 (mp3)       -> 与原项目相同
// ============================================================
#include "lgfx.h"
#include "atk_hardware.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Audio.h>
#include <WiFi.h>
#include <vector>

// ---------- 板级对象 ----------
XL9555 atk_io;
ES8311 atk_codec;

// ---------- 显示 ----------
#define FONT16 &fonts::efontCN_16
#define SCREEN_W 320
#define SCREEN_H 240
#define CX (SCREEN_W / 2)          // 水平中心 = 160
LGFX tft;
LGFX_Sprite sp(&tft);

// ---------- 音频 ----------
Audio audio;
int curVolume = 15;                // 0~21
const char *WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

// ---------- 电台 ----------
#define FM_URL "http://lhttp.qtfm.cn/live/%d/64k.mp3"
typedef struct { u32_t id; String name; } RadioItem;

static std::vector<RadioItem> radios = {
    {4915, "清晨音乐台"}, {1223, "怀旧好声音"}, {4866, "浙江音乐调频"},
    {1739, "厦门音乐广播"}, {1271, "深圳飞扬971"}, {20240, "山东经典音乐广播"},
    {1296, "湖北经典音乐广播"}, {267, "上海经典947"}, {20003, "天津TIKI FM100.5"},
    {1111, "四川城市之音"}, {4237, "长沙FM101.7城市之声"}, {332, "北京音乐广播"},
    {274, "上海动感101"}, {839, "哈尔滨音乐广播"}, {1283, "江门旅游之声"},
    {647, "重庆音乐广播"}, {4921, "郑州音乐广播"}, {4871, "唐山音乐广播"},
    {20033, "1047 Nice FM"}, {4846, "893音乐广播"},
};
int curIndex = 0;

// ---------- 计时 ----------
long t1s = 0, t10ms = 0;
char buf[128] = {0};

// ============================================================
// 显示辅助
// ============================================================
void showStation() {
    sprintf(buf, "%d.%s", curIndex + 1, radios[curIndex].name.c_str());
    sp.createSprite(SCREEN_W, 20);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawCentreString(buf, CX, 0);
    sp.pushSprite(0, 24);
    sp.deleteSprite();
}

void showVolume() {
    sprintf(buf, "音量: %d", curVolume);
    sp.createSprite(140, 20);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(buf, 8, 0);
    sp.pushSprite(0, SCREEN_H - 24);
    sp.deleteSprite();
}

void showIP() {
    sp.createSprite(180, 20);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawRightString(WiFi.localIP().toString().c_str(), 172, 0);
    sp.pushSprite(SCREEN_W - 180, SCREEN_H - 24);
    sp.deleteSprite();
}

void showTime() {
    struct tm info;
    if (!getLocalTime(&info)) return;
    sprintf(buf, "%d年%d月%d日 星期%s", 1900 + info.tm_year, info.tm_mon + 1,
            info.tm_mday, WEEK_DAYS[info.tm_wday]);
    sp.createSprite(SCREEN_W, 20);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawCentreString(buf, CX, 0);
    sp.pushSprite(0, 96);
    sp.deleteSprite();

    strftime(buf, 36, "%T", &info);
    sp.createSprite(SCREEN_W, 40);
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.drawCentreString(buf, CX, 0, &fonts::FreeSans24pt7b);
    sp.pushSprite(0, 120);
    sp.deleteSprite();
}

void showSplash(const char *line1, const char *line2 = nullptr) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(line1, CX, 70, FONT16);
    if (line2) tft.drawCentreString(line2, CX, 100, FONT16);
}

// ============================================================
// 播放控制
// ============================================================
void playNext(int offset) {
    int total = radios.size();
    curIndex += offset;
    if (curIndex >= total) curIndex %= total;
    if (curIndex < 0) curIndex += total;

    sprintf(buf, FM_URL, radios[curIndex].id);
    audio.connecttohost(buf);
    showStation();
}

void setVol(int offset) {
    int v = curVolume + offset;
    if (v >= 0 && v <= 21) {
        curVolume = v;
        audio.setVolume(curVolume);
        showVolume();
    }
}

// ============================================================
// 按键 (K0=BOOT/GPIO0 播放暂停/音量, K1/K2 在 XL9555 上, 低电平按下)
// ============================================================
void setupBootButton() {
    pinMode(0, INPUT_PULLUP);
}

void pollXL9555Keys() {
    static bool k1Last = true, k2Last = true;
    static unsigned long k1Time = 0, k2Time = 0;
    bool k1 = atk_io.readInput(XL_P_KEY1);
    bool k2 = atk_io.readInput(XL_P_KEY2);
    unsigned long now = millis();

    // K1: 下一台
    if (k1 != k1Last && (now - k1Time) > 200) {
        k1Time = now;
        if (!k1) playNext(1);
        k1Last = k1;
    }
    // K2: 上一台
    if (k2 != k2Last && (now - k2Time) > 200) {
        k2Time = now;
        if (!k2) playNext(-1);
        k2Last = k2;
    }
}

void pollBootButton() {
    static bool last = true;
    static unsigned long pressTime = 0, lastPress = 0;
    bool cur = digitalRead(0);
    unsigned long now = millis();
    if (cur != last && (now - lastPress) > 200) {
        if (!cur) pressTime = now;              // 按下
        else {                                   // 松开
            lastPress = now;
            if (pressTime && (now - pressTime) > 1200) {
                setVol(-1);                      // 长按: 音量减
            } else {
                audio.pauseResume();             // 短按: 播放/暂停
            }
            pressTime = 0;
        }
        last = cur;
    }
}

// ============================================================
// 初始化
// ============================================================
void initHardware() {
    Wire.begin(ATK_I2C_SDA, ATK_I2C_SCL);
    atk_io.begin();                       // 功放 + 背光打开, 探测音频芯片

    tft.init();
    tft.setRotation(0);
    tft.setColorDepth(8);
    tft.fillScreen(TFT_BLACK);
    tft.setFont(FONT16);
    sp.setColorDepth(8);
    sp.setFont(FONT16);

    if (atk_io.hasES8311()) {
        atk_codec.begin();                // ES8311 初始化
    }
    // ESP32-audioI2S: BCLK / LRC / DOUT
    audio.setPinout(ATK_I2S_BCLK, ATK_I2S_LRC, ATK_I2S_DOUT);
    audio.setVolume(curVolume);

    setupBootButton();
}

void connectWiFi() {
    showSplash("正在连接 WiFi...", "首次使用请配网");
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();
    for (int i = 0; WiFi.status() != WL_CONNECTED && i < 100; i++) delay(100);

    if (WiFi.status() != WL_CONNECTED) {
        showSplash("进入配网模式", "用 ESPTouch APP 配网");
        WiFi.mode(WIFI_MODE_APSTA);
        WiFi.beginSmartConfig();
        while (WiFi.status() != WL_CONNECTED) delay(100);
        WiFi.stopSmartConfig();
        WiFi.mode(WIFI_MODE_STA);
    }
}

void setupOTA() {
    ArduinoOTA.onStart([] {
        audio.stopSong();
        showSplash("软件升级", "请勿断电...");
    });
    ArduinoOTA.onProgress([](u32_t pro, u32_t total) {
        sprintf(buf, "升级进度: %d / %d", pro, total);
        sp.createSprite(SCREEN_W, 20);
        sp.drawCentreString(buf, CX, 0);
        sp.pushSprite(0, 120);
        sp.deleteSprite();
    });
    ArduinoOTA.onEnd([] { showSplash("升级成功", "正在重启..."); });
    ArduinoOTA.onError([](ota_error_t e) { ESP.restart(); });
    ArduinoOTA.begin();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("ATK-DNESP32S3-BOX V1.1 NetRadio boot");

    initHardware();
    showSplash("ATK BOX 网络收音机", "正在启动...");

    connectWiFi();
    configTime(8 * 3600, 0, "ntp6.aliyun.com", "cn.ntp.org.cn", "ntp.ntsc.ac.cn");

    setupOTA();

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("ATK BOX 网络收音机", CX, 8, FONT16);
    showIP();
    showVolume();

    playNext(0);
    Serial.printf("WiFi=%s  ES8311=%d  IP=%s\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                  atk_io.hasES8311(), WiFi.localIP().toString().c_str());
}

void loop() {
    audio.loop();
    ArduinoOTA.handle();

    unsigned long now = millis();
    if (now - t10ms >= 10) { t10ms = now; pollXL9555Keys(); pollBootButton(); }
    if (now - t1s >= 1000) { t1s = now; showTime(); }
}

// ---------- audioI2S 回调 ----------
void audio_info(const char *info) { Serial.println(info); }
void audio_eof_mp3(const char *info) { playNext(1); }   // 播放结束自动下一台
