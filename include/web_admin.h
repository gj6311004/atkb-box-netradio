#pragma once
// ============================================================
// 后台管理网页 (手机/电脑浏览器访问盒子 IP 即可)
//
// 功能:
//   - 音量调节 (0~21)
//   - 亮屏时间 (常亮 / 15s / 30s / 1min / 3min / 5min)
//   - 广播源管理: 切换电台、添加自定义电台、删除自定义电台
//
// 自定义电台与设置存 NVS (namespace "admin"), 掉电不丢。
// 依赖 ESP32 自带 WebServer, 无第三方库。
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <vector>

struct RadioItem {
    String name;
    String url;
};

#define MAX_CUSTOM 20                 // 最多自定义电台数
#define ADMIN_NS   "admin"

class WebAdmin {
public:
    std::vector<RadioItem> &radios;
    int &curVolume;
    int &curIndex;
    int &screenTimeout;               // 秒, 0 = 常亮
    int builtinCount;                 // 内置电台数量 (前 N 个不可删)

    // 回调: 由 main 提供
    void (*onPlay)(int) = nullptr;    // 播放第 idx 个电台
    void (*onVolume)(int) = nullptr;  // 设置音量
    void (*onTimeout)(int) = nullptr; // 设置亮屏时间
    void (*onWake)() = nullptr;       // 唤醒屏幕

    WebServer server{80};
    Preferences prefs;
    bool running = false;

    WebAdmin(std::vector<RadioItem> &r, int &vol, int &idx, int &timeout, int builtin)
        : radios(r), curVolume(vol), curIndex(idx), screenTimeout(timeout), builtinCount(builtin) {}

    // 从 NVS 加载自定义电台, 追加到 radios 末尾
    void loadCustomStations() {
        prefs.begin(ADMIN_NS, true);
        int cnt = prefs.getInt("ccnt", 0);
        if (cnt > MAX_CUSTOM) cnt = MAX_CUSTOM;
        for (int i = 0; i < cnt; i++) {
            String keyN = "n" + String(i);
            String keyU = "u" + String(i);
            String name = prefs.getString(keyN.c_str(), "");
            String url  = prefs.getString(keyU.c_str(), "");
            if (name.length() && url.length()) radios.push_back({name, url});
        }
        prefs.end();
        Serial.printf("自定义电台已加载: %d 个\n", cnt);
    }

    void saveCustomStations() {
        prefs.begin(ADMIN_NS, false);
        int customCnt = radios.size() - builtinCount;
        if (customCnt > MAX_CUSTOM) customCnt = MAX_CUSTOM;
        prefs.putInt("ccnt", customCnt);
        for (int i = 0; i < customCnt; i++) {
            String keyN = "n" + String(i);
            String keyU = "u" + String(i);
            prefs.putString(keyN.c_str(), radios[builtinCount + i].name);
            prefs.putString(keyU.c_str(), radios[builtinCount + i].url);
        }
        prefs.end();
    }

    void begin() {
        server.on("/", [this]() { handleRoot(); });
        server.on("/setvol", [this]() { handleSetVol(); });
        server.on("/timeout", [this]() { handleTimeout(); });
        server.on("/play", [this]() { handlePlay(); });
        server.on("/add", [this]() { handleAdd(); });
        server.on("/del", [this]() { handleDel(); });
        server.onNotFound([this]() { handleRoot(); });
        server.begin();
        running = true;
        Serial.println("管理网页已启动: http://" + WiFi.localIP().toString());
    }

    void handle() {
        if (running) server.handleClient();
    }

private:
    void redirectHome() {
        server.sendHeader("Location", "/");
        server.send(303);
    }
    void wake() { if (onWake) onWake(); }

    void handleSetVol() {
        wake();
        if (server.hasArg("v")) {
            int v = server.arg("v").toInt();
            if (v < 0) v = 0;
            if (v > 21) v = 21;
            if (onVolume) onVolume(v);
        }
        redirectHome();
    }

    void handleTimeout() {
        wake();
        if (server.hasArg("s")) {
            int s = server.arg("s").toInt();
            if (s < 0) s = 0;
            if (onTimeout) onTimeout(s);
        }
        redirectHome();
    }

    void handlePlay() {
        wake();
        if (server.hasArg("idx")) {
            int i = server.arg("idx").toInt();
            if (i >= 0 && i < (int)radios.size()) {
                if (onPlay) onPlay(i);
            }
        }
        redirectHome();
    }

