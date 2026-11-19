#pragma once
// ============================================================
// 网络收音机 UI —— 双主题
//   主题0: 圆环仪表盘 (深蓝黑 + 青/紫)
//   主题1: FM 刻度盘 (青色大频率 + 横向频谱 + 游标, 仿用户提供的图片)
//
// 动画由状态变化触发: 切台 -> 频率滚动/游标移动; 调音量 -> 音量条。
// 静止时零重绘。主题由长按 BOOT 2 秒切换, 存 NVS。
// 频率是装饰数字(网络电台无真实频率概念), 电台名是真实的。
// ============================================================
#include <Arduino.h>
#include <LovyanGFX.hpp>

extern LGFX tft;

// 颜色 (RGB565, 用 color565 生成, r/g/b 范围 0~255)
#define UI_BG        lgfx::color565(10, 14, 26)
#define UI_PANEL     lgfx::color565(16, 24, 48)
#define UI_CYAN      lgfx::color565(0, 230, 255)
#define UI_CYAN_DIM  lgfx::color565(0, 120, 140)
#define UI_BLUE      lgfx::color565(30, 100, 210)
#define UI_PURPLE    lgfx::color565(150, 90, 255)
#define UI_TEXT      lgfx::color565(210, 220, 230)
#define UI_TEXT_DIM  lgfx::color565(110, 125, 140)
#define UI_WHITE     0xFFFF

// 带缓动的动画量
class UiAnim {
public:
    float value = 0, from = 0, target = 0;
    unsigned long t0 = 0;
    uint16_t duration = 0;
    bool animating = false;

    void jump(float v) { value = from = target = v; animating = false; }
    void to_(float v, uint16_t ms) {
        if (fabsf(v - value) < 0.001f) return;
        from = value; target = v; duration = ms; t0 = millis(); animating = true;
    }
    void tick() {
        if (!animating) return;
        unsigned long e = millis() - t0;
        if (e >= duration) { value = target; animating = false; return; }
        float p = (float)e / duration;
        p = 1 - powf(1 - p, 3);
        value = from + (target - from) * p;
    }
};

class RadioUI {
public:
    LGFX_Sprite cv{&tft};
    int W = 320, H = 240;
    bool ready = false;
    int theme = 0;                 // 0=仪表盘, 1=FM刻度盘

    // 状态
    int stationIndex = 0;
    int stationTotal = 20;
    String stationName = "";
    int volume = 15, volumeMax = 21;
    bool playing = false;
    int rssi = -60;

    // 动画量
    UiAnim ring;                   // 主题0: 圆环角度 / 主题1: 游标比例(0..1)
    UiAnim freq;                   // 装饰频率 (MHz*10)
    UiAnim volBar;                 // 音量比例 0..1

    unsigned long lastRedraw = 0;
    static const uint16_t ANIM_MS = 650;

    void begin(int w, int h, int th = 0) {
        W = w; H = h; theme = th;
        cv.setColorDepth(16);
        cv.createSprite(W, H);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextDatum(textdatum_t::top_center);
        ready = true;
    }

    void setTheme(int th) {
        theme = th;
        if (theme < 0) theme = 0;
        if (theme > 1) theme = 1;
        // 切换主题时, ring 的含义变了(角度 vs 比例), 重置到当前台对应值, 避免游标跳到边界
        ring.jump(theme == 0 ? ringAngleFor(stationIndex, stationTotal)
                             : ringRatioFor(stationIndex, stationTotal));
        redraw();
    }

    static float fakeFreq(int idx) { return 87.5f + (idx % 41) * 0.5f; }
    float ringAngleFor(int idx, int total) {
        return total > 0 ? 360.0f * (idx % total) / total : 0;
    }
    float ringRatioFor(int idx, int total) {
        return total > 0 ? (float)(idx % total) / total : 0;
    }

