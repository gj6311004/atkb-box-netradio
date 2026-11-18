#pragma once
// ============================================================
// 网络收音机动态仪表盘 UI
//
// 设计原则:
//   - 动画由"状态变化"触发: 切台 -> 圆环进度前进 + 频率滚动;
//     调音量 -> 音量条升降。静止时零重绘, 不抢音频 CPU。
//   - 全屏 Sprite 缓冲, 无闪烁。
//   - 中间显示"真实电台名"; 频率是装饰数字(网络电台无频率概念),
//     但切换时会滚动变化, 纯视觉。
//   - 底部 6 个按钮是视觉提示; 无触摸屏, 实际操作仍走实体按键。
//       FM        当前模式
//       播放/暂停 BOOT 短按
//       上一个    K2 短按
//       下一个    K1 短按
//       设置      网页后台
// ============================================================
#include <Arduino.h>
#include <LovyanGFX.hpp>

extern LGFX tft;                       // main.cpp 里定义

// ---- 颜色 (RGB565) ----
#define UI_BG        0x080C18
#define UI_PANEL     0x101830
#define UI_CYAN      0x07E7           // 亮青
#define UI_BLUE      0x041F
#define UI_PURPLE    0x8010
#define UI_TEXT      0xC618
#define UI_TEXT_DIM  0x8410
#define UI_WHITE     0xFFFF

// 一个带缓动的动画量
class UiAnim {
public:
    float value = 0, from = 0, target = 0;
    unsigned long t0 = 0;
    uint16_t duration = 0;
    bool animating = false;

    void jump(float v) { value = from = target = v; animating = false; }

    void to_(float v, uint16_t ms) {
        if (fabsf(v - value) < 0.001f) return;
        from = value;
        target = v;
        duration = ms;
        t0 = millis();
        animating = true;
    }

    void tick() {
        if (!animating) return;
        unsigned long e = millis() - t0;
        if (e >= duration) { value = target; animating = false; return; }
        float p = (float)e / duration;
        p = 1 - powf(1 - p, 3);          // easeOutCubic
        value = from + (target - from) * p;
    }
};

class RadioUI {
public:
    LGFX_Sprite cv{&tft};
    int W = 320, H = 240;
    bool ready = false;

    // 状态
    int stationIndex = 0;
    int stationTotal = 20;
    String stationName = "";
    int volume = 15, volumeMax = 21;
    bool playing = false;
    int rssi = -60;

    // 动画量
    UiAnim ring;        // 圆环进度角度(度)
    UiAnim freq;        // 装饰频率(MHz * 10)
    UiAnim volBar;      // 音量条高度比例 0..1

    unsigned long lastRedraw = 0;
    static const uint16_t ANIM_MS = 650;

    void begin(int w, int h) {
        W = w; H = h;
        cv.setColorDepth(16);
        cv.createSprite(W, H);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextDatum(top_center_datum);
        ready = true;
    }

    // 装饰频率: 87.5 ~ 107.9, 由台号映射 (纯视觉)
    static float fakeFreq(int idx) {
        return 87.5f + (idx % 41) * 0.5f;
    }

    // 圆环目标角度: 当前台在列表中的比例, 0 度在 12 点方向
    float ringAngleFor(int idx, int total) {
        if (total <= 0) return 0;
        return 360.0f * (idx % total) / total;
    }

