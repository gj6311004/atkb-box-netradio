#pragma once
// ============================================================
// 正点原子 ATK ESP32S3 BOX 屏幕驱动 (LovyanGFX)
// ST7789 320x240, 8-bit i8080 并口
// 引脚依据: 78/xiaozhi-esp32 atk-dnesp32s3-box/config.h
//   CS=1  DC=2  RD=41  WR=42  RST=NC
//   D0=40 D1=39 D2=38 D3=12 D4=11 D5=10 D6=9  D7=46
// 背光由 XL9555 P0.7 控制 (非 PWM), 见 atk_hardware.h
//
// 屏幕方向可在运行时调整:
//   开机后长按 BOOT 键 3 秒进入"方向设置"模式, 屏幕会显示当前方向编号,
//   短按 BOOT 切换 0~7, 长按 3 秒保存并重启。设置存 NVS, 掉电不丢。
// ============================================================
#include <LovyanGFX.hpp>

// 默认旋转值。0~7 之一; 若开机方向不对, 用上面按键方法调, 无需改代码。
#define ATK_DEFAULT_ROTATION 1

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789   _panel_instance;
    lgfx::Bus_Parallel8  _bus_instance;

public:
    LGFX(void) {
        // ---------- i8080 8位并口总线 ----------
        {
            auto cfg = _bus_instance.config();
            // ESP32-S3 的 Bus_Parallel8 走 LCD_CAM 外设 (非 I2S), S3 只有 port 0。
            cfg.port = 0;
            cfg.pin_wr = 42;       // WR
            cfg.pin_rd = 41;       // RD
            cfg.pin_rs = 2;        // RS == DC
            cfg.pin_d0 = 40;
            cfg.pin_d1 = 39;
            cfg.pin_d2 = 38;
            cfg.pin_d3 = 12;
            cfg.pin_d4 = 11;
            cfg.pin_d5 = 10;
            cfg.pin_d6 = 9;
            cfg.pin_d7 = 46;
            cfg.freq_write = 10000000;   // 10MHz, 与小智 pclk 一致
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ---------- ST7789 面板 ----------
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = 1;
            cfg.pin_rst  = -1;     // 板上未接复位
            cfg.pin_busy = -1;

            // ST7789 控制器物理尺寸是 240x320; 横竖屏由 setRotation 决定。
            cfg.memory_width  = 240;
            cfg.memory_height = 320;
            cfg.panel_width   = 240;
            cfg.panel_height  = 320;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.offset_rotation = 0;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable   = false;
            cfg.invert     = true;   // 小智: invert_color(true)
            cfg.rgb_order  = false;  // RGB
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};
