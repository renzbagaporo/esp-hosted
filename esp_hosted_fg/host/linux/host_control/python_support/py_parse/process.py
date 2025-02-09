# SPDX-License-Identifier: Apache-2.0
# Copyright 2015-2022 Espressif Systems (Shanghai) PTE LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os
from commands_lib import *
from hosted_py_header import *
import re
import subprocess

os_dhcp_down = "sudo dhclient ethsta0 -r"
os_ifdown_cmd = "sudo ifconfig ethsta0 down"
os_set_mac = "sudo ifconfig ethsta0 hw ether "
os_ifup_cmd = "sudo ifconfig ethsta0 up"
os_dhcp_up = "sudo dhclient ethsta0 -v"

os_run_dhcp_server='sudo bash ./run_dhcp_server.sh'
os_ifup_softap_cmd = "sudo ifconfig ethap0 up 192.168.4.5"
os_ifdown_softap_cmd = "sudo ifconfig ethap0 down"

sta_interface = "ethsta0"
softap_interface = "ethap0"

down_hci_instance_cmd = "sudo hciconfig | grep  'Bus: SDIO\\| Bus: UART\\| Bus: SPI' | awk -F: '{print $1}' | xargs -I{} sudo hciconfig {} down"
reset_hci_instance_cmd = "sudo hciconfig | grep  'Bus: SDIO\\| Bus: UART\\| Bus: SPI' | awk -F: '{print $1}' | xargs -I{} sudo hciconfig {} reset"

heartbeat_started = False

def down_net_interface(interface):
	result = subprocess.run(["ifconfig", interface, "down"])
	if (result.returncode):
		print("ifconfig " + interface + " down FAILED");

def up_net_interface(interface, mac_addr_str):
	result = subprocess.run(["ifconfig", interface, "down"])
	if (result.returncode):
		print("ifconfig " + interface + " down FAILED");

	result = subprocess.run(["ifconfig", interface, "hw", "ether", mac_addr_str])
	if (result.returncode):
		print("ifconfig " + interface + " hw ether " + mac_addr_str + " FAILED");

	result = subprocess.run(["ifconfig", interface, "up"])
	if (result.returncode):
		print("ifconfig " + interface + " up FAILED");

def down_hci_instance():
	result = subprocess.run(down_hci_instance_cmd, shell=True)
	if (result.returncode):
		print("Failed to bring Bluetooth interface down")
	else:
		print("Bluetooth interface set down successfully")

def reset_hci_instance():
	result = subprocess.run(reset_hci_instance_cmd, shell=True)
	if (result.returncode):
		print("Failed to reset Bluetooth interface")
	else:
		print("Bluetooth interface reset successfully")

def process_is_param_missing(x):
	if not x:
		return True
	return False


def _is_mac_valid(x):
	return re.match("[0-9a-f]{2}([-:]?)[0-9a-f]{2}(\\1[0-9a-f]{2}){4}$", x.lower())


def _get_bool(x):
	if x == True or x == 'y' or x == 'yes' or x == '1':
		return True
	elif x == False or x == 'n' or x == 'no' or x == '0':
		return False
	else:
		return None


def process_init_control_lib():
	if (init_hosted_control_lib()):
		print("init hosted control lib failed")
		quit()


def process_deinit_control_lib(stop_heartbeat = False):
	if stop_heartbeat == True:
		process_heartbeat(False)
	commands_map_py_to_c.deinit_hosted_control_lib()


def process_get_mode():
	test_sync_get_wifi_mode()
	return ""


def process_set_mode(mode):
	if mode == "station":
		test_sync_set_wifi_mode_station()
	elif mode == "softap":
		test_sync_set_wifi_mode_softap()
	elif mode == "station+softap":
		test_sync_set_wifi_mode_station_softap()
	elif mode == "none":
		test_sync_set_wifi_mode_none()
	else:
		return "Invalid mode"
	return ""


def process_get_mac_addr(mode):
	if mode == "station":
		if test_sync_station_mode_get_mac_addr():
			return "\nfailed to get station mac addr"
	elif mode == "softap":
		if test_sync_softap_mode_get_mac_addr():
			return "\nfailed to get softap mac addr"
	else:
		return "\nIncorrect Wi-Fi mode"
	return ""