    // ---- 状态更新入口 (由 main 调用) ----
    void setStation(int idx, int total, const String& name, bool instant = false) {
        stationIndex = idx; stationTotal = total; stationName = name;
        float ra = ringAngleFor(idx, total);
        float fr = fakeFreq(idx) * 10.0f;
        if (instant || !ready) {
            ring.jump(ra); freq.jump(fr);
        } else {
            ring.to_(ra, ANIM_MS);
            freq.to_(fr, ANIM_MS);
        }
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

    // 每帧调用: 推进动画, 有动画时才重绘
    void tick() {
        if (!ready) return;
        bool wasAnimating = ring.animating || freq.animating || volBar.animating;
        ring.tick(); freq.tick(); volBar.tick();
        bool stillAnimating = ring.animating || freq.animating || volBar.animating;
        if (wasAnimating || stillAnimating) redraw();
    }

    // ---- 绘制 ----
    void redraw() {
        if (!ready) return;
        cv.fillScreen(UI_BG);
        drawHeader();
        drawRing();
        drawCenter();
        drawLeftBar();
        drawRightBar();
        drawButtons();
        cv.pushSprite(0, 0);
        lastRedraw = millis();
    }

private:
    void drawHeader() {
        struct tm info;
        cv.setTextColor(UI_TEXT, UI_BG);
        cv.setFont(&fonts::efontCN_16);
        if (getLocalTime(&info)) {
            char t[16];
            strftime(t, sizeof(t), "%H:%M", &info);
            cv.drawString(t, 6, 4);
            sprintf(t, "%02d/%02d", info.tm_mon + 1, info.tm_mday);
            cv.drawString(t, W - 52, 4);
        }
        // WiFi 信号弧
        int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
        for (int i = 0; i < 4; i++) {
            int bx = W - 96 + i * 7;
            int bh = 3 + i * 3;
            cv.fillRect(bx, 18 - bh, 5, bh, i < bars ? UI_CYAN : UI_TEXT_DIM);
        }
    }

    void drawRing() {
        int cx = W / 2, cy = H / 2 - 6;
        int rOut = min(W, H) / 2 - 30;
        int rIn = rOut - 9;
        if (rIn < 20) rIn = 20;

        // 底环
        cv.fillArc(cx, cy, rOut, rIn, 0, 360, UI_PANEL);
        // 刻度点
        int ticks = stationTotal > 0 ? stationTotal : 20;
        for (int i = 0; i < ticks; i++) {
            float a = (-90 + 360.0f * i / ticks) * DEG_TO_RAD;
            int x1 = cx + cosf(a) * (rOut + 3);
            int y1 = cy + sinf(a) * (rOut + 3);
            int x2 = cx + cosf(a) * (rOut + 7);
            int y2 = cy + sinf(a) * (rOut + 7);
            cv.drawLine(x1, y1, x2, y2, UI_TEXT_DIM);
        }
        // 进度弧 (从 12 点顺时针)
        float ang = ring.value;
        if (ang < 1) ang = 1;
        cv.fillArc(cx, cy, rOut, rIn, -90, -90 + ang, UI_CYAN);
        // 末端光点
        float a = (-90 + ang) * DEG_TO_RAD;
        int px = cx + cosf(a) * ((rOut + rIn) / 2);
        int py = cy + sinf(a) * ((rOut + rIn) / 2);
        cv.fillCircle(px, py, 6, UI_WHITE);
        cv.drawCircle(px, py, 8, UI_CYAN);
        // 内圈装饰弧
        cv.drawArc(cx, cy, rIn - 6, rIn - 6, -60, 240, UI_BLUE);
    }

    void drawCenter() {
        int cx = W / 2, cy = H / 2 - 6;
        // 台号
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        char idxBuf[24];
        snprintf(idxBuf, sizeof(idxBuf), "%d/%d", stationIndex + 1, stationTotal);
        cv.drawString(idxBuf, cx, cy - 48);
        // 电台名 (真实)
        cv.setTextColor(UI_CYAN, UI_BG);
        const char* nm = stationName.length() ? stationName.c_str() : "网络电台";
        cv.drawString(nm, cx, cy - 30);
        // 装饰频率 (大字, 滚动)
        cv.setFont(&fonts::FreeSans24pt7b);
        cv.setTextColor(UI_WHITE, UI_BG);
        char fBuf[16];
        snprintf(fBuf, sizeof(fBuf), "%.1f", freq.value / 10.0f);
        cv.drawString(fBuf, cx, cy - 4);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("MHz", cx, cy + 30);
        // 播放状态
        cv.setTextColor(playing ? UI_CYAN : UI_TEXT_DIM, UI_BG);
        cv.drawString(playing ? "播放中" : "已暂停", cx, cy + 48);
    }

    void drawLeftBar() {
        // RSSI 竖条
        int x = 8, y0 = H / 2 - 40, h = 80;
        cv.setTextDatum(top_center_datum);
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("信号", x + 10, y0 - 18);
        cv.drawRoundRect(x, y0, 20, h, 6, UI_TEXT_DIM);
        int bars = rssi > -55 ? 5 : rssi > -65 ? 4 : rssi > -75 ? 3 : rssi > -85 ? 2 : 1;
        int fill = h * bars / 5;
        cv.fillRoundRect(x + 2, y0 + h - fill - 2, 16, fill, 5, UI_BLUE);
    }

    void drawRightBar() {
        // 音量竖条 (动画)
        int x = W - 28, y0 = H / 2 - 40, h = 80;
        cv.setFont(&fonts::efontCN_16);
        cv.setTextColor(UI_TEXT_DIM, UI_BG);
        cv.drawString("音量", x + 10, y0 - 18);
        cv.drawRoundRect(x, y0, 20, h, 6, UI_TEXT_DIM);
        int fill = (int)(h * volBar.value);
        if (fill > h) fill = h;
        cv.fillRoundRect(x + 2, y0 + h - fill - 2, 16, fill, 5, UI_PURPLE);
        // 音量数字
        cv.setTextColor(UI_TEXT, UI_BG);
        char vBuf[8];
        snprintf(vBuf, sizeof(vBuf), "%d", volume);
        cv.drawString(vBuf, x + 10, y0 + h + 4);
    }

    void drawButtons() {
        // 底部 6 个按钮提示 (无触摸, 仅显示; 实际操作走实体键)
        const char* labels[6] = {"FM", "播放", "暂停", "上一个", "下一个", "设置"};
        int n = 6;
        int gap = 4;
        int bw = (W - gap * (n + 1)) / n;
        int bh = 26;
        int by = H - bh - 10;
        cv.setFont(&fonts::efontCN_16);
        for (int i = 0; i < n; i++) {
            int bx = gap + i * (bw + gap);
            bool active = (i == 0);                       // FM 常亮
            if (i == 1 && playing) active = true;         // 播放中
            if (i == 2 && !playing) active = true;        // 暂停中
            if (active) {
                cv.fillRoundRect(bx, by, bw, bh, 5, UI_BLUE);
                cv.drawRoundRect(bx, by, bw, bh, 5, UI_CYAN);
                cv.setTextColor(UI_WHITE, UI_BLUE);
            } else {
                cv.drawRoundRect(bx, by, bw, bh, 5, UI_TEXT_DIM);
                cv.setTextColor(UI_TEXT_DIM, UI_BG);
            }
            cv.drawString(labels[i], bx + bw / 2, by + 5);
        }
    }
};