    // ---- 状态更新入口 ----
    void setStation(int idx, int total, const String& name, bool instant = false) {
        stationIndex = idx; stationTotal = total; stationName = name;
        float fr = fakeFreq(idx) * 10.0f;
        float prog = (theme == 0) ? ringAngleFor(idx, total) : ringRatioFor(idx, total);
        if (instant || !ready) { ring.jump(prog); freq.jump(fr); }
        else { ring.to_(prog, ANIM_MS); freq.to_(fr, ANIM_MS); }
        if (ready) redraw();
    }

    void setVolume(int v, int maxV, bool instant = false) {
        volume = v; volumeMax = maxV;
        float ratio = maxV > 0 ? (float)v / maxV : 0;
        if (instant || !ready) volBar.jump(ratio);
        else volBar.to_(ratio, 300);
        if (ready) redraw();
    }

    void setPlaying(bool p) { playing = p; if (ready) redraw(); }
    void setRssi(int r)     { rssi = r; }

    void tick() {
        if (!ready) return;
        bool was = ring.animating || freq.animating || volBar.animating;
        ring.tick(); freq.tick(); volBar.tick();
        bool now = ring.animating || freq.animating || volBar.animating;
        if (was || now) redraw();
    }

    void redraw() {
        if (!ready) return;
        cv.fillScreen(UI_BG);
        if (theme == 0) drawTheme0();
        else            drawTheme1();
        cv.pushSprite(0, 0);
        lastRedraw = millis();
    }

private:
    // ================= 主题 0: 圆环仪表盘 =================
    void drawTheme0() {
        drawHeader();
        drawRing();
        drawCenter0();
        drawLeftBar();
        drawRightBarV();
        drawButtons0();
    }

    void drawHeader() {
        struct tm info;
        cv.setTextColor(UI_TEXT, UI_BG);
        cv.setFont(&fonts::efontCN_16);
        if (getLocalTime(&info)) {
            char t[16];
            strftime(t, sizeof(t), "%H:%M", &info);
            cv.setTextDatum(textdatum_t::top_left);
            cv.drawString(t, 10, 4);
            cv.setTextDatum(textdatum_t::top_right);
            sprintf(t, "%02d/%02d", info.tm_mon + 1, info.tm_mday);
            cv.drawString(t, W - 8, 4);
        }
        int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
        for (int i = 0; i < 4; i++) {
            int bx = W - 96 + i * 7, bh = 3 + i * 3;
            cv.fillRect(bx, 18 - bh, 5, bh, i < bars ? UI_CYAN : UI_TEXT_DIM);
        }
        cv.setTextDatum(textdatum_t::top_center);
    }

    void drawRing() {
        int cx = W / 2, cy = H / 2 - 6;
        int rOut = min(W, H) / 2 - 30, rIn = rOut - 9;
        if (rIn < 20) rIn = 20;
        cv.fillArc(cx, cy, rOut, rIn, 0, 360, UI_PANEL);
        int ticks = stationTotal > 0 ? stationTotal : 20;
        for (int i = 0; i < ticks; i++) {
            float a = (-90 + 360.0f * i / ticks) * DEG_TO_RAD;
            cv.drawLine(cx + cosf(a) * (rOut + 3), cy + sinf(a) * (rOut + 3),
                        cx + cosf(a) * (rOut + 7), cy + sinf(a) * (rOut + 7), UI_TEXT_DIM);
        }
        float ang = ring.value; if (ang < 1) ang = 1;
        cv.fillArc(cx, cy, rOut, rIn, -90, -90 + ang, UI_CYAN);
        float a = (-90 + ang) * DEG_TO_RAD;
        int px = cx + cosf(a) * ((rOut + rIn) / 2), py = cy + sinf(a) * ((rOut + rIn) / 2);
        cv.fillCircle(px, py, 6, UI_WHITE);
        cv.drawCircle(px, py, 8, UI_CYAN);
        cv.drawArc(cx, cy, rIn - 6, rIn - 6, -60, 240, UI_BLUE);
    }

