// SPDX-License-Identifier: GPL-2.0-only
/*
 * Espressif Systems Wireless LAN device driver
 *
 * Copyright (C) 2015-2021 Espressif Systems (Shanghai) PTE LTD
 *
 * This software file (the "File") is distributed by Espressif Systems (Shanghai)
 * PTE LTD under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/** prevent recursive inclusion **/
#ifndef __CTRL_CONFIG_H
#define __CTRL_CONFIG_H

#define GET_STA_MAC_ADDR                   "get_sta_mac_addr"
#define GET_SOFTAP_MAC_ADDR                "get_softap_mac_addr"
#define SET_STA_MAC_ADDR                   "set_sta_mac_addr"
#define SET_SOFTAP_MAC_ADDR                "set_softap_mac_addr"

#define GET_WIFI_MODE                      "get_wifi_mode"
#define SET_WIFI_MODE                      "set_wifi_mode"

#define GET_AP_SCAN_LIST                   "get_ap_scan_list"
#define STA_CONNECT                        "sta_connect"
#define GET_STA_CONFIG                     "get_sta_config"
#define STA_DISCONNECT                     "sta_disconnect"

#define SET_SOFTAP_VENDOR_IE               "set_softap_vendor_ie"
#define RESET_SOFTAP_VENDOR_IE             "reset_softap_vendor_ie"
#define SOFTAP_START                       "softap_start"
#define GET_SOFTAP_CONFIG                  "get_softap_config"
#define SOFTAP_CONNECTED_STA_LIST          "softap_connected_sta_list"
#define SOFTAP_STOP                        "softap_stop"

#define GET_WIFI_POWERSAVE_MODE            "get_wifi_powersave_mode"
#define SET_WIFI_POWERSAVE_MODE            "set_wifi_powersave_mode"
#define OTA                                "ota"

#define SET_WIFI_MAX_TX_POWER              "set_wifi_max_tx_power"
#define GET_WIFI_CURR_TX_POWER             "get_wifi_curr_tx_power"

#define ENABLE_WIFI                        "enable_wifi"
#define DISABLE_WIFI                       "disable_wifi"
#define ENABLE_BT                          "enable_bt"
#define DISABLE_BT                         "disable_bt"

#define GET_FW_VERSION                     "get_fw_version"
#define GET_DHCP_DNS_STATUS                "get_dhcp_dns_status"

#define SET_COUNTRY_CODE                   "set_country_code"
/* ENABLED means ieee80211d ("additional regulatory domains") enabled */
#define SET_COUNTRY_CODE_ENABLED           "set_country_code_enabled"

#define GET_COUNTRY_CODE                   "get_country_code"

#define CUSTOM_RPC_DEMO1                   "send_packed_data__only_ack"
#define CUSTOM_RPC_DEMO2                   "send_packed_data__echo_back_as_response"
#define CUSTOM_RPC_DEMO3                   "send_packed_data__echo_back_as_event"

#ifndef SSID_LENGTH
#define SSID_LENGTH                         33
#endif
#ifndef PWD_LENGTH
#define PWD_LENGTH                          65
#endif
#define CHUNK_SIZE                          4000

/* sets the band used in Station Mode to connect to the SSID
 * BAND_MODE_2G_ONLY - only look for SSID on 2.4GHz bands
 * BAND_MODE_5G_ONLY - only look for SSID on 5GHz bands
 * BAND_MODE_AUTO - look for SSID on 2.4GHz band, then 5GHz band
 */
#define BAND_MODE_2G_ONLY                   1
#define BAND_MODE_5G_ONLY                   2
#define BAND_MODE_AUTO                      3

/* station mode */
#define STATION_MODE_MAC_ADDRESS            "aa:bb:cc:dd:ee:ff"
#define STATION_MODE_SSID                   "MyWifi"
#define STATION_MODE_PWD                    "MyWifiPass@123"
#define STATION_MODE_BSSID                  ""
#define STATION_BAND_MODE                   BAND_MODE_AUTO
#define STATION_MODE_IS_WPA3_SUPPORTED      false
#define STATION_MODE_LISTEN_INTERVAL        3

/* softap mode */
#define SOFTAP_MODE_MAC_ADDRESS             "cc:bb:aa:ee:ff:dd"
#define SOFTAP_MODE_SSID                    "ESPWifi"
#define SOFTAP_MODE_PWD                     "ESPWifi@123"
#define SOFTAP_MODE_CHANNEL                 1
#define SOFTAP_MODE_ENCRYPTION_MODE         3
#define SOFTAP_MODE_MAX_ALLOWED_CLIENTS     4
#define SOFTAP_MODE_SSID_HIDDEN             false
#define SOFTAP_MODE_BANDWIDTH               2
#define SOFTAP_BAND_MODE                    BAND_MODE_AUTO

/* COUNTRY_CODE is expected to be three octets. */
/* From documentation for esp_wifi_set_country_code():
 *
 * Supported country codes are "01"(world safe mode) "AT","AU","BE","BG","BR", "CA","CH","CN","CY","CZ","DE","DK","EE","ES","FI","FR","GB","GR","HK","HR","HU", "IE","IN","IS","IT","JP","KR","LI","LT","LU","LV","MT","MX","NL","NO","NZ","PL","PT", "RO","SE","SI","SK","TW","US"
 *
 * The third octet of country code string is one of the following: ' ', 'O', 'I', 'X', otherwise it is considered as ' '.
 *
 * From ESP-IDF Wi-Fi API documentation on country code:
 * - an ASCII space character, which means the regulations under which the station/AP is operating encompass all environments for the current frequency band in the country.
 * - an ASCII ‘O’ character, which means the regulations under which the station/AP is operating are for an outdoor environment only.
 * - an ASCII ‘I’ character, which means the regulations under which the station/AP is operating are for an indoor environment only.
 * - an ASCII ‘X’ character, which means the station/AP is operating under a noncountry entity. The first two octets of the noncountry entity is two ASCII ‘XX’ characters.
 */
#define COUNTRY_CODE                        "01 " // 01 is 'world-safe'

#define INPUT_WIFI_TX_POWER                 20

#define HEARTBEAT_ENABLE                    1
#define HEARTBEAT_DURATION_SEC              20

#define TEST_DEBUG_PRINTS                   1

#endif
