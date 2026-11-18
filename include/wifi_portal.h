#pragma once
// ============================================================
// 轻量 SoftAP 网页配网 (不依赖 WiFiManager, 避免 byte 类型冲突)
//
// 流程:
//   1. 从 NVS 读取已存 WiFi, 尝试连接 (最多 12 秒)
//   2. 连不上 -> 开热点 "ATK-BOX-Setup", 启动 captive portal
//   3. 手机/电脑连该热点, 浏览器打开 192.168.4.1 (多数会自动弹出)
//   4. 网页里选 WiFi、填密码 -> 保存到 NVS -> 自动重启连接
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

class WiFiPortal {
public:
    static const byte DNS_PORT = 53;
    IPAddress apIP{192, 168, 4, 1};
    DNSServer dnsServer;
    WebServer server{80};
    Preferences prefs;
    const char *apName;
    String newSsid, newPass;
    bool gotConfig = false;

    WiFiPortal(const char *name) : apName(name) {}

    // 从 NVS 读账号密码
    bool load(String &ssid, String &pass) {
        prefs.begin("netradio", true);
        ssid = prefs.getString("ssid", "");
        pass = prefs.getString("pass", "");
        prefs.end();
        return ssid.length() > 0;
    }

    void save(const String &ssid, const String &pass) {
        prefs.begin("netradio", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
    }

    // 尝试连接已存 WiFi, 成功返回 true
    bool tryConnect(const String &ssid, const String &pass) {
        if (!ssid.length()) return false;
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
            delay(200);
        }
        return WiFi.status() == WL_CONNECTED;
    }

    // 开启配网页 (阻塞, 直到用户提交)
    void startConfigPortal() {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP(apName);                 // 无密码热点
        delay(200);

        // 所有域名都指向我们的 IP, 实现 captive portal 自动弹出
        dnsServer.start(DNS_PORT, "*", apIP);

        server.on("/", [this]() { handleRoot(); });
        server.on("/save", [this]() { handleSave(); });
        server.onNotFound([this]() { handleRoot(); });
        server.begin();

        while (!gotConfig) {
            dnsServer.processNextRequest();
            server.handleClient();
            delay(10);
        }
        server.stop();
        dnsServer.stop();
    }

private:
    void handleRoot() {
        // 扫描附近 WiFi 生成下拉列表
        int n = WiFi.scanNetworks(false, true);
        String options;
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            ssid.replace("<", "&lt;");
            options += "<option value='" + ssid + "'>" + ssid + "</option>";
        }
        String html =
          "<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>ATK BOX 配网</title>"
          "<style>body{font-family:sans-serif;max-width:420px;margin:40px auto;padding:20px;"
          "background:#f5f5f5;border-radius:12px}h2{color:#333}input,select,button{width:100%;"
          "padding:12px;margin:8px 0;border-radius:8px;border:1px solid #ccc;font-size:16px}"
          "button{background:#07c160;color:#fff;border:none}</style></head><body>"
          "<h2>ATK BOX 网络收音机配网</h2>"
          "<p>请选择家里的 WiFi(2.4GHz)并输入密码:</p>"
          "<form action='/save' method='GET'>"
          "<select name='ssid'>" + options + "</select>"
          "<input name='ssid2' placeholder='或手动输入 WiFi 名称'>"
          "<input name='pass' type='password' placeholder='WiFi 密码'>"
          "<button type='submit'>连接</button></form>"
          "<p style='color:#888;font-size:13px'>保存后盒子会自动重启并连接。</p>"
          "</body></html>";
        server.send(200, "text/html; charset=utf-8", html);
    }

    void handleSave() {
        String ssid = server.arg("ssid2");
        if (ssid.length() == 0) ssid = server.arg("ssid");   // 用下拉选的
        String pass = server.arg("pass");

        String html =
          "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
          "text-align:center;margin-top:60px'><h2>已保存 ✓</h2>"
          "<p>正在重启并连接 " + ssid + ", 请等待约 10 秒。</p></body></html>";
        server.send(200, "text/html; charset=utf-8", html);
        delay(500);

        if (ssid.length()) {
            save(ssid, pass);
            newSsid = ssid;
            newPass = pass;
            gotConfig = true;
        }
    }
};
