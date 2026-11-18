// ============================================================
// 正点原子 ATK ESP32S3 BOX 网络收音机
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
#include "wifi_portal.h"      // 轻量 SoftAP 网页配网
#include "web_admin.h"        // 后台管理网页
#include "radio_ui.h"         // 动态仪表盘显示层

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Audio.h>
#include <WiFi.h>
#include <Preferences.h>

// ---------- 板级对象 ----------
XL9555 atk_io;
ES8311 atk_codec;

// ---------- 显示 ----------
#define FONT16 &fonts::efontCN_16
LGFX tft;                       // radio_ui.h 里 extern
LGFX_Sprite sp(&tft);           // splash / 方向设置 等简单界面仍用
RadioUI ui;                     // 主界面动态仪表盘
#define CX (tft.width() / 2)
#define CY (tft.height() / 2)
#define SW tft.width()
#define SH tft.height()

// ---------- 音频 ----------
Audio audio;
int curVolume = 15;                // 0~21
const char *WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

// ---------- 电台 (name + url; RadioItem 定义在 web_admin.h) ----------
#define FM_URL "http://lhttp.qtfm.cn/live/%lu/64k.mp3"
static String fmUrl(u32_t id) {
    char u[96];
    sprintf(u, FM_URL, id);
    return String(u);
}

// 内置电台 (前 BUILTIN_COUNT 个, 网页不可删除)
static const int BUILTIN_COUNT = 20;
static std::vector<RadioItem> radios = {
    {"清晨音乐台",   fmUrl(4915)},
    {"怀旧好声音",   fmUrl(1223)},
    {"浙江音乐调频", fmUrl(4866)},
    {"厦门音乐广播", fmUrl(1739)},
    {"深圳飞扬971",  fmUrl(1271)},
    {"山东经典音乐", fmUrl(20240)},
    {"湖北经典音乐", fmUrl(1296)},
    {"上海经典947",  fmUrl(267)},
    {"天津TIKI FM",  fmUrl(20003)},
    {"四川城市之音", fmUrl(1111)},
    {"长沙城市之声", fmUrl(4237)},
    {"北京音乐广播", fmUrl(332)},
    {"上海动感101",  fmUrl(274)},
    {"哈尔滨音乐台", fmUrl(839)},
    {"江门旅游之声", fmUrl(1283)},
    {"重庆音乐广播", fmUrl(647)},
    {"郑州音乐广播", fmUrl(4921)},
    {"唐山音乐广播", fmUrl(4871)},
    {"Nice FM",      fmUrl(20033)},
    {"893音乐广播",  fmUrl(4846)},
};
int curIndex = 0;

// ---------- 设置 / 状态 ----------
Preferences prefs;
WiFiPortal portal("ATK-BOX-Setup");
int screenRotation = ATK_DEFAULT_ROTATION;
bool configMode = false;           // 方向设置模式

int screenTimeout = 60;            // 亮屏秒数, 0=常亮 (从 NVS 载入)
unsigned long lastActivity = 0;    // 最后一次操作时间
bool screenAwake = true;           // 背光是否亮

WebAdmin admin(radios, curVolume, curIndex, screenTimeout, BUILTIN_COUNT);

long t1s = 0, t10ms = 0;
char buf[128] = {0};

// 唤醒屏幕 (按键/网页操作调用)
void wakeScreen() {
    lastActivity = millis();
    bool wasAsleep = !screenAwake;
    if (wasAsleep) {
        atk_io.backlight(true);
        screenAwake = true;
    }
    // 关键修复: 唤醒后立刻重绘一次, 否则熄屏期间没画, 亮屏后是旧残影
    if (wasAsleep && !configMode && ui.ready) {
        ui.redraw();
    }
}

// ============================================================
// 显示辅助
//   主界面由 RadioUI (ui) 负责; splash / 方向设置用 sp 简单绘制
// ============================================================
// 把当前状态同步到仪表盘 (供状态变化时调用)
void refreshUI() {
    if (!ui.ready) return;
    ui.setPlaying(audio.isRunning());
    ui.setRssi(WiFi.RSSI());
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
    if (radios.empty()) return;
    int total = radios.size();
    curIndex += offset;
    if (curIndex >= total) curIndex %= total;
    if (curIndex < 0) curIndex += total;

    audio.connecttohost(radios[curIndex].url.c_str());
    ui.setStation(curIndex, radios.size(), radios[curIndex].name);
    ui.setPlaying(true);
}

