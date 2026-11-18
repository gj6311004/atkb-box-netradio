// ============================================================
// 正点原子 ATK-DNESP32S3-BOX V1.1 网络收音机
// 基于 zhuhai-esp/ESP32-S3-AmourK08-4Keys :: K08-4Keys-NetRadio 修改
//
// 硬件适配:
//   屏幕 ST7789 320x240 i8080 并口  -> include/lgfx.h
//   音频 ES8311 (I2C)              -> include/atk_hardware.h
//   IO   XL9555 (功放/背光/按键)    -> include/atk_hardware.h
//   配网 WiFiManager (SoftAP 网页)  -> 无需 APP
// ============================================================
#include "lgfx.h"
#include "atk_hardware.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Audio.h>
#include <WiFi.h>
#include <WiFiManager.h>       // SoftAP 网页配网
#include <Preferences.h>
#include <vector>

// ---------- 板级对象 ----------
XL9555 atk_io;
ES8311 atk_codec;

// ---------- 显示 (尺寸运行时从 tft 读取, 横竖屏通用) ----------
#define FONT16 &fonts::efontCN_16
LGFX tft;
LGFX_Sprite sp(&tft);
#define CX (tft.width() / 2)
#define CY (tft.height() / 2)
#define SW tft.width()
#define SH tft.height()

// ---------- 音频 ----------
Audio audio;
int curVolume = 15;                // 0~21
const char *WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

// ---------- 电台 ----------
#define FM_URL "http://lhttp.qtfm.cn/live/%u/64k.mp3"
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

// ---------- 设置 / 状态 ----------
Preferences prefs;
WiFiManager wm;
int screenRotation = ATK_DEFAULT_ROTATION;
bool configMode = false;           // 方向设置模式

long t1s = 0, t10ms = 0;
char buf[128] = {0};

// ============================================================
// 显示辅助 (全部用动态宽高)
// ============================================================
void showStation() {
    sprintf(buf, "%d.%s", curIndex + 1, radios[curIndex].name.c_str());
    int w = SW;
    sp.createSprite(w, 20);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawCentreString(buf, w / 2, 0);
    sp.pushSprite(0, 20);
    sp.deleteSprite();
}

void showVolume() {
    int w = 140;
    sp.createSprite(w, 20);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprintf(buf, "音量: %d", curVolume);
    sp.drawString(buf, 6, 0);
    sp.pushSprite(4, SH - 24);
    sp.deleteSprite();
}

void showIP() {
    int w = 170;
    sp.createSprite(w, 20);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawRightString(WiFi.localIP().toString().c_str(), w - 6, 0);
    sp.pushSprite(SW - w - 4, SH - 24);
    sp.deleteSprite();
}

void showTime() {
    struct tm info;
    if (!getLocalTime(&info)) return;
    sprintf(buf, "%d年%d月%d日 星期%s", 1900 + info.tm_year, info.tm_mon + 1,
            info.tm_mday, WEEK_DAYS[info.tm_wday]);
    sp.createSprite(SW, 20);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawCentreString(buf, CX, 0);
    sp.pushSprite(0, SH / 2 - 30);
    sp.deleteSprite();

    strftime(buf, 36, "%T", &info);
    sp.createSprite(SW, 40);
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.drawCentreString(buf, CX, 0, &fonts::FreeSans24pt7b);
    sp.pushSprite(0, SH / 2);
    sp.deleteSprite();
}

void showSplash(const char *line1, const char *line2 = nullptr,
                const char *line3 = nullptr) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(line1, CX, CY - 30, FONT16);
    if (line2) tft.drawCentreString(line2, CX, CY, FONT16);
    if (line3) tft.drawCentreString(line3, CX, CY + 30, FONT16);
}

// 方向设置模式界面: 大字显示当前旋转值
void showRotationScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("屏幕方向设置", CX, 20, FONT16);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprintf(buf, "方向 %d / 7", screenRotation);
    tft.drawCentreString(buf, CX, CY - 20, FONT16);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("短按:切换  长按3秒:保存", CX, SH - 40, FONT16);
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
// 按键
//   BOOT(K0): 短按 播放/暂停; 长按3秒 进入/保存方向设置
//   方向模式内: 短按 切换方向; 长按3秒 保存并重启
//   K1 下一台, K2 上一台 (XL9555, 低电平按下)
// ============================================================
void setupBootButton() {
    pinMode(0, INPUT_PULLUP);
}

