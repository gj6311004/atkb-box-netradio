#pragma once
// ============================================================
// 正点原子 ATK-DNESP32S3-BOX V1.1 板级硬件驱动
//   - XL9555  : 16bit I2C IO 扩展 (0x20) -> 功放 / 背光 / 按键
//   - ES8311  : 音频编解码器 (0x18), 无独立 MCLK, 从 BCLK 恢复
//
// 引脚依据: 78/xiaozhi-esp32 main/boards/alientek/atk-dnesp32s3-box
// 寄存器依据: espressif/esp-bsp components/es8311
// ============================================================
#include <Arduino.h>
#include <Wire.h>

// ---------- 板级引脚定义 ----------
#define ATK_I2C_SDA        48
#define ATK_I2C_SCL        45

// I2S (ESP32-audioI2S 只需要 BCLK / LRC / DOUT 三根线)
#define ATK_I2S_BCLK       21
#define ATK_I2S_LRC        13
#define ATK_I2S_DOUT       14

// XL9555
#define ATK_XL9555_ADDR    0x20
#define ATK_XL9555_INT     4      // 板上 LED 也复用, 本工程不用中断

// XL9555 端口位 (P0.x)
#define XL_P_SPK_CTRL      5      // 功放 / 音频设备选择 (高=ES8311)
#define XL_P_BACKLIGHT     7      // 背光 (高=亮)
#define XL_P_KEY1          4      // K1 输入
#define XL_P_KEY2          3      // K2 输入

// ES8311
#define ATK_ES8311_ADDR    0x18

// ---------- XL9555 寄存器 ----------
#define XL9555_INPUT0      0x00
#define XL9555_INPUT1      0x01
#define XL9555_OUTPUT0     0x02
#define XL9555_OUTPUT1     0x03
#define XL9555_CONFIG0     0x06   // 1 = 输入, 0 = 输出
#define XL9555_CONFIG1     0x07

class XL9555 {
public:
    void begin() {
        // 先把 P0.5 设为输入, 读取电平判断音频芯片类型
        writeReg(XL9555_CONFIG0, 0x3B);
        writeReg(XL9555_CONFIG1, 0xFE);
        delay(10);
        // bit5=1 -> ES8311; bit5=0 -> NS4168 (无 codec, 直接 I2S 功放)
        _es8311 = readReg(XL9555_INPUT0) & (1 << XL_P_SPK_CTRL);

        // P0.5 改为输出 (功放/音频选择), P0.3/P0.4 保持输入 (按键)
        // 0x1B = 0001 1011 : P0.3/P0.4 输入, P0.5/P0.7 输出
        writeReg(XL9555_CONFIG0, 0x1B);
        writeReg(XL9555_CONFIG1, 0xFE);

        // 打开功放 + 背光
        setOutput(XL_P_SPK_CTRL, 1);
        setOutput(XL_P_BACKLIGHT, 1);
    }

    bool hasES8311() const { return _es8311; }

    void setOutput(uint8_t bit, uint8_t level) {
        uint8_t reg = (bit < 8) ? XL9555_OUTPUT0 : XL9555_OUTPUT1;
        uint8_t idx = (bit < 8) ? bit : (bit - 8);
        uint8_t data = readReg(reg);
        data = (data & ~(1 << idx)) | (level << idx);
        writeReg(reg, data);
    }

    // 读取 P0.x 输入电平 (按键按下通常为低)
    bool readInput(uint8_t bit) {
        uint8_t reg = (bit < 8) ? XL9555_INPUT0 : XL9555_INPUT1;
        uint8_t idx = (bit < 8) ? bit : (bit - 8);
        return (readReg(reg) >> idx) & 0x01;
    }

    void backlight(bool on)    { setOutput(XL_P_BACKLIGHT, on ? 1 : 0); }
    void amplifier(bool on)    { setOutput(XL_P_SPK_CTRL, on ? 1 : 0); }

private:
    bool _es8311 = true;

    void writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(ATK_XL9555_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }
    uint8_t readReg(uint8_t reg) {
        Wire.beginTransmission(ATK_XL9555_ADDR);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)ATK_XL9555_ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0;
    }
};

// ---------- ES8311 ----------
// 采用 slave 模式, MCLK 从 BCLK 恢复 (use_mclk = false)
// 系数使用 44.1k/48k 行: {pre_div=1, mult=3, adc_div=1, dac_div=1,
//                        fs=0, lrch=0, lrcl=0xff, bclkdiv=4, osr=0x10,0x10}
// 该组寄存器对 44100Hz 与 48000Hz 通用 (见 coeff_div 表)。
class ES8311 {
public:
    void begin() {
        write(0x00, 0x1F);        // 软件复位
        delay(20);
        write(0x00, 0x00);        // 解除复位
        write(0x00, 0x80);        // Power up
        write(0x01, 0xBF);        // 开所有时钟; BIT7=1: MCLK 来自 BCLK(SCK) 引脚

        // ---- 采样率系数 (44.1k / 48k) ----
        write(0x02, 0x18);        // ((pre_div-1)<<5)|(mult<<3) = (0<<5)|(3<<3)
        write(0x03, 0x10);        // (fs_mode<<6)|adc_osr
        write(0x04, 0x10);        // dac_osr
        write(0x05, 0x00);        // ((adc_div-1)<<4)|(dac_div-1)
        write(0x06, 0x03);        // bclk_div - 1 = 3
        write(0x07, 0x00);        // lrck_h
        write(0x08, 0xFF);        // lrck_l

        // ---- 16-bit I2S 格式 ----
        write(0x09, 0x0C);        // SDP In  16bit
        write(0x0A, 0x0C);        // SDP Out 16bit

        // ---- DAC / 模拟输出上电 ----
        write(0x0D, 0x01);        // power up analog
        write(0x0E, 0x02);        // enable analog PGA
        write(0x12, 0x00);        // power up DAC
        write(0x13, 0x10);        // HP drive
        write(0x1C, 0x6A);        // ADC EQ bypass / DC offset cancel
        write(0x37, 0x08);        // bypass DAC equalizer

        setVolume(100);           // ES8311 输出满刻度, 音量由 audioI2S 数字控制
        unmute();
    }

    // volume: 0~100
    void setVolume(uint8_t volume) {
        if (volume > 100) volume = 100;
        uint8_t reg32 = (volume == 0) ? 0 : (uint8_t)((volume * 256) / 100 - 1);
        write(0x32, reg32);
    }

    void mute()   { uint8_t v = read(0x31); write(0x31, v | 0x60); }
    void unmute() { uint8_t v = read(0x31); write(0x31, v & ~0x60); }

private:
    void write(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(ATK_ES8311_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }
    uint8_t read(uint8_t reg) {
        Wire.beginTransmission(ATK_ES8311_ADDR);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)ATK_ES8311_ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0;
    }
};

// 全局板级外设
extern XL9555 atk_io;
extern ES8311 atk_codec;