def process_set_mac_addr(mode, mac):

	#verify mac
	if not _is_mac_valid(mac):
		return "\nInvalid MAC " + mac

	# verify mode
	if mode == "station":
		if test_sync_station_mode_set_mac_addr_of_esp(mac):
			return "\nfailed to set station mac addr"
	elif mode == "softap":
		if test_sync_softap_mode_set_mac_addr_of_esp(mac):
			return "\nfailed to set softap mac addr"
	else:
		return "\nIncorrect Wi-Fi mode"
	return ""


def process_get_available_wifi():
	test_sync_get_available_wifi()
	return ""


def process_connect_ap(ssid, pwd, bssid, use_wpa3, listen_interval, set_dhcp, band_mode):
	ret_str = ""

	if band_mode < WIFI_BAND_MODE_2G_ONLY or band_mode > WIFI_BAND_MODE_AUTO:
		return "Invalid band_mode parameter " + get_str(band_mode)

	if test_sync_station_mode_connect(ssid, pwd, bssid, use_wpa3, listen_interval, band_mode) != SUCCESS:
		ret_str = "Failed to connect AP"
		return ret_str
	print("\n")

	if set_dhcp:
		try:
			print(os_dhcp_down)
			os.system(os_dhcp_down)
			print("\n")

			print(os_dhcp_up)
			os.system(os_dhcp_up)
			print("\n")
		except Exception as e:
			ret_str = f"Failed during DHCP operations: {e}"
			return ret_str

	ret_str = "\nConnected to " + ssid
	return ret_str


def process_get_connected_ap_info():
	test_sync_station_mode_get_info()
	return ""


def process_disconnect_ap(reset_dhcp):

	test_sync_station_mode_disconnect()
	print("\n")

	if reset_dhcp:
		print(os_dhcp_down)
		os.system(os_dhcp_down)
	return ""


def process_softap_vendor_ie(enable, data):
	if enable == "yes" or enable == 'y':
		if data == '':
			return 'Vendor IE data is expected while enabling'
		flag = True
	elif enable == "no" or enable == 'n':
		flag = False
	else:
		return 'enable takes values: ["yes" | "no"]'
	test_sync_set_vendor_specific_ie(flag, data)
	return ""


def process_start_softap(ssid, pwd, channel, sec_prot, max_conn, hide_ssid, bw, start_dhcp_server, band_mode):
	if sec_prot != "open" and pwd == "":
		return "password mandatory for security protocol"

	if sec_prot == "open":
		encr = WIFI_AUTH_MODE.WIFI_AUTH_OPEN.value
	elif sec_prot == "wpa_psk":
		encr = WIFI_AUTH_MODE.WIFI_AUTH_WPA_PSK.value
	elif sec_prot == "wpa2_psk":
		encr = WIFI_AUTH_MODE.WIFI_AUTH_WPA2_PSK.value
	elif sec_prot == "wpa_wpa2_psk":
		encr = WIFI_AUTH_MODE.WIFI_AUTH_WPA_WPA2_PSK.value
	else:
		return "Unsupported sec prot" + sec_prot

	if bw == 20:
		bw_l = WIFI_BW.WIFI_BW_HT20.value
	elif bw == 40:
		bw_l = WIFI_BW.WIFI_BW_HT40.value
	else:
		return "Unsupported bandwidth " + get_str(bw)

	if max_conn < 1 or max_conn > 10:
		return "max connections should be 1 to 10(hardware_max)"

	if band_mode < WIFI_BAND_MODE_2G_ONLY or band_mode > WIFI_BAND_MODE_AUTO:
		return "Invalid band_mode parameter " + str(band_mode) + ": value should be from " + str(WIFI_BAND_MODE_2G_ONLY) + " to " + str(WIFI_BAND_MODE_AUTO)

	if test_sync_softap_mode_start(ssid, pwd, channel, encr, max_conn, hide_ssid, bw_l, band_mode) != SUCCESS:
		ret_str = "Failed to start ESP softap"
		return ret_str
	print("\n")

	if start_dhcp_server:
		print("Running " + os_run_dhcp_server)
		exit_code = os.system(os_run_dhcp_server)
		if exit_code != 0:
			print("DHCP server (dnsmasq) not configured/running")
			print("\033[91m Please review/edit and run 'bash -x run_dhcp_server.sh for your platform' \033[0m")
		print("Running " + os_ifup_softap_cmd)
		os.system(os_ifup_softap_cmd)

	ret_str = "\nSoftAP started"
	return ret_str