// 播放指定电台 (网页调用)
void playIndex(int idx) {
    if (idx < 0 || idx >= (int)radios.size()) return;
    curIndex = idx;
    audio.connecttohost(radios[curIndex].url.c_str());
    ui.setStation(curIndex, radios.size(), radios[curIndex].name);
    ui.setPlaying(true);
}

void setVolAbsolute(int v) {
    if (v < 0) v = 0;
    if (v > 21) v = 21;
    curVolume = v;
    audio.setVolume(curVolume);
    prefs.begin(ADMIN_NS, false);
    prefs.putInt("volume", curVolume);
    prefs.end();
    ui.setVolume(curVolume, 21);
}

void setVol(int offset) {
    setVolAbsolute(curVolume + offset);
}

void setScreenTimeout(int sec) {
    if (sec < 0) sec = 0;
    screenTimeout = sec;
    prefs.begin(ADMIN_NS, false);
    prefs.putInt("timeout", screenTimeout);
    prefs.end();
}

// ============================================================
// 按键
//   BOOT(K0): 短按 播放/暂停; 长按3秒 进入/保存方向设置
//   方向模式内: 短按 切换方向; 长按3秒 保存并重启
//   K1 短按下一台 / 长按音量+; K2 短按上一台 / 长按音量- (低电平按下)
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
    static unsigned long k1Press = 0, k2Press = 0;
    static bool k1Long = false, k2Long = false;
    static unsigned long k1Repeat = 0, k2Repeat = 0;
    bool k1 = atk_io.readInput(XL_P_KEY1);
    bool k2 = atk_io.readInput(XL_P_KEY2);
    unsigned long now = millis();

    // ---- K1 ----
    if (k1 != k1Last) {
        if (!k1) {                           // 按下
            k1Press = now; k1Long = false; k1Repeat = now;
            wakeScreen();
        } else {                             // 松开
            if (!k1Long && (now - k1Press) < 1200) playNext(1);  // 短按: 下一台
        }
        k1Last = k1;
    }
    // K1 长按: 音量+, 每 250ms 加一级
    if (!k1 && k1Press && (now - k1Press) >= 1000) {
        k1Long = true;
        if (now - k1Repeat >= 250) { k1Repeat = now; setVol(1); }
    }

    // ---- K2 ----
    if (k2 != k2Last) {
        if (!k2) {
            k2Press = now; k2Long = false; k2Repeat = now;
            wakeScreen();
        } else {
            if (!k2Long && (now - k2Press) < 1200) playNext(-1); // 短按: 上一台
        }
        k2Last = k2;
    }
    // K2 长按: 音量-
    if (!k2 && k2Press && (now - k2Press) >= 1000) {
        k2Long = true;
        if (now - k2Repeat >= 250) { k2Repeat = now; setVol(-1); }
    }
}