    void drawCenter0() {
        int cx = W / 2, cy = H / 2 - 6;
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        char b[24]; snprintf(b, sizeof(b), "%d/%d", stationIndex + 1, stationTotal);
        cv.drawString(b, cx, cy - 48);
        cv.setTextColor(UI_CYAN, UI_BG);
        cv.drawString(stationName.length() ? stationName.c_str() : "网络电台", cx, cy - 30);
        cv.setFont(&fonts::FreeSans24pt7b);
        cv.setTextColor(UI_WHITE, UI_BG);
        snprintf(b, sizeof(b), "%.1f", freq.value / 10.0f);
        cv.drawString(b, cx, cy - 4);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("MHz", cx, cy + 30);
        cv.setTextColor(playing ? UI_CYAN : UI_TEXT_DIM, UI_BG);
        cv.drawString(playing ? "播放中" : "已暂停", cx, cy + 48);
    }

    void drawLeftBar() {
        int x = 8, y0 = H / 2 - 40, h = 80;
        cv.setTextDatum(textdatum_t::top_center);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("信号", x + 10, y0 - 18);
        cv.drawRoundRect(x, y0, 20, h, 6, UI_TEXT_DIM);
        int bars = rssi > -55 ? 5 : rssi > -65 ? 4 : rssi > -75 ? 3 : rssi > -85 ? 2 : 1;
        int fill = h * bars / 5;
        cv.fillRoundRect(x + 2, y0 + h - fill - 2, 16, fill, 5, UI_BLUE);
    }

    void drawRightBarV() {
        int x = W - 28, y0 = H / 2 - 40, h = 80;
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("音量", x + 10, y0 - 18);
        cv.drawRoundRect(x, y0, 20, h, 6, UI_TEXT_DIM);
        int fill = (int)(h * volBar.value); if (fill > h) fill = h;
        cv.fillRoundRect(x + 2, y0 + h - fill - 2, 16, fill, 5, UI_PURPLE);
        cv.setTextColor(UI_TEXT, UI_BG);
        char b[8]; snprintf(b, sizeof(b), "%d", volume);
        cv.drawString(b, x + 10, y0 + h + 4);
    }

    void drawButtons0() {
        const char* labels[6] = {"FM", "播放", "暂停", "上一个", "下一个", "主题"};
        int n = 6, gap = 4, bw = (W - gap * (n + 1)) / n, bh = 24, by = H - bh - 8;
        cv.setFont(&fonts::efontCN_16);
        for (int i = 0; i < n; i++) {
            int bx = gap + i * (bw + gap);
            bool active = (i == 0) || (i == 1 && playing) || (i == 2 && !playing);
            if (active) {
                cv.fillRoundRect(bx, by, bw, bh, 5, UI_BLUE);
                cv.drawRoundRect(bx, by, bw, bh, 5, UI_CYAN);
                cv.setTextColor(UI_WHITE, UI_BLUE);
            } else {
                cv.drawRoundRect(bx, by, bw, bh, 5, UI_TEXT_DIM);
                cv.setTextColor(UI_TEXT_DIM, UI_BG);
            }
            cv.drawString(labels[i], bx + bw / 2, by + 4);
        }
    }

    // ================= 主题 1: FM 刻度盘 (仿用户图片) =================
    void drawTheme1() {
        // 顶部: 电台名 (青色)
        cv.setTextDatum(textdatum_t::top_center);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_CYAN, UI_BG);
        cv.drawString(stationName.length() ? stationName.c_str() : "城市音乐广播", W / 2, 6);
        // 分隔线 + 小字
        cv.drawLine(10, 26, W - 10, 26, UI_CYAN_DIM);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("FM  ·  ST  ·  RDS", W / 2, 28);

