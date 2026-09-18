# ESP32-P4-NANO-WIFI6-DB

[英文版](../../en/hw-reference/user-guide-esp32-p4-nano-wifi6-db.md)

ESP32-P4-NANO-WIFI6-DB 是一款以 ESP32-P4NRW32X 为主处理器、搭配 ESP32-C5-MINI-1U 无线协处理器的开发板，两者通过 SDIO 连接。板载 MIPI-DSI、MIPI-CSI、ES8311 音频、microSD、以太网、USB 和扩展排针。

![ESP32-P4-NANO-WIFI6-DB](../../../assert/ESP32-P4-NANO-WIFI6-DB.jpg)

> **说明**
>
> 本页根据 [Nano BSP](../../../components/esp32_p4_nano_wifi6_db/) 和[官方文档](https://docs.waveshare.net/ESP32-P4-NANO-WIFI6-DB/)整理。电路连接细节见[原理图](../../../hardware/schematics/ESP32-P4-NANO-WIFI6-DB.pdf)。

## 板载资源

- **主处理器**：ESP32-P4NRW32X，双核高性能 RISC-V 系统与单核低功耗系统，集成 16 MB PSRAM。
- **Flash**：16 MB 外置闪存；仓库示例选择 16 MB 闪存配置。
- **无线协处理器**：ESP32-C5-MINI-1U，使用外接天线，支持 2.4 / 5 GHz Wi-Fi 6 和 Bluetooth 5 LE。应用通过 ESP-Hosted 和四位 SDIO 总线通信。
- **下载接口**：USB Type-C，用于 5 V 供电、固件下载及 UART0 串口监视，串口引脚为 GPIO37/GPIO38。
- **USB OTG**：Type-A 接口，支持 USB 2.0 高速 OTG。BSP 通过 `bsp_usb_host_start()` 提供 USB 主机初始化。
- **显示**：双通道 MIPI-DSI，通过 Kconfig 选择 HX8394（5 英寸，720×1280，700 Mbps/通道）、ILI9881C（7 英寸，720×1280，1000 Mbps/通道）或 JD9365（8 / 10.1 英寸，800×1280，1500 Mbps/通道）。默认选择 10.1 英寸屏幕。LCD 复位未连接，背光通过 I2C 控制。
- **触摸**：GT911，接共享 I2C 总线。BSP 尝试 `0x5D` 和 `0x14` 两个地址，采用轮询方式；复位和中断未接线。
- **音频**：ES8311 编解码器、板载模拟麦克风、扬声器接口，功放使能引脚为 GPIO53。
- **存储**：microSD，四位 SDMMC 总线，GPIO45 低电平打开卡电源，IO 供电来自 LDO 通道 4。
- **摄像头**：双通道 MIPI-CSI，通过 `esp_video` 使用；仓库示例采用 OV5647。BSP 不驱动摄像头 XCLK 和 RESET。
- **以太网**：百兆 RJ45 接口，使用 IP101GRI PHY，通过 RMII 和 SMI 连接。
- **扩展排针**：两组 2×13 排针，引出电源、I2C、UART 和 GPIO。BSP 枚举了 22 个 GPIO，其中部分与板载外设共用。
- **按键 / 指示灯**：BOOT、RESET 和电源指示灯。BSP 未注册按键（`BSP_CAPS_BUTTONS=0`）。
- **PoE**：提供兼容的选配 PoE 模块接口。
- **ESP-IDF 目标**：`esp32p4`，BSP 要求 ESP-IDF >= 5.5。

## 外设速查

| 模块 | 器件 / 功能 | 接口 | 地址 / 参数 | GPIO / 信号 |
| --- | --- | --- | --- | --- |
| LCD | MIPI-DSI 显示屏 | MIPI-DSI | 双通道；HX8394 / ILI9881C / JD9365；RGB565 或 RGB888 | 无 LCD 复位 GPIO；背光通过共享 I2C 控制 |
| 触摸 | GT911 电容触摸 | I2C | 7 位地址 0x5D 或 0x14，均会尝试 | SDA=GPIO7，SCL=GPIO8；RST/INT 未接 |
| LCD 背光 | 背光控制器 | I2C | 7 位地址 0x45；亮度寄存器 0x96，数据 0～255 | SDA=GPIO7，SCL=GPIO8 |
| 摄像头 | MIPI-CSI 摄像头接口 | MIPI-CSI + SCCB | 双通道；仓库示例使用 OV5647 | SCCB 复用 GPIO7/8；BSP 不驱动 XCLK/RESET |
| 音频 | ES8311 音频编解码器 | I2C + I2S | 7 位地址 0x18；默认 48 kHz、16 位、单声道全双工 | SDA=7，SCL=8；MCLK=13，SCLK=12，WS=10，DOUT=9，DIN=11 |
| 功放控制 | 扬声器功放使能 | GPIO | 高电平有效 | GPIO53 |
| TF 卡 | SDMMC 四位总线 | SDMMC | 槽位 0；IO 电源来自 LDO 通道 4 | CLK=43，CMD=44，D0～D3=39～42；电源控制=45，低电平有效 |
| 以太网 | IP101GRI PHY | RMII + SMI | PHY 地址 1；外部输入 50 MHz 参考时钟 | CRS_DV=28，RXD0=29，RXD1=30，MDC=31，TXD0=34，TXD1=35，TX_EN=49，REF_CLK=50，RESET=51，MDIO=52 |
| 无线协处理器 | ESP32-C5-MINI-1U | SDIO | 四位 ESP-Hosted 通信链路 | CLK=18，CMD=19，D0～D3=14～17；复位=54；C5 IO2=6 |
| USB OTG | USB 2.0 高速 OTG | USB | 内部 USB PHY；BSP 提供主机 API | Type-A 接口 |
| UART0 | 下载和串口监视 | UART | 板载串口接口 | TX=GPIO37，RX=GPIO38 |
| 按键 | BOOT / RESET | GPIO | BOOT 在启动时选择下载模式 | BOOT=GPIO35；RESET 复位开发板 |

## 引脚定义

### 扩展排针

开发板具有两组 2×13 扩展排针。以下左右位置以图中方向为准：USB Type-C 接口朝上，RJ45 网口朝下。每组排针顶部为 1、2 脚，底部为 25、26 脚。

![ESP32-P4-NANO-WIFI6-DB 扩展排针引脚定义](../../../assert/ESP32-P4-NANO-WIFI6-DB-details-inter.jpg)

<table>
  <thead>
    <tr>
      <th colspan="2">左侧排针</th>
      <th colspan="2">右侧排针</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>3V3</td><td>5V</td><td>5V</td><td>ESP_LDO_VO4</td></tr>
    <tr><td>GPIO7 / SDA</td><td>5V</td><td>GND</td><td>GND</td></tr>
    <tr><td>GPIO8 / SCL</td><td>GND</td><td>3V3</td><td>GPIO0 / XTAL_32K_N</td></tr>
    <tr><td>GPIO23</td><td>GPIO37 / UART0_TXD</td><td>GND</td><td>GPIO1 / XTAL_32K_P</td></tr>
    <tr><td>GND</td><td>GPIO38 / UART0_RXD</td><td>GPIO3</td><td>GND</td></tr>
    <tr><td>GPIO5</td><td>GPIO4</td><td>GPIO2</td><td>GPIO6</td></tr>
    <tr><td>GPIO20</td><td>GND</td><td>GPIO54</td><td>GPIO53</td></tr>
    <tr><td>GPIO21</td><td>GPIO22</td><td>GPIO47</td><td>GPIO48</td></tr>
    <tr><td>3V3</td><td>GPIO24 / USB1P1_N0</td><td>GPIO46</td><td>GND</td></tr>
    <tr><td>GPIO25 / USB1P1_P0</td><td>GND</td><td>GPIO45</td><td>C5_U0RXD</td></tr>
    <tr><td>GPIO26 / USB1P1_N1</td><td>GPIO27 / USB1P1_P1</td><td>USBD_P</td><td>C5_U0TXD</td></tr>
    <tr><td>GPIO32</td><td>GPIO33</td><td>USBD_N</td><td>C5_BOOT</td></tr>
    <tr><td>GND</td><td>GPIO36</td><td>GND</td><td>GND</td></tr>
  </tbody>
</table>

- 图中 GPIO 编号均属于 ESP32-P4；`C5_U0RXD`、`C5_U0TXD` 和 `C5_BOOT` 是 ESP32-C5 的下载信号，不属于 P4 的 UART0。
- `USBD_P`、`USBD_N` 为 USB 数据差分信号；GPIO24～GPIO27 另标有 `USB1P1_N0/P0/N1/P1` 复用功能，连接时应区分。
- `ESP_LDO_VO4` 为电源信号，不是 GPIO，也不是相邻的 5V 引脚。
- GPIO0/GPIO1 复用晶振信号 `XTAL_32K_N/P`。图中 ADC 和触摸通道标注表示对应 GPIO 的复用功能。
- GPIO7/GPIO8 与板载 I2C 总线共用；GPIO37/GPIO38 与 UART0 串口共用。GPIO45、GPIO53、GPIO54 和 GPIO6 也与板载外设共用，外设工作时不要独立驱动。

`bsp_get_header_gpios()` 返回以下 BSP 只读数组（22 项）：

```text
23, 5, 20, 21, 25, 26, 32, 4, 22, 24, 27, 33, 36, 3, 2, 54,
47, 46, 45, 6, 53, 48
```

该数组是 BSP 提供的 GPIO 枚举，不是排针的物理顺序，也不包含排针上的全部信号。GPIO0/GPIO1、GPIO7/GPIO8 和 GPIO37/GPIO38 已引出，但未纳入数组；电源、USB 专用信号和 C5 下载信号也不在其中。

### 摄像头与显示接口

摄像头 SCCB、GT911 触摸控制器和 LCD 背光共用 GPIO7/GPIO8 上的 I2C 总线。

| 接口 | 类型 | ESP32-P4 信号 |
| --- | --- | --- |
| 摄像头 | MIPI-CSI | 两条数据通道、时钟通道、共享 I2C；BSP 不驱动 XCLK/RESET |
| 显示屏 | MIPI-DSI | 两条数据通道、时钟通道、共享 I2C；LCD 复位、触摸复位和中断未连接 |

## GPIO 完整分配

下表列出全部 GPIO（0～54）的板上连接及使用注意。

| GPIO | 信号名 | 连接到 | 备注 |
| --- | --- | --- | --- |
| GPIO0 | XTAL_32K_N | 32 kHz 晶振 |
| GPIO1 | XTAL_32K_P | 32 kHz 晶振 |
| GPIO2 | GPIO2 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO3 | GPIO3 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO4 | GPIO4 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO5 | GPIO5 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO6 | C5 IO2 | ESP32-C5 IO2 |  |
| GPIO7 | I2C SDA | ES8311、GT911、背光、摄像头 SCCB 与扩展排针 | 共享 I2C 总线 |
| GPIO8 | I2C SCL | ES8311、GT911、背光、摄像头 SCCB 与扩展排针 | 共享 I2C 总线 |
| GPIO9 | I2S DOUT | ES8311 DSDIN | 音频播放数据，ESP32-P4 → 编解码器 |
| GPIO10 | I2S WS | ES8311 LRCK | 音频帧同步 |
| GPIO11 | I2S DIN | ES8311 ASDOUT | 音频采集数据，编解码器 → ESP32-P4 |
| GPIO12 | I2S SCLK | ES8311 SCLK | 音频位时钟 |
| GPIO13 | I2S MCLK | ES8311 MCLK | 音频主时钟 |
| GPIO14 | SDIO D0 | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO15 | SDIO D1 | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO16 | SDIO D2 | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO17 | SDIO D3 | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO18 | SDIO CLK | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO19 | SDIO CMD | ESP32-C5 | ESP-Hosted SDIO 通信链路 |
| GPIO20 | GPIO20 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO21 | GPIO21 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO22 | GPIO22 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO23 | GPIO23 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO24 | USB1P1_N0 | 扩展排针 | 与 USB 功能复用 |
| GPIO25 | USB1P1_P0 | 扩展排针 | 与 USB 功能复用 |
| GPIO26 | USB1P1_N1 | 扩展排针 | 与 USB 功能复用 |
| GPIO27 | USB1P1_P1 | 扩展排针 | 与 USB 功能复用 |
| GPIO28 | RMII CRS_DV | IP101GRI | PHY → ESP32-P4 |
| GPIO29 | RMII RXD0 | IP101GRI | PHY → ESP32-P4 |
| GPIO30 | RMII RXD1 | IP101GRI | PHY → ESP32-P4 |
| GPIO31 | SMI MDC | IP101GRI | ESP32-P4 → PHY |
| GPIO32 | GPIO32 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO33 | GPIO33 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO34 | RMII TXD0 | IP101GRI | ESP32-P4 → PHY |
| GPIO35 | RMII TXD1 / BOOT | IP101GRI 与 BOOT 按键 | ESP32-P4 → PHY；与启动模式选择共用 |
| GPIO36 | GPIO36 | 扩展排针 | 启动配置引脚；外部上拉 |
| GPIO37 | UART0 TX | 串口与扩展口 | 固件下载和串口监视 |
| GPIO38 | UART0 RX | 串口与扩展口 | 固件下载和串口监视 |
| GPIO39 | SD D0 | microSD 卡 | SDMMC 四位 数据线 |
| GPIO40 | SD D1 | microSD 卡 | SDMMC 四位 数据线 |
| GPIO41 | SD D2 | microSD 卡 | SDMMC 四位 数据线 |
| GPIO42 | SD D3 | microSD 卡 | SDMMC 四位 数据线 |
| GPIO43 | SD CLK | microSD 卡 | SDMMC 时钟线 |
| GPIO44 | SD CMD | microSD 卡 | SDMMC 命令线 |
| GPIO45 | SD_VDD_EN | microSD 电源控制与扩展排针 | 低电平打开 |
| GPIO46 | GPIO46 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO47 | GPIO47 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO48 | GPIO48 | 扩展排针 | `bsp_get_header_gpios()` |
| GPIO49 | RMII TX_EN | IP101GRI | ESP32-P4 → PHY |
| GPIO50 | RMII REF_CLK | IP101GRI | PHY → ESP32-P4；50 MHz 参考时钟 |
| GPIO51 | PHY RESET | IP101GRI | ESP32-P4 → PHY |
| GPIO52 | SMI MDIO | IP101GRI | 双向 PHY 管理数据 |
| GPIO53 | PA_CTRL | 扬声器功放与扩展排针 | 高电平使能 |
| GPIO54 | C5_CHIP_PU | ESP32-C5 EN 与扩展排针 | 高电平使能，低电平复位 |

## 使用注意

- GPIO7/8 共享 I2C 总线应统一管理；音频、触摸、背光和摄像头共用该总线，新增设备需确认地址。
- 外设工作期间，不要独立切换 GPIO45、GPIO53、GPIO54 或 GPIO6。
- GPIO28～31、GPIO34～35、GPIO49～52 用于以太网；GPIO35 还参与 BOOT 模式选择，外接电路需核对启动条件。
- GPIO36 是启动配置引脚；GPIO37/38 用于 UART0 下载和日志。
- GPIO0/1 与 32 kHz 晶振电路共用。官方资料将 R32/R36 标为复用前需断开晶振连接的电阻。
- GPIO24～27 存在 USB 复用功能，作普通 GPIO 使用前需确认应用的 PHY 配置。
- Arduino 的模拟输入、触摸别名只表示芯片能力，不会自动解除板上连接或占用。
- 更新 P4 应用不会同步更新 C5 从机固件，须确认 C5 镜像与无线工程的 ESP-Hosted 依赖兼容。
- 显示屏、摄像头排线应断电插拔，并根据模块说明确认连接器方向。

## 产品尺寸

板卡外形与安装孔位见[官方文档](https://docs.waveshare.net/ESP32-P4-NANO-WIFI6-DB/)。

## 相关文档

- [BSP 使用说明](../../../components/esp32_p4_nano_wifi6_db/README.md)
- [BSP API 参考](../../../components/esp32_p4_nano_wifi6_db/API.md)
- [原理图](../../../hardware/schematics/ESP32-P4-NANO-WIFI6-DB.pdf)
- [官方文档](https://docs.waveshare.net/ESP32-P4-NANO-WIFI6-DB/)
- [产品页](https://www.waveshare.net/shop/ESP32-P4-NANO-WIFI6-DB.htm)