    void handleAdd() {
        wake();
        String name = server.arg("n");
        String url  = server.arg("u");
        name.trim(); url.trim();
        if (name.length() && url.length() &&
            (url.startsWith("http://") || url.startsWith("https://"))) {
            if ((int)radios.size() - builtinCount < MAX_CUSTOM) {
                radios.push_back({name, url});
                saveCustomStations();
            }
        }
        redirectHome();
    }

    void handleDel() {
        wake();
        if (server.hasArg("idx")) {
            int i = server.arg("idx").toInt();
            // 只允许删除自定义电台 (索引 >= builtinCount)
            if (i >= builtinCount && i < (int)radios.size()) {
                radios.erase(radios.begin() + i);
                saveCustomStations();
                if (curIndex >= (int)radios.size()) curIndex = radios.size() - 1;
            }
        }
        redirectHome();
    }

    String timeoutOptions(int cur) {
        struct TO { int v; const char *label; };
        static const TO opts[] = {
            {0, "常亮"}, {15, "15 秒"}, {30, "30 秒"},
            {60, "1 分钟"}, {180, "3 分钟"}, {300, "5 分钟"},
        };
        String s;
        for (auto &o : opts) {
            s += "<option value='" + String(o.v) + "'";
            if (o.v == cur) s += " selected";
            s += ">" + String(o.label) + "</option>";
        }
        return s;
    }

    void handleRoot() {
        wake();
        String html;
        html.reserve(6000);
        html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>ATK BOX 收音机</title><style>"
          "body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:16px;"
          "background:#f5f5f5;color:#222;border-radius:12px}"
          "h2{color:#0a7;margin-bottom:4px} h3{margin-top:22px;color:#444;font-size:16px}"
          ".card{background:#fff;padding:14px;margin:10px 0;border-radius:10px}"
          "input[type=range]{width:100%} select,input[type=text]{width:100%;padding:10px;"
          "margin:6px 0;border:1px solid #ccc;border-radius:8px;box-sizing:border-box;font-size:15px}"
          "button{padding:10px 16px;background:#07c160;color:#fff;border:none;"
          "border-radius:8px;font-size:15px;cursor:pointer}"
          "a.st{display:block;padding:10px;margin:4px 0;background:#eef;font-size:15px;"
          "text-decoration:none;color:#222;border-radius:8px}"
          "a.st.cur{background:#07c160;color:#fff}"
          "a.del{float:right;color:#c00;text-decoration:none;font-size:13px}"
          ".small{color:#888;font-size:12px}"
          "</style></head><body>"
          "<h2>ATK BOX 网络收音机</h2>"
          "<div class='small'>IP: ");
        html += WiFi.localIP().toString();
        html += F("</div>");

        // 音量
        html += F("<div class='card'><h3>音量</h3>"
          "<input type='range' min='0' max='21' value='");
        html += String(curVolume);
        html += F("' onchange=\"location='/setvol?v='+this.value\">"
          "<div class='small'>当前: ");
        html += String(curVolume);
        html += F(" / 21</div></div>");

        // 亮屏时间
        html += F("<div class='card'><h3>亮屏时间</h3>"
          "<select onchange=\"location='/timeout?s='+this.value\">");
        html += timeoutOptions(screenTimeout);
        html += F("</select><div class='small'>无操作后自动关背光, 按任意键唤醒</div></div>");

        // 电台列表
        html += F("<div class='card'><h3>广播源</h3>");
        for (int i = 0; i < (int)radios.size(); i++) {
            html += "<a class='st";
            if (i == curIndex) html += " cur";
            html += "' href='/play?idx=" + String(i) + "'>";
            html += String(i + 1) + ". " + radios[i].name;
            html += "</a>";
            if (i >= builtinCount) {
                html += "<a class='del' href='/del?idx=" + String(i) + "'>删除</a>";
            }
        }
        html += F("</div>");

        // 添加自定义电台
        html += F("<div class='card'><h3>添加自定义电台</h3>"
          "<form action='/add' method='get'>"
          "<input type='text' name='n' placeholder='电台名称' required>"
          "<input type='text' name='u' placeholder='流地址 http://... 或 https://...' required>"
          "<button type='submit'>添加</button></form>"
          "<div class='small'>支持 mp3 / m3u8 直链; 最多 ")
        html += String(MAX_CUSTOM);
        html += F(" 个自定义电台</div></div>");

        html += F("</body></html>");
        server.send(200, "text/html; charset=utf-8", html);
    }
};
