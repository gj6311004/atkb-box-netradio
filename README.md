# 正点原子 ATK ESP32S3 BOX 网络收音机

把 ATK ESP32S3 BOX 改成一台 WiFi 网络电台。
基于开源项目 **`zhuhai-esp/ESP32-S3-AmourK08-4Keys` 的 `K08-4Keys-NetRadio`** 修改，
只改硬件适配层，播放/配网/OTA 逻辑沿用原项目。

---

## 1. 这个工程适配哪块板

**适配 ATK ESP32S3 BOX 中采用下列硬件配置的型号**（已在用户实机上验证可正常显示和播放），判据：

| 项目 | 值 |
|---|---|
| 模组 | ATK-MWS3S (ESP32-S3R8, 16MB Flash + 8MB PSRAM) |
| 屏幕 | 320×240 ST7789，**8 位 i8080 并口** |
| 音频 | ES8311（I2C 0x18，无独立 MCLK，从 BCLK 恢复） |
| IO 扩展 | XL9555（I2C 0x20）：功放/背光/K1/K2 |

> **重要**：正点原子 ESP32S3 BOX 有多个硬件版本，引脚完全不同，固件**不能通用**：
> - 本固件对应 **320×240 i8080 并口屏 + I2C 48/45 + I2S 21/13/14** 这一套（即小智 `atk-dnesp32s3-box` 定义）。
> - 小智项目里另有 `atk-dnesp32s3-box0`（240×240 **SPI 屏**，I2C 11/12，I2S 5/10/9，带电源/充电引脚），**不能用本固件**。
> - BOX2 是 ES8389 + 不同 I2S 引脚，BOX3 是 SPI 屏 + ES8311/ES7210，同样不通用。
>
> 如果不确定你的板子属于哪种，看屏幕接口和引脚即可：本固件要求 8 位并口屏（D0~D7 见下表）。
> 本工程**不使用触摸屏**，只用实体按键，所以有无触摸不影响。

### 引脚映射（来自小智官方 atk-dnesp32s3-box 板级定义）

| 功能 | GPIO |
|---|---|
| I2C SDA / SCL | 48 / 45 |
| I2S BCLK / WS(LRC) / DOUT | 21 / 13 / 14 |
| 屏 CS / DC / RD / WR | 1 / 2 / 41 / 42 |
| 屏 D0~D7 | 40,39,38,12,11,10,9,46 |
| BOOT 键 (K0) | 0 |
| K1 / K2 | XL9555 P0.4 / P0.3 |
| 功放 / 背光 | XL9555 P0.5 / P0.7 |

---

## 2. 目录结构

```
atkb-box-netradio/
├── .github/workflows/build.yml   # GitHub 云端自动编译（无需本机工具链）
├── platformio.ini          # 编译配置（16MB Flash + OPI PSRAM）
├── include/
│   ├── lgfx.h              # i8080 并口 ST7789 驱动（本次适配）
│   └── atk_hardware.h      # XL9555 + ES8311 驱动（本次适配）
└── src/
    └── main.cpp            # 网络收音机主程序（本次适配）
```

## 3. 编译（GitHub 云端，本机零安装）

本机**不需要**安装 PlatformIO / ESP-IDF。已配好 GitHub Actions：

1. 把本目录上传到你的 GitHub 仓库（在仓库网页点 `Add file → Upload files`，
   选中整个 `atkb-box-netradio` 里的内容，即 `.github`、`include`、`src`、`platformio.ini`、
   `README.md` 都放到仓库**根目录**）。
2. 提交后进入仓库的 **Actions** 标签页，等待 `Build NetRadio Firmware` 跑完（首次约 5–10 分钟）。
3. 点开该次运行，在页面底部 **Artifacts** 下载 `atkb-box-netradio-firmware`。
4. 解压得到 **`atkb-box-netradio-16MB.bin`**（合并好的整片固件，一个文件搞定）和
   `firmware.bin`（备用）。

> 以后改了源码，只要 push，GitHub 会自动重新编译出新固件。
> 也可以进 Actions 页面点 **Run workflow** 手动触发。

<details>
<summary>可选：想在本机编译（需装 PlatformIO，占约 10GB 磁盘）</summary>

```bash
cd atkb-box-netradio
pio run -e atkb-box
```

成功后固件在 `.pio/build/atkb-box/firmware.bin`。首次会下载工具链。

</details>

## 4. 烧录

这块 BOX 有两个 USB-C 口：
- **USB-SLAVE**：芯片原生 USB Serial/JTAG（推荐，设备号 303a:1001）
- **UART**：CH343P 转串口

只需本机装一个很小的 **esptool**（`pip install esptool`，几十 MB，远小于整套工具链）。

### ⚠️ 先备份原厂固件（强烈建议）

16MB 整片备份，以后可刷回：

```bash
python -m esptool --chip esp32s3 -p COMx read-flash 0 0x1000000 atk-box-factory.bin
```

