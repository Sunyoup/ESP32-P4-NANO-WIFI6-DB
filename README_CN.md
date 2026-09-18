# ESP32-P4-NANO-WIFI6-DB

基于 ESP32-P4 与 ESP32-C5 的多媒体、网络和边缘应用开发板

[ESP-IDF 示例](./examples/esp_idf/) · [许可证](#许可证)

[English](./README.md) · 中文 · [🌐 产品页](https://www.waveshare.net/shop/ESP32-P4-NANO-WIFI6-DB.htm) · [📚 中文文档](https://docs.waveshare.net/ESP32-P4-NANO-WIFI6-DB/) · [🧩 ESP-IDF 示例](./examples/esp_idf/) · [🔧 Arduino 示例](./examples/Arduino/) · [📦 固件](./firmware/)

![ESP32-P4-NANO-WIFI6-DB](./assert/ESP32-P4-NANO-WIFI6-DB.jpg)

微雪 `ESP32-P4-NANO-WIFI6-DB` 是一款面向多媒体、网络和边缘应用开发的
ESP32-P4 开发板。本仓库提供与该开发板对应的 ESP-IDF BSP、Arduino 板卡文件、
外设示例、Brookesia 固件源码以及预编译固件。

开发板采用 ESP32-P4 作为主控，并通过 ESP-Hosted/SDIO 连接板载 ESP32-C5
无线协处理器；同时提供 MIPI-DSI 显示、MIPI-CSI 摄像头、音频、microSD、
Ethernet、USB Host 和扩展 GPIO 等硬件接口。显示屏和摄像头为外接模块，
具体连接方式以实际硬件和对应示例说明为准。

## ✨ 产品概述

本仓库包含：

- 面向 `esp32p4` 的本地 ESP-IDF BSP 组件；
- 基于 Arduino-ESP32 `3.3.11` 的板卡定义和示例；
- 覆盖 I2C、I2S 音频、SDMMC、MIPI-DSI、MIPI-CSI、Wi-Fi、Ethernet、USB 和
  NVS 等功能的 ESP-IDF 示例；
- `firmware/brookesia` 应用固件源码；
- P4 主控和 C5 无线协处理器的预编译固件；
- 开发板原理图和 BSP API 说明。

常用入口：

| 资源 | 说明 |
| --- | --- |
| [ESP-IDF BSP](./components/esp32_p4_nano_wifi6_db/) | 板级初始化、显示、触摸、音频、存储、摄像头和 USB Host 接口 |
| [BSP 使用说明](./components/esp32_p4_nano_wifi6_db/README.md) | 引脚、面板配置和基本用法 |
| [BSP API](./components/esp32_p4_nano_wifi6_db/API.md) | BSP 宏和公共函数 |
| [ESP-IDF 示例](./examples/esp_idf/) | ESP-IDF 外设和功能示例 |
| [Arduino 示例](./examples/Arduino/) | Arduino 工程、板卡文件和示例 |
| [硬件原理图](./hardware/schematics/ESP32-P4-NANO-WIFI6-DB.pdf) | 开发板原理图 |
| [官方文档](https://docs.waveshare.net/ESP32-P4-NANO-WIFI6-DB/) | 微雪硬件和软件开发文档 |

## 🖥️ 硬件概况

| 功能 | 器件 / 接口 |
| --- | --- |
| 主处理器 | ESP32-P4NRW32X，HP 系统双核 RISC-V 主频最高 360 MHz，并包含低功耗单核 |
| 无线 | ESP32-C5 无线协处理器，通过 ESP-Hosted/SDIO 与 ESP32-P4 连接 |
| 存储 | 16 MB Flash；Arduino 板卡配置默认启用 PSRAM |
| 显示 | 外接 MIPI-DSI LCD，支持双 DSI data lane |
| 触摸 | GT911 电容触摸控制器，使用共享 I2C 总线轮询 |
| 摄像头 | MIPI-CSI 接口；Arduino 摄像头示例使用 OV5647 |
| 音频 | ES8311 编解码器、单路模拟麦克风输入和扬声器输出 |
| 存储扩展 | microSD，4-bit SDMMC |
| 网络 | ESP32-C5 Wi-Fi 协处理器和板载 Ethernet 接口 |
| USB | USB Host |
| 扩展 | 多组 GPIO、I2C、UART、CAN/TWAI 等扩展接口 |

### 软件版本和配置

| 项目 | 配置 |
| --- | --- |
| ESP-IDF BSP | 组件版本 `0.0.1`，manifest 要求 ESP-IDF `>=5.5` |
| Arduino-ESP32 | `3.3.11` |
| Arduino 板卡 | `Waveshare ESP32-P4-NANO-WIFI6-DB` |
| Arduino Flash | 16 MB，默认分区方案 `default_16MB` |
| Arduino 串口 | 默认 `115200` |

### 关键硬件资源

| 功能 | 引脚 / 参数 | 说明 |
| --- | --- | --- |
| 共享 I2C | SDA `GPIO7`，SCL `GPIO8` | ES8311、GT911、LCD 背光和摄像头 SCCB 共用 |
| ES8311 I2S | MCLK/BCLK/WS/DOUT/DIN：`GPIO13/12/10/9/11` | 音频输入输出 |
| 功放使能 | `GPIO53`，高电平有效 | 音频功率放大器使能 |
| SDMMC 4-bit | CLK/CMD：`GPIO43/44`；D0-D3：`GPIO39/40/41/42` | SD 卡电源控制为 `GPIO45`，低电平有效 |
| ESP-Hosted SDIO | CLK/CMD/D0-D3/RESET：`GPIO18/19/14/15/16/17/54` | 连接 ESP32-C5 无线协处理器 |
| LCD 背光 | I2C 地址 `0x45`，寄存器 `0x96` | 无独立背光 GPIO |
| LCD 复位 | 未连接 | 由面板初始化流程处理 |
| GT911 复位 / 中断 | 未连接 | 触摸数据通过 I2C 轮询 |

GPIO45 和 GPIO53 同时被 BSP 用于 SD 卡供电和音频功放控制。对应外设工作时，
不要将这两个引脚当作普通 GPIO 独立驱动。

### MIPI-DSI 面板

Nano BSP 支持以下外接 Waveshare DSI 触摸屏。默认面板为 10.1 英寸 JD9365；
切换面板时，控制器、分辨率、初始化命令和 DSI 速率必须使用同一套配置。

| 面板选项 | 控制器 | 分辨率 | DSI lane 速率 |
| --- | --- | ---: | ---: |
| Waveshare 5-DSI-TOUCH-A | HX8394 | 720 × 1280 | 700 Mbps |
| Waveshare 7-DSI-TOUCH-A | ILI9881C | 720 × 1280 | 1000 Mbps |
| Waveshare 8-DSI-TOUCH-A | JD9365 | 800 × 1280 | 1500 Mbps |
| Waveshare 10.1-DSI-TOUCH-A | JD9365 | 800 × 1280 | 1500 Mbps |

## 🚀 快速开始

### ESP-IDF

安装满足 BSP 要求的 ESP-IDF 环境后，在 ESP-IDF 终端中进入示例目录。建议先
运行不依赖外部模块的板级检查示例：

```
cd examples/esp_idf/00_board_check
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

将 `PORT` 替换为开发板对应的串口，例如 Windows 下的 `COM7`。推荐按以下顺序
确认基础硬件和外设：

1. `00_board_check`：确认芯片、Flash、PSRAM 和串口链路。
2. `01_i2c_tools`：确认 GPIO7/GPIO8 共享 I2C 总线。
3. `02_sdmmc`：确认 microSD 和 4-bit SDMMC。
4. `06_Displaycolorbar`：确认实际 LCD 面板、MIPI-DSI 时序和背光。
5. `05_I2SCodec`：确认 ES8311 音频输入输出。
6. `03_wifistation`：确认 ESP-Hosted、ESP32-C5、Wi-Fi 关联和 DHCP。

### Arduino

Arduino 示例基于 Arduino-ESP32 `3.3.11`，目标芯片为 ESP32-P4；ESP32-C5 是由
ESP-Hosted 管理的无线协处理器，不是 Arduino 工程的编译目标。

1. 安装 Arduino-ESP32 `3.3.11`。
2. 将
   `examples/Arduino/esp32/variants/waveshare_esp32_p4_nano_wifi6_db`
   复制到对应版本 Arduino-ESP32 的 `variants` 目录。
3. `examples/Arduino/esp32/boards.txt` 是完整的 `boards.txt` 快照。建议先备份
   Arduino-ESP32 原文件，再合并以
   `waveshare_esp32_p4_nano_wifi6_db.` 开头的板卡条目；不要覆盖已有的其他板卡定义。
4. 重启 Arduino IDE，选择 `Waveshare ESP32-P4-NANO-WIFI6-DB`。
5. 选择 16 MB Flash、`default_16MB` 分区方案、启用 PSRAM，并选择正确的串口。
6. 按实际连接的 LCD 面板修改显示示例中的面板 profile。

## 🧪 ESP-IDF 示例

所有示例位于 [examples/esp_idf](./examples/esp_idf)。每个示例目录包含独立的
工程配置和 README；其中部分示例同时提供中文和英文说明。

| 示例 | 主要功能 |
| --- | --- |
| [00_board_check](./examples/esp_idf/00_board_check/) | 首次运行时检查芯片、Flash、PSRAM 和基础串口输出 |
| [01_i2c_tools](./examples/esp_idf/01_i2c_tools/) | I2C 总线扫描和交互式诊断 |
| [02_sdmmc](./examples/esp_idf/02_sdmmc/) | microSD 卡 4-bit SDMMC 读写 |
| [03_wifistation](./examples/esp_idf/03_wifistation/) | 通过 ESP32-C5/ESP-Hosted 连接 Wi-Fi |
| [04_ethernetbasic](./examples/esp_idf/04_ethernetbasic/) | Ethernet 链路和 IP 地址初始化 |
| [05_I2SCodec](./examples/esp_idf/05_I2SCodec/) | ES8311 音频播放、采集和回环 |
| [06_Displaycolorbar](./examples/esp_idf/06_Displaycolorbar/) | MIPI-DSI LCD 色条和背光测试 |
| [07_lvgl_demo_v9](./examples/esp_idf/07_lvgl_demo_v9/) | LVGL 9 图形界面示例 |
| [08_eth2ap](./examples/esp_idf/08_eth2ap/) | Ethernet 到 Wi-Fi 接入点示例 |
| [09_simple_video_server](./examples/esp_idf/09_simple_video_server/) | 摄像头视频流、图像抓取和 Web 控制 |
| [10_video_lcd_display](./examples/esp_idf/10_video_lcd_display/) | 摄像头视频输出到 LCD |
| [11_usb_extend_screen](./examples/esp_idf/11_usb_extend_screen/) | USB 扩展屏示例 |
| [12_nvs_counter](./examples/esp_idf/12_nvs_counter/) | 使用 NVS 保存启动计数器 |

## 🧪 Arduino 示例

| 示例 | 主要功能 |
| --- | --- |
| [board_check](./examples/Arduino/board_check/) | 打印芯片、Flash、PSRAM 和堆内存信息 |
| [gpio](./examples/Arduino/gpio/) | 通过串口命令交互式测试 GPIO |
| [i2c](./examples/Arduino/i2c/) | 扫描共享 I2C1 总线地址 |
| [i2s](./examples/Arduino/i2s/) | ES8311 单麦克风到单扬声器的音频回环 |
| [sdmmc](./examples/Arduino/sdmmc/) | 4-bit microSD 文件读写测试 |
| [mipi_dsi](./examples/Arduino/mipi_dsi/) | MIPI-DSI LCD 色条和背光测试 |
| [mipi_csi](./examples/Arduino/mipi_csi/) | OV5647 摄像头实时显示到 MIPI-DSI LCD |

`mipi_dsi` 和 `mipi_csi` 默认使用 10.1 英寸 JD9365 的 800 × 1280、RGB565、双
lane 配置。使用其他 LCD 时，必须根据实际面板选择 JD9365、HX8394 或 ILI9881C
profile，不能直接套用默认配置。

I2C1 是共享总线。不同示例可能使用 Arduino `Wire1`、legacy I2C API 或新版
`i2c_master` API，不要在同一个程序中重复初始化同一总线；详细限制请阅读对应
示例 README。

## 📦 固件

### Brookesia 固件源码

[firmware/brookesia](./firmware/brookesia/) 是基于 ESP-Brookesia 的应用固件源码，
包含图形界面、设置、音乐播放器、视频播放器、摄像头和其他应用组件。

### 预编译固件

| 文件 | 用途 |
| --- | --- |
| [ESP32-P4-NANO-WIFI6-DB-FactoryOnly-260623.bin](./firmware/bin/ESP32-P4-NANO-WIFI6-DB-FactoryOnly-260623.bin) | ESP32-P4 主控出厂固件 |
| [esp32-c5-slave-260623.bin](./firmware/bin/ESP32-C5/esp32-c5-slave-260623.bin) | ESP32-C5 无线协处理器从机固件 |

P4 主控固件和 C5 从机固件属于不同目标，烧录时请确认目标芯片、烧录地址和
固件版本。重新编译或替换 C5 固件时，还需要确认其与 P4 工程使用的 ESP-Hosted
和远程 Wi-Fi 组件版本兼容。

## 🗂️ 仓库结构

| 路径 | 用途 |
| --- | --- |
| [assert/](./assert/) | README 使用的开发板产品图片 |
| [examples/esp_idf/](./examples/esp_idf/) | ESP-IDF 外设和功能示例 |
| [examples/Arduino/](./examples/Arduino/) | Arduino 示例、板卡文件和 variant |
| [components/esp32_p4_nano_wifi6_db/](./components/esp32_p4_nano_wifi6_db/) | Nano 本地 BSP 组件 |
| [firmware/brookesia/](./firmware/brookesia/) | Brookesia 应用固件源码 |
| [firmware/bin/](./firmware/bin/) | P4 和 C5 预编译固件 |
| [hardware/schematics/](./hardware/schematics/) | 开发板原理图 |

`build/`、`managed_components/`、本地 `sdkconfig` 等生成文件不属于源码文档，
不应作为项目源文件提交。

## 📚 相关文档

- [文档索引](./docs/README.md)
- [硬件指南](./docs/zh_CN/hw-reference/user-guide-esp32-p4-nano-wifi6-db.md)
- [BSP 使用说明](./components/esp32_p4_nano_wifi6_db/README.md)
- [BSP API](./components/esp32_p4_nano_wifi6_db/API.md)
- [Arduino 板卡文件说明](./examples/Arduino/esp32/README.md)
- [Arduino 示例说明](./examples/Arduino/README.md)
- [ESP-IDF 板级检查 README](./examples/esp_idf/00_board_check/README_CN.md)
- [Arduino MIPI-CSI README](./examples/Arduino/mipi_csi/README.md)
- [开发板原理图](./hardware/schematics/ESP32-P4-NANO-WIFI6-DB.pdf)

## ⚠️ 使用注意

- 显示、触摸和摄像头均依赖外接模块、供电、线缆和面板 profile；源码通过或
  编译成功不等于已经完成目标硬件验证。
- I2C1 由 ES8311、GT911、LCD 背光和摄像头 SCCB 共用。使用摄像头示例时，
  还要确认摄像头供电和 SCCB 连接。
- GPIO45、GPIO53、SDIO 和 MIPI 相关引脚存在板级复用关系，使用扩展接口前请
  先查看引脚表和原理图。
- ESP32-P4 的 Wi-Fi 功能依赖 ESP32-C5 从机固件和 ESP-Hosted/SDIO 通道；修改
  任一侧的软件版本时，应同步检查组件兼容性。
- 请勿将真实 Wi-Fi 密码、API key 或带凭据的串口日志提交到仓库。

## 📄 许可证

Nano BSP 和部分附加组件目录中包含各自的 `LICENSE` 文件。使用和再分发时，
请以对应目录中的许可证文本为准。