void applyRotation() {
    tft.setRotation(screenRotation);
    tft.fillScreen(TFT_BLACK);
}

void pollXL9555Keys() {
    if (configMode) return;                 // 方向模式下忽略 K1/K2
    static bool k1Last = true, k2Last = true;
    static unsigned long k1Time = 0, k2Time = 0;
    bool k1 = atk_io.readInput(XL_P_KEY1);
    bool k2 = atk_io.readInput(XL_P_KEY2);
    unsigned long now = millis();

    if (k1 != k1Last && (now - k1Time) > 200) {
        k1Time = now;
        if (!k1) playNext(1);
        k1Last = k1;
    }
    if (k2 != k2Last && (now - k2Time) > 200) {
        k2Time = now;
        if (!k2) playNext(-1);
        k2Last = k2;
    }
}

void pollBootButton() {
    static bool last = true;
    static unsigned long pressStart = 0;
    static bool longFired = false;
    bool cur = digitalRead(0);
    unsigned long now = millis();

    if (cur != last) {
        if (!cur) {                          // 按下
            pressStart = now;
            longFired = false;
        } else {                             // 松开
            unsigned long held = now - pressStart;
            if (!longFired && held < 3000) {
                // 短按
                if (configMode) {
                    screenRotation = (screenRotation + 1) % 8;
                    applyRotation();
                    showRotationScreen();
                } else {
                    audio.pauseResume();
                }
            }
            pressStart = 0;
        }
        last = cur;
    }

    // 长按 3 秒触发一次
    if (!cur && pressStart && !longFired && (now - pressStart) >= 3000) {
        longFired = true;
        if (!configMode) {
            configMode = true;
            showRotationScreen();
        } else {
            // 保存方向并重启
            prefs.begin("netradio", false);
            prefs.putInt("rotation", screenRotation);
            prefs.end();
            showSplash("方向已保存", "正在重启...");
            delay(800);
            ESP.restart();
        }
    }
}

// ============================================================
// 初始化
// ============================================================
void initHardware() {
    Wire.begin(ATK_I2C_SDA, ATK_I2C_SCL);
    atk_io.begin();                       // 功放 + 背光打开, 探测音频芯片

    // 读取保存的屏幕方向
    prefs.begin("netradio", true);
    screenRotation = prefs.getInt("rotation", ATK_DEFAULT_ROTATION);
    prefs.end();
    if (screenRotation < 0 || screenRotation > 7) screenRotation = ATK_DEFAULT_ROTATION;

    tft.init();
    tft.setRotation(screenRotation);
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
    // WiFiManager: 连不上已存 WiFi 时自动开热点 "ATK-BOX-Setup",
    // 手机/电脑连上后浏览器填家里 WiFi 密码即可 (无需 APP)。
    wm.setConnectTimeout(15);
    wm.setConfigPortalTimeout(300);       // 配网页面最多停留 5 分钟

    bool res = wm.autoConnect("ATK-BOX-Setup");   // 无密码热点
    if (!res) {
        // 配网超时/失败, 重启再试
        showSplash("配网超时", "重启后重试");
        delay(2000);
        ESP.restart();
    }
    // 成功 (首次会自动把账号密码存进 NVS, 下次直接连)
}

void setupOTA() {
    ArduinoOTA.onStart([] {
        audio.stopSong();
        showSplash("软件升级", "请勿断电...");
    });
    ArduinoOTA.onProgress([](u32_t pro, u32_t total) {
        sprintf(buf, "升级进度: %u / %u", pro, total);
        sp.createSprite(SW, 20);
        sp.drawCentreString(buf, CX, 0);
        sp.pushSprite(0, SH / 2);
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
    Serial.printf("WiFi=%s  ES8311=%d  IP=%s  rotation=%d\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                  atk_io.hasES8311(), WiFi.localIP().toString().c_str(),
                  screenRotation);
}

void loop() {
    audio.loop();
    ArduinoOTA.handle();

    unsigned long now = millis();
    if (now - t10ms >= 10) { t10ms = now; pollXL9555Keys(); pollBootButton(); }
    if (now - t1s >= 1000 && !configMode) { t1s = now; showTime(); }
}

// ---------- audioI2S 回调 ----------
void audio_info(const char *info) { Serial.println(info); }
void audio_eof_mp3(const char *info) { playNext(1); }   // 播放结束自动下一台
