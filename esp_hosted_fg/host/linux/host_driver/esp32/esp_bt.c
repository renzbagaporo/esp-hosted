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
#include "esp_utils.h"
#include "esp_bt_api.h"
#include "esp_api.h"
#include "esp_kernel_port.h"

#define INVALID_HDEV_BUS (0xff)


static ESP_BT_SEND_FRAME_PROTOTYPE();

static void esp_hci_update_tx_counter(struct hci_dev *hdev, u8 pkt_type, size_t len)
{
	if (hdev) {
		if (pkt_type == HCI_COMMAND_PKT) {
			hdev->stat.cmd_tx++;
		} else if (pkt_type == HCI_ACLDATA_PKT) {
			hdev->stat.acl_tx++;
		} else if (pkt_type == HCI_SCODATA_PKT) {
			hdev->stat.sco_tx++;
		}

		hdev->stat.byte_tx += len;
	}
}

static void esp_hci_update_rx_counter(struct hci_dev *hdev, u8 pkt_type, size_t len)
{
	if (hdev) {
		if (pkt_type == HCI_EVENT_PKT) {
			hdev->stat.evt_rx++;
		} else if (pkt_type == HCI_ACLDATA_PKT) {
			hdev->stat.acl_rx++;
		} else if (pkt_type == HCI_SCODATA_PKT) {
			hdev->stat.sco_rx++;
		}

		hdev->stat.byte_rx += len;
	}
}

void esp_hci_rx(struct esp_adapter *adapter, struct sk_buff *skb)
{
	struct hci_dev *hdev = NULL;
	struct esp_payload_header *h = NULL;
	u16 offset = 0;
	u16 len = 0;
	u8 *type = NULL;
	int ret = 0;

	if (unlikely(!adapter || !skb || !skb->data || !skb->len)) {
		esp_err("Invalid args: adapter=%p, skb=%p\n", adapter, skb);
		if (skb)
			dev_kfree_skb_any(skb);
		return;
	}

	if (unlikely(atomic_read(&adapter->state) < ESP_CONTEXT_RX_READY)) {
		esp_err("Adapter being removed, dropping packet\n");
		dev_kfree_skb_any(skb);
		return;
	}

	h = (struct esp_payload_header *)skb->data;
	hdev = adapter->hcidev;

	if (unlikely(!hdev)) {
		esp_err("NULL hcidev, dropping packet\n");
		dev_kfree_skb_any(skb);
		return;
	}

	offset = le16_to_cpu(h->offset);
	len = le16_to_cpu(h->len);

	if (unlikely(!offset || !len || len > (skb->len - offset))) {
		esp_err("Invalid packet parameters: offset=%u, len=%u, skb->len=%u\n",
				offset, len, skb->len);
		dev_kfree_skb_any(skb);
		return;
	}

	if (unlikely(skb->len < offset + 1)) {
		esp_err("SKB too short: skb->len=%u, offset=%u\n", skb->len, offset);
		dev_kfree_skb_any(skb);
		return;
	}

	/* chop off the header from skb */
	skb_pull(skb, offset);

	type = skb->data;
	esp_hex_dump_dbg("bt_rx: ", skb->data, len);

	if (unlikely(skb->len <= 1)) {
		esp_err("No data after packet type byte\n");
		dev_kfree_skb_any(skb);
		return;
	}
	hci_skb_pkt_type(skb) = *type;

	skb_pull(skb, 1);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	ret = hci_recv_frame(hdev, skb);
#else
	ret = hci_recv_frame(skb);
#endif

	if (ret) {
		esp_err("Failed to process HCI frame: %d\n", ret);
		hdev->stat.err_rx++;
		dev_kfree_skb_any(skb);
	} else {
		esp_hci_update_rx_counter(hdev, *type, skb->len);
	}
}

static int esp_bt_open(struct hci_dev *hdev)
{
	return 0;
}

static int esp_bt_close(struct hci_dev *hdev)
{
	return 0;
}

static int esp_bt_flush(struct hci_dev *hdev)
{
	return 0;
}