### 烧录方式 A：整片固件（推荐，来自 GitHub 的合并 bin）

```bash
python -m esptool --chip esp32s3 -p COMx erase-flash
python -m esptool --chip esp32s3 -p COMx write-flash 0x0 atkb-box-netradio-16MB.bin
```

### 烧录方式 B：分文件（本机编译时）

```bash
python -m esptool --chip esp32s3 -p COMx erase-flash
python -m esptool --chip esp32s3 -p COMx write-flash \
  0x0 .pio/build/atkb-box/bootloader.bin \
  0x8000 .pio/build/atkb-box/partition-table.bin \
  0x10000 .pio/build/atkb-box/firmware.bin
```

> COMx 换成设备管理器里实际的串口号。若烧录失败：按住 BOOT 键再按 RESET 进入下载模式。

## 5. 使用

### 首次配网（不用装任何 APP）

1. 刚烧录完、或换 WiFi 后，开机会自动开一个热点 **`ATK-BOX-Setup`**。
2. 用手机 WiFi **连上这个热点**（2.4GHz）。大部分手机会自动弹出配网页面；
   没弹出就打开浏览器访问 **`192.168.4.1`**。
3. 在网页里选择你家的 WiFi、输入密码、保存。
4. 盒子自动连上家里 WiFi，配网信息会记住，以后开机直接连，不用再配。
   - 若想重新配网：长按 BOOT 键约 10 秒可清除配网（或重新烧录）。

### 屏幕方向调整（若显示颠倒/镜像）

开机后：
1. **长按 BOOT 键 3 秒** → 进入"屏幕方向设置"界面，显示当前方向编号 `方向 N / 7`。
2. **短按 BOOT** → 方向 +1（0~7 循环），屏幕立刻跟着变。
3. 看到方向正确后，**再长按 BOOT 3 秒** → 保存并自动重启。
4. 设置存进芯片，掉电不丢，以后不用再调。

### 日常按键

- **K0 (BOOT)**：短按 播放/暂停
- **K1**：短按 下一台，**长按 音量 +**
- **K2**：短按 上一台，**长按 音量 −**
- 电台播放结束自动切下一台
- 熄屏后按任意键唤醒屏幕
- OTA：连上同一网络后，可用 ArduinoOTA 无线更新（Arduino IDE / platformio）。

### 后台管理网页（重点）

手机或电脑连到和盒子同一个 WiFi，浏览器打开**屏幕上显示的 IP**（例如 `192.168.1.50`），即可进入管理页：

1. **音量**：拖动滑块（0~21），立即生效并记住。
2. **亮屏时间**：下拉选 常亮 / 15秒 / 30秒 / 1分钟 / 3分钟 / 5分钟。
   无操作到时间后自动关背光（不影响播放），按任意键或刷新网页唤醒。
3. **广播源管理**：
   - 点任一电台即可切换播放，当前电台绿色高亮。
   - **添加自定义电台**：填名称 + 流地址（http/https 的 mp3 或 m3u8 直链），点添加。
   - 自定义电台可点"删除"（内置 20 个电台不可删）。
   - 所有设置和自定义电台存芯片，掉电不丢。

## 6. 已知限制 / 可能要微调的地方

- **电台源**：沿用原项目的喜马拉雅开放接口 `lhttp.qtfm.cn`，若接口失效需替换 `FM_URL` 或改成自己的 m3u8/mp3 直链。
- **屏幕方向**：默认旋转写在 `lgfx.h` 的 `ATK_DEFAULT_ROTATION`（值 0~7）。若开机方向不对，用上面"方向调整"步骤调；或改这个宏后重新 push 编译。若只是颜色反了（负片），把 `lgfx.h` 里 `cfg.invert` 改成 `false`。
- **不使用触摸屏**：本工程纯按键控制，无触屏的 1.0 版本和有触屏的版本都能用（触摸一律不接入）。
- **NS4168 版本**：小智代码会探测 P0.5 判断是 ES8311 还是 NS4168（纯功放）。本工程按 ES8311 编写；若你的盒子探测为 NS4168（无 ES8311），需要改用直驱 I2S 功放分支（见原项目 `initAudioDevice`）。
- **音量范围**：audioI2S 的音量是 0~21，与 ES8311 硬件音量独立；ES8311 固定满刻度，由 audioI2S 数字音量控制。

## 7. 来源与许可

- 播放逻辑：`zhuhai-esp/ESP32-S3-AmourK08-4Keys` (K08-4Keys-NetRadio)
- 板级引脚：`78/xiaozhi-esp32` main/boards/alientek/atk-dnesp32s3-box
- ES8311 寄存器：`espressif/esp-bsp` components/es8311
- 屏幕/音频库：LovyanGFX、ESP32-audioI2S（各按其许可证）

尊重原作者开源协议，修改分发时请保留出处。