void pollBootButton() {
    static bool last = true;
    static unsigned long pressStart = 0;
    static bool longFired = false;
    bool cur = digitalRead(0);              // 按下=LOW
    unsigned long now = millis();

    // 按住期间持续检测长按 (不依赖松开沿)
    if (!cur && pressStart && !longFired && (now - pressStart) >= 2000) {
        longFired = true;
        wakeScreen();
        if (!configMode) {
            configMode = true;
            showRotationScreen();
            Serial.printf("[ BTN] long press -> config mode ON, rotation=%d\n", screenRotation);
        } else {
            // 方向模式下长按: 保存并重启
            prefs.begin("netradio", false);
            prefs.putInt("rotation", screenRotation);
            prefs.end();
            showSplash("方向已保存", "正在重启...");
            delay(800);
            ESP.restart();
        }
    }

    if (cur != last) {
        if (!cur) {                          // 按下沿
            pressStart = now;
            longFired = false;
            wakeScreen();
        } else {                             // 松开沿
            unsigned long held = now - pressStart;
            if (!longFired && held < 2000) {
                // 短按
                if (configMode) {
                    screenRotation = (screenRotation + 1) % 8;
                    applyRotation();
                    showRotationScreen();
                    Serial.printf("[ BTN] short press -> rotation=%d\n", screenRotation);
                } else {
                    audio.pauseResume();
                    ui.setPlaying(audio.isRunning());
                }
            }
            pressStart = 0;
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

    // 读取音量 / 亮屏时间 (namespace "admin")
    prefs.begin(ADMIN_NS, true);
    curVolume     = prefs.getInt("volume", curVolume);
    screenTimeout = prefs.getInt("timeout", screenTimeout);
    prefs.end();
    if (curVolume < 0 || curVolume > 21) curVolume = 15;
    if (screenTimeout < 0) screenTimeout = 0;
    audio.setVolume(curVolume);

    // 载入自定义电台, 接在内置电台后面
    admin.loadCustomStations();

    // 后台网页回调
    admin.onPlay    = playIndex;
    admin.onVolume  = setVolAbsolute;
    admin.onTimeout = setScreenTimeout;
    admin.onWake    = wakeScreen;

    // 初始化仪表盘 UI (此时电台列表已完整)
    ui.begin(SW, SH);
    ui.setStation(0, radios.size(), radios.size() ? radios[0].name : "网络电台", true);
    ui.setVolume(curVolume, 21, true);
    ui.setPlaying(false);

    setupBootButton();
    lastActivity = millis();
}

void connectWiFi() {
    // 1) 尝试连接已存 WiFi
    String ssid, pass;
    if (portal.load(ssid, pass) && portal.tryConnect(ssid, pass)) {
        return;                            // 连上了
    }

    // 2) 连不上 -> 开热点, 网页配网
    showSplash("配网模式", "连接热点 ATK-BOX-Setup", "然后打开 192.168.4.1");
    portal.startConfigPortal();            // 阻塞直到用户提交

    // 3) 用户提交后重启, 用新账号连接
    showSplash("已保存 WiFi", "正在重启...");
    delay(800);
    ESP.restart();
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
    Serial.println("ATK ESP32S3 BOX NetRadio boot");

    initHardware();
    showSplash("ATK BOX 网络收音机", "正在启动...");

    connectWiFi();
    configTime(8 * 3600, 0, "ntp6.aliyun.com", "cn.ntp.org.cn", "ntp.ntsc.ac.cn");

    setupOTA();
    admin.begin();                         // 启动后台管理网页

    // 显示后台管理地址 2 秒, 然后进入仪表盘
    showSplash("后台管理地址", WiFi.localIP().toString().c_str());
    delay(2000);

    playNext(0);                           // 触发仪表盘首次绘制
    Serial.printf("WiFi=%s  ES8311=%d  IP=%s  rotation=%d\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                  atk_io.hasES8311(), WiFi.localIP().toString().c_str(),
                  screenRotation);
}

void loop() {
    audio.loop();
    ArduinoOTA.handle();
    admin.handle();                        // 处理后台网页请求

    unsigned long now = millis();
    if (now - t10ms >= 10) { t10ms = now; pollXL9555Keys(); pollBootButton(); }

    // 主界面: 推进动画 + 每秒更新时间/信号 (方向设置模式与熄屏时不重绘)
    if (!configMode && screenAwake) {
        bool wasAnimating = ui.ring.animating || ui.freq.animating || ui.volBar.animating;
        ui.tick();
        // 动画期间持续刷新活动时间, 避免动画一停就被判为超时黑屏
        if (wasAnimating) lastActivity = now;
        if (now - t1s >= 1000) {
            t1s = now;
            ui.setRssi(WiFi.RSSI());
            ui.setPlaying(audio.isRunning());
            ui.redraw();                   // 刷新时间显示
        }
    }

    // 亮屏时间到 -> 关背光 (熄屏但继续播放)
    if (screenAwake && screenTimeout > 0 && !configMode &&
        (now - lastActivity > (unsigned long)screenTimeout * 1000)) {
        atk_io.backlight(false);
        screenAwake = false;
        Serial.println("[LCD] backlight OFF (timeout)");
    }
}

// ---------- audioI2S 回调 ----------
void audio_info(const char *info) { Serial.println(info); }
void audio_eof_mp3(const char *info) { playNext(1); }   // 播放结束自动下一台