        // 大频率数字 (青色, 滚动)
        cv.setFont(&fonts::FreeSans24pt7b);
        cv.setTextColor(UI_CYAN, UI_BG);
        char fbuf[16]; snprintf(fbuf, sizeof(fbuf), "%.2f", freq.value / 10.0f);
        cv.drawString(fbuf, W / 2 - 18, 46);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_CYAN, UI_BG);
        cv.drawString("MHz", W / 2 + 62, 62);

        // 频谱柱 + 刻度条 (y≈100~140)
        drawSpectrum();
        drawScale();

        // 底部: 音量条 + 按钮提示
        cv.setFont(&fonts::efontCN_16);
        cv.setTextDatum(textdatum_t::top_left);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("VOL", 10, 158);
        // 横向音量条
        int vbX = 42, vbY = 162, vbW = 150, vbH = 8;
        cv.drawRoundRect(vbX, vbY, vbW, vbH, 4, UI_TEXT_DIM);
        int vfill = (int)(vbW * volBar.value);
        cv.fillRoundRect(vbX + 1, vbY + 1, vfill, vbH - 2, 3, UI_CYAN);
        cv.fillCircle(vbX + vfill, vbY + vbH / 2, 5, UI_WHITE);
        char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%d", volume);
        cv.setTextDatum(textdatum_t::top_left);
        cv.drawString(vbuf, vbX + vbW + 8, vbY - 4);

        cv.setTextDatum(textdatum_t::top_center);
        cv.drawLine(10, 182, W - 10, 182, UI_CYAN_DIM);
        const char* labels[4] = {"FM", "AM", "SW", "主题"};
        int gap = 6, bw = (W - gap * 5) / 4, bh = 22, by = 196;
        for (int i = 0; i < 4; i++) {
            int bx = gap + i * (bw + gap);
            bool active = (i == 0);
            if (active) {
                cv.fillRoundRect(bx, by, bw, bh, 5, UI_CYAN_DIM);
                cv.setTextColor(UI_WHITE, UI_CYAN_DIM);
            } else {
                cv.drawRoundRect(bx, by, bw, bh, 5, UI_TEXT_DIM);
                cv.setTextColor(UI_TEXT_DIM, UI_BG);
            }
            cv.drawString(labels[i], bx + bw / 2, by + 3);
        }
        // 播放状态小字
        cv.setTextColor(playing ? UI_CYAN : UI_TEXT_DIM, UI_BG);
        cv.drawString(playing ? "▶ 播放中" : "❚❚ 已暂停", W / 2, 222);
    }

    void drawSpectrum() {
        // 12 根频谱柱, 高度由台号+固定图案决定 (静态, 随切台变化)
        int n = 16, x0 = 14, yBase = 104, maxH = 30;
        int step = (W - 2 * x0) / n;
        for (int i = 0; i < n; i++) {
            // 用台号与 i 生成伪随机高度, 切台时图案变化
            int h = 6 + ((stationIndex * 7 + i * 13) % 25);
            if (h > maxH) h = maxH;
            cv.fillRect(x0 + i * step, yBase - h, step - 3, h, UI_CYAN_DIM);
        }
    }

    void drawScale() {
        // 横向刻度条: 背景槽 + 已填充段 + 游标
        int x0 = 12, x1 = W - 12, y = 118;
        cv.drawRoundRect(x0, y - 3, x1 - x0, 6, 3, UI_TEXT_DIM);
        // 刻度线
        for (int i = 0; i <= 40; i++) {
            int x = x0 + (x1 - x0) * i / 40;
            int h = (i % 5 == 0) ? 8 : 4;
            cv.drawLine(x, y + 6, x, y + 6 + h, UI_TEXT_DIM);
        }
        // 游标位置 (动画)
        float r = ring.value; if (r < 0) r = 0; if (r > 1) r = 1;
        int cur = x0 + (int)((x1 - x0) * r);
        // 已填充段 (青色)
        cv.fillRoundRect(x0, y - 2, cur - x0, 4, 2, UI_CYAN);
        // 游标圆点
        cv.fillCircle(cur, y, 7, UI_CYAN);
        cv.fillCircle(cur, y, 4, UI_WHITE);
        // 频率标签
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.setTextDatum(textdatum_t::top_left);
        cv.drawString("87.5", x0, 132);
        cv.setTextDatum(textdatum_t::top_center);
        cv.drawString("97.0", W / 2, 132);
        cv.setTextDatum(textdatum_t::top_right);
        cv.drawString("108.0", x1, 132);
        cv.setTextDatum(textdatum_t::top_center);
    }
};