def process_get_softap_info():
	test_sync_softap_mode_get_info()
	return ""


def process_softap_connected_clients_info():
	test_sync_softap_mode_connected_clients_info()
	return ""


def process_stop_softap():
	if test_sync_softap_mode_stop() != SUCCESS:
		ret_str = "Failed to stop SoftAP"
		return ret_str
	print()

	print(os_ifdown_softap_cmd)
	os.system(os_ifdown_softap_cmd)
	print()

	ret_str = "SoftAP stopped"
	return ret_str


def process_set_power_save(mode):
	if mode =="max":
		test_sync_set_wifi_power_save_mode_max()
	elif mode =="min":
		test_sync_set_wifi_power_save_mode_min()
	else:
		return "Unsupported power save mode " + mode
	return ""


def process_get_power_save():
	test_sync_get_wifi_power_save_mode()
	return ""


def process_set_wifi_max_tx_power(power):
	test_sync_wifi_set_max_tx_power(power)
	return ""


def process_wifi_curr_tx_power():
	test_sync_wifi_get_curr_tx_power()
	return ""

def process_enable_wifi():
	global sta_interface
	global softap_interface

	test_feature_enable_wifi()
	test_sync_station_mode_get_mac_addr()
	sta_mac_addr = get_mac_addr_str()
	up_net_interface(sta_interface, sta_mac_addr)

	test_sync_softap_mode_get_mac_addr()
	softap_mac_addr = get_mac_addr_str()
	up_net_interface(softap_interface, softap_mac_addr)
	return ""

def process_disable_wifi():
	global sta_interface
	global softap_interface

	test_feature_disable_wifi()
	down_net_interface(sta_interface);
	down_net_interface(softap_interface);
	return ""

def process_enable_bluetooth():
	test_feature_enable_bt()
	reset_hci_instance()
	return ""

def process_disable_bluetooth():
	test_feature_disable_bt()
	down_hci_instance()
	return ""

def process_get_fw_version():
	test_get_fw_version()
	return ""

def process_get_country_code():
	test_get_country_code()
	return ""

def process_set_country_code(country, ieee80211d_enabled):
	test_set_country_code(country, ieee80211d_enabled)
	return ""

def process_ota_update(url):
	return test_sync_ota(url)


def process_heartbeat(enable, duration = 30):
	global heartbeat_started

	if None == _get_bool(enable):
		return "Unsupported enable value"
	if _get_bool(enable) == True:
		if duration < 10 or duration > 3600:
			return "Duration should be from 10 to 3600 seconds"
		heartbeat_started = True
		test_sync_config_heartbeat(True, duration)

	elif _get_bool(enable) == False:
		if heartbeat_started == True:
			test_sync_config_heartbeat(False, duration)
		heartbeat_started = False

	else:
		return "Unsupported enable value " + enable

	return ""


def process_subscribe_event(event):
	if event == 'esp_init':
		subscribe_event_esp_init()
	elif event == 'heartbeat':
		subscribe_event_heartbeat()
	elif event == 'sta_connected_to_ap':
		subscribe_event_sta_connected_to_ap()
	elif event == 'sta_disconnect_from_ap':
		subscribe_event_sta_disconnect_from_ap()
	elif event == 'sta_connected_to_softap':
		subscribe_event_sta_connected_to_softap()
	elif event == 'sta_disconnect_from_softap':
		subscribe_event_sta_disconnect_from_softap()
	elif event == 'all':
		register_all_event_callbacks()
	else:
		return "Unsupported event " + event
	return ""


def process_unsubscribe_event(event):
	if event == 'esp_init':
		unsubscribe_event_esp_init()
	elif event == 'heartbeat':
		unsubscribe_event_heartbeat()
	elif event == 'sta_connected_to_ap':
		unsubscribe_event_sta_connected_to_ap()
	elif event == 'sta_disconnect_from_ap':
		unsubscribe_event_sta_disconnect_from_ap()
	elif event == 'sta_connected_to_softap':
		unsubscribe_event_sta_connected_to_softap()
	elif event == 'sta_disconnect_from_softap':
		unsubscribe_event_sta_disconnect_from_softap()
	elif event == 'all':
		unregister_all_event_callbacks()
	else:
		return "Unsupported event " + event
	return ""
