# ESP-Hosted: Detailed Comparison

What each solution supports, where they agree, and where they differ.

| Solution | Model | Hosts |
| --- | --- | --- |
| [esp-hosted-linux](https://github.com/espressif/esp-hosted-linux) | FullMAC &mdash; the 802.11 MAC runs on the ESP, the host registers a `cfg80211` wiphy | Linux |
| [esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu) | Custom &mdash; ESP-IDF APIs carried as protobuf RPC | MCU and Linux |

Each project's own documentation is the authority for the setup you are
building. What follows is a comparison, not a substitute.

## Bus and feature support

<table>
  <thead>
    <tr>
      <th align="left">Group</th>
      <th align="left"></th>
      <th align="center">esp-hosted-linux</th>
      <th align="center">esp-hosted-mcu</th>
    </tr>
  </thead>
  <tbody>
    <tr><td rowspan="6" valign="middle" bgcolor="#fff8e1"><b>Bus supported</b></td><td align="left"><img src="icons/sdio_only.svg" alt="SDIO"></td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left"><img src="icons/spi_only.svg" alt="SPI full duplex"></td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left"><img src="icons/spi_hd.svg" alt="SPI half duplex"></td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left"><img src="icons/uart_only.svg" alt="UART"></td><td align="center">Bluetooth only</td><td align="center">&#10003;</td></tr>
    <tr><td align="left"><img src="icons/sdio_uart.svg" alt="SDIO + UART"></td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left"><img src="icons/spi_uart.svg" alt="SPI + UART"></td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td rowspan="15" valign="middle" bgcolor="#e3f2fd"><b>Features supported</b></td><td align="left">Linux host</td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left">MCU host, including non-ESP MCUs with porting</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Native wireless interface: <code>wlan0</code> via <code>cfg80211</code></td><td align="center">&#10003;</td><td align="center"></td></tr>
    <tr><td align="left"><code>wpa_supplicant</code>, <code>NetworkManager</code></td><td align="center">&#10003;</td><td align="center"></td></tr>
    <tr><td align="left">ESP-IDF <code>esp_wifi_*()</code> APIs on the host</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Station, AP</td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Station and AP at the same time</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Interface presented to the host</td><td align="center">802.11</td><td align="center">802.3</td></tr>
    <tr><td align="left">Host and ESP share one IP address (using the <a href="https://github.com/espressif/esp-hosted-mcu/blob/main/docs/features/network-split.md">network split</a> feature)</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Bluetooth over standard HCI</td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left">802.15.4 &mdash; Thread, Zigbee</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Host sleep, the ESP stays connected</td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Co-processor OTA from the host</td><td align="center">&#10003;</td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Custom RPCs of your own</td><td align="center"></td><td align="center">&#10003;</td></tr>
    <tr><td align="left">Your own frames across the link</td><td align="center"></td><td align="center">&#10003;</td></tr>
  </tbody>
</table>

On **esp-hosted-linux** a lone UART carries Bluetooth but not Wi-Fi &mdash; pair
it with SDIO or SPI when you need both. On **esp-hosted-mcu** UART carries both,
as Hosted HCI, which is not the same as standard HCI-over-UART; it is the
lowest-pin-count option and is not intended above roughly 1 Mbit/s.

## Co-processor chipset capabilities

What each part can do, from ESP-IDF
[`soc_caps.h`](https://github.com/espressif/esp-idf/tree/master/components/soc).
The BLE column is the version the part is **certified** for, from the ESP-IDF
[Bluetooth LE introduction](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/overview.html).

| Chipset | Wi-Fi | Classic BT | BLE certified | 802.15.4 |
| --- | :---: | :---: | :---: | :---: |
| ESP32 | <img src="icons/wifi.svg" alt="Wi-Fi"> | <img src="icons/ClassicBT.svg" alt="Classic BT"> | <img src="icons/BLE5.0.svg" alt="BLE 5.0"> |  |
| ESP32-S2 | <img src="icons/wifi.svg" alt="Wi-Fi"> |  |  |  |
| ESP32-S3 | <img src="icons/wifi.svg" alt="Wi-Fi"> |  | <img src="icons/BLE5.4.svg" alt="BLE 5.4"> |  |
| ESP32-C3 | <img src="icons/wifi.svg" alt="Wi-Fi"> |  | <img src="icons/BLE5.4.svg" alt="BLE 5.4"> |  |
| ESP32-C2 | <img src="icons/wifi.svg" alt="Wi-Fi"> |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> |  |
| ESP32-C6 | <img src="icons/wifi_6.svg" alt="Wi-Fi 6"> |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> | <img src="icons/thread.svg" alt="802.15.4"> |
| ESP32-C5 | <img src="icons/wifi_dual_band.svg" alt="Wi-Fi 6 dual band"> |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> | <img src="icons/thread.svg" alt="802.15.4"> |
| ESP32-C61 | <img src="icons/wifi_6.svg" alt="Wi-Fi 6"> |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> |  |
| ESP32-H2 |  |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> | <img src="icons/thread.svg" alt="802.15.4"> |
| ESP32-H21 |  |  | <img src="icons/BLE5.3.svg" alt="BLE 5.3"> | <img src="icons/thread.svg" alt="802.15.4"> |
| ESP32-H4 |  |  | <img src="icons/BLE5.0.svg" alt="BLE 5.0"> | <img src="icons/thread.svg" alt="802.15.4"> |

## Radio support

| Radio | esp-hosted-linux | esp-hosted-mcu | Remarks |
| --- | :---: | :---: | --- |
| Wi-Fi | &#10003; | &#10003; | |
| Classic Bluetooth | &#10003; | &#10003; | ESP32 is the only co-processor with a Classic radio |
| BLE | &#10003; | &#10003; | carried as HCI |
| 802.15.4 &mdash; Thread, Zigbee | | &#10003; | |

**esp-hosted-linux** needs a Wi-Fi capable co-processor: the driver exists to
present that Wi-Fi to Linux, and Bluetooth rides alongside it.

**esp-hosted-mcu** takes the co-processor for whichever radios you need. A part
with no Wi-Fi at all &mdash; H2, H21, H4 &mdash; is a valid choice when only
Bluetooth or Thread is wanted.

## Wi-Fi features

**esp-hosted-mcu** offers a feature when the RPC surface carries it &mdash; an
ESP-IDF API with no RPC behind it fails at build or at run time.
**esp-hosted-linux** offers what its driver advertises to `cfg80211`; anything
the host stack does on its own needs nothing from the driver.

&#10003; supported &nbsp; &#10005; not supported &nbsp; &mdash; not required, the host stack provides it

| Feature | esp-hosted-linux | esp-hosted-mcu | Notes |
| --- | :---: | :---: | --- |
| 802.11 b/g/n | &#10003; | &#10003; | `Req_WifiSetProtocol(s)` |
| 802.11 ac | &#10005; | &#10005; | no ESP co-processor implements it |
| 802.11 ax, HE | &#10005; | &#10003; | `Req_WifiSetProtocols`, on a C5, C6 or C61 co-processor |
| Station | &#10003; | &#10003; |  |
| SoftAP | &#10003; | &#10003; |  |
| Station + SoftAP together | &#10005; | &#10003; | the Linux driver exposes one interface at a time |
| Scan | &#10003; | &#10003; |  |
| Open, WPA, WPA2, WPA3 | &#10003; | &#10003; | WPA3 on Linux through `NL80211_FEATURE_SAE` |
| PMF | &#10003; | &#10003; | |
| WPA2 / WPA3 Enterprise | &mdash; | &#10003; | `wpa_supplicant` handles it on Linux; `Req_Eap*` on mcu |
| DPP, Wi-Fi Easy Connect | &mdash; | &#10003; | `wpa_supplicant` handles it on Linux; `Req_SuppDpp*` on mcu |
| WPS | &mdash; | &#10005; | `wpa_supplicant` handles it on Linux; no RPC on mcu |
| Roaming: 802.11 k, v | &mdash; | &#10005; | `wpa_supplicant` handles it on Linux; no RPC on mcu |
| Roaming: 802.11 r | &#10005; | &#10005; | the Linux driver has no `update_ft_ies`; no RPC on mcu |
| iTWT | &#10005; | &#10003; | `Req_WifiStaItwt*`, needs an 802.11ax co-processor |
| FTM | &#10005; | &#10003; | `Req_WifiFtm*` |
| CSI | &#10005; | &#10003; | `Req_WifiSetCsi`, `Req_WifiSetCsiConfig` |

The Linux driver declares `ht_cap` only, with no VHT or HE, and carries no FTM,
CSI or FT handling &mdash; which settles those rows on that side.

## Hosts

| | esp-hosted-linux | esp-hosted-mcu |
| --- | --- | --- |
| Linux | any Linux platform, with porting | any Linux platform, with porting |
| Linux demonstrated on | Raspberry Pi 3, 4, 5 &middot; i.MX8M Mini | Raspberry Pi 3, 4, 5 &middot; i.MX8M Mini |
| ESP as host | &mdash; | any ESP SoC; demonstrated on ESP32-P4 with a C6 co-processor |
| Other MCUs | &mdash; | any MCU, with porting; STM32 tested |