static ESP_BT_SEND_FRAME_PROTOTYPE()
{
	struct esp_payload_header *hdr;
	size_t total_len, len;
	int ret = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
    struct hci_dev * hdev = (struct hci_dev *)(skb->dev);
#endif
	struct esp_adapter *adapter;
	struct sk_buff *new_skb;
	u8 pad_len = 0, realloc_skb = 0;
	u8 *pos = NULL;
	u8 pkt_type;

	if (!hdev || !skb) {
		esp_err("Invalid args: hdev=%p, skb=%p\n", hdev, skb);
		return -EINVAL;
	}

	adapter = hci_get_drvdata(hdev);

	if (!adapter) {
		esp_err("Invalid adapter\n");
		return -EINVAL;
	}

	len = skb->len;

	if (len == 0) {
		esp_err("Zero length SKB\n");
		return -EINVAL;
	}

	esp_hex_dump_dbg("bt_tx: ", skb->data, len);

	/* Create space for payload header */
	pad_len = sizeof(struct esp_payload_header);
	total_len = len + sizeof(struct esp_payload_header);

	/* Align buffer len */
	pad_len += SKB_DATA_ADDR_ALIGNMENT - (total_len % SKB_DATA_ADDR_ALIGNMENT);

	pkt_type = hci_skb_pkt_type(skb);

	if (skb_headroom(skb) < pad_len) {
		/* Headroom is not sufficient */
		realloc_skb = 1;
	}

	if (realloc_skb || !IS_ALIGNED((unsigned long) skb->data, SKB_DATA_ADDR_ALIGNMENT)) {
		/* Realloc SKB */
		if (skb_linearize(skb)) {
			esp_err("Failed to linearize skb\n");
			hdev->stat.err_tx++;
			return -EINVAL;
		}

		new_skb = esp_alloc_skb(skb->len + pad_len);

		if (!new_skb) {
			esp_err("Failed to allocate SKB\n");
			hdev->stat.err_tx++;
			return -ENOMEM;
		}

		pos = new_skb->data;

		pos += pad_len;

		/* Populate new SKB */
		skb_copy_from_linear_data(skb, pos, skb->len);
		skb_put(new_skb, skb->len);

		/* Replace old SKB */
		dev_kfree_skb_any(skb);
		skb = new_skb;
	} else {
		/* Realloc is not needed, Make space for interface header */
		skb_push(skb, pad_len);
	}

	hdr = (struct esp_payload_header *) skb->data;

	memset (hdr, 0, sizeof(struct esp_payload_header));

	hdr->if_type = ESP_HCI_IF;
	hdr->if_num = 0;
	hdr->len = cpu_to_le16(len);
	hdr->offset = cpu_to_le16(pad_len);
	pos = skb->data;

	/* set HCI packet type */
	*(pos + pad_len - 1) = pkt_type;

	ret = esp_send_packet(adapter, skb);

	if (ret) {
		esp_err("Failed to send packet, error: %d\n", ret);
		hdev->stat.err_tx++;
		return ret;
	} else {
		esp_hci_update_tx_counter(hdev, hdr->hci_pkt_type, skb->len);
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
static int esp_bt_setup(struct hci_dev *hdev)
{
	return 0;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
static int esp_bt_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return 0;
}
#endif

int esp_deinit_bt(struct esp_adapter *adapter)
{
	struct hci_dev *hdev = NULL;

	if (!adapter) {
		return 0;
	}

	if (!adapter->hcidev) {
		esp_info("No HCI device to deinit\n");
		return 0;
	}

	hdev = adapter->hcidev;

	if (!hdev) {
		esp_info("HCI device is NULL, nothing to deinit\n");
		return 0;
	}

	adapter->hcidev = NULL;
	hci_set_drvdata(hdev, NULL);

	hci_unregister_dev(hdev);
	msleep(50);

	hci_free_dev(hdev);

	esp_info("Bluetooth deinit success\n");
	return 0;
}

int esp_init_bt(struct esp_adapter *adapter)
{
	int ret = 0;
	struct hci_dev *hdev = NULL;

	esp_info("Init Bluetooth\n");

	if (!adapter) {
		esp_err("null adapter\n");
		return -EINVAL;
	}

	if (adapter->hcidev) {
		esp_info("hcidev already exists, deinitializing first\n");
		esp_deinit_bt(adapter);
	}

	hdev = hci_alloc_dev();

	if (!hdev) {
		esp_err("Can not allocate HCI device\n");
		return -ENOMEM;
	}

	adapter->hcidev = hdev;
	hci_set_drvdata(hdev, adapter);

	hdev->bus = INVALID_HDEV_BUS;

	if (adapter->if_type == ESP_IF_TYPE_SDIO) {
		esp_info("Setting up BT over SDIO\n");
		hdev->bus = HCI_SDIO;
	}
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
	else if (adapter->if_type == ESP_IF_TYPE_SPI) {
		esp_info("Setting up BT over SPI\n");
		hdev->bus = HCI_SPI;
	}
    #endif

	if (hdev->bus == INVALID_HDEV_BUS) {
		if (adapter->if_type == ESP_IF_TYPE_SDIO) {
			esp_err("Kernel version does not support HCI over SDIO BUS\n");
		} else if (adapter->if_type == ESP_IF_TYPE_SPI) {
			esp_err("Kernel version does not support HCI over SPI BUS\n");
		} else {
			esp_err("HCI over expected BUS[%u] is not supported\n", adapter->if_type);
		}
		hci_free_dev(hdev);
		adapter->hcidev = NULL;
		return -EINVAL;
	}

	if (adapter->dev)
		SET_HCIDEV_DEV(hdev, adapter->dev);

	hdev->open  = esp_bt_open;
	hdev->close = esp_bt_close;
	hdev->flush = esp_bt_flush;
	hdev->send  = esp_bt_send_frame;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	hdev->setup = esp_bt_setup;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	hdev->set_bdaddr = esp_bt_set_bdaddr;
#endif

#ifdef HCI_PRIMARY
	hdev->dev_type = HCI_PRIMARY;
#endif

	ret = hci_register_dev(hdev);
	if (ret < 0) {
		esp_err("Can not register HCI device, error: %d\n", ret);
		hci_free_dev(hdev);
		adapter->hcidev = NULL;
		return ret;
	}

	esp_info("Bluetooth init success\n");
	return 0;
}
