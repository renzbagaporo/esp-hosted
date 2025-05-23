// SPDX-License-Identifier: GPL-2.0-only
/*
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

#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include "esp_spi.h"
#include "esp_if.h"
#include "esp_api.h"
#include "esp_bt_api.h"
#include "esp_serial.h"
#include "esp_kernel_port.h"
#include "esp_stats.h"
#include "esp_fw_verify.h"

#define SPI_INITIAL_CLK_MHZ     10
#define NUMBER_1M               1000000
#define TX_RESUME_THRESHOLD     (TX_MAX_PENDING_COUNT/5)

/* ESP in sdkconfig has CONFIG_IDF_FIRMWARE_CHIP_ID entry.
 * supported values of CONFIG_IDF_FIRMWARE_CHIP_ID are - */
#define ESP_PRIV_FIRMWARE_CHIP_UNRECOGNIZED (0xff)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32        (0x0)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32S2      (0x2)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32C3      (0x5)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32S3      (0x9)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32C2      (0xC)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32C5      (0x17)
#define ESP_PRIV_FIRMWARE_CHIP_ESP32C6      (0xD)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
  /* gpio_get_value reads raw state & not aware of CS being active low */
  #define IS_CS_ASSERTED(sPiDeV) !gpio_get_value(((const struct spi_device*)sPiDeV)->cs_gpio)
#else
  /* gpiod_get_value is aware of CS being active low */
  #define IS_CS_ASSERTED(sPiDeV) gpiod_get_value(((const struct spi_device*)sPiDeV)->cs_gpiod)
#endif


static struct sk_buff * read_packet(struct esp_adapter *adapter);
static int write_packet(struct esp_adapter *adapter, struct sk_buff *skb);
static void spi_exit(void);
static void esp_spi_transaction(void);
static int spi_dev_init(struct esp_spi_context *context);
static int spi_init(void);

volatile u8 data_path = 0;
static struct esp_spi_context spi_context;
static char hardware_type = ESP_PRIV_FIRMWARE_CHIP_UNRECOGNIZED;
static atomic_t tx_pending;
u8 first_esp_bootup_over;

#if !defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
struct task_struct *spi_thread;
struct semaphore spi_sem;
#endif

static struct esp_if_ops if_ops = {
	.read		= read_packet,
	.write		= write_packet,
};

static DEFINE_MUTEX(spi_lock);

static void print_capabilities(u32 cap)
{
	esp_info("Features supported are:\n");
	if (cap & ESP_WLAN_SPI_SUPPORT)
		esp_info("\t * WLAN\n");
	if ((cap & ESP_BT_UART_SUPPORT) || (cap & ESP_BT_SPI_SUPPORT)) {
		esp_info("\t * BT/BLE\n");
		if (cap & ESP_BT_UART_SUPPORT)
			esp_info("\t   - HCI over UART\n");
		if (cap & ESP_BT_SPI_SUPPORT)
			esp_info("\t   - HCI over SPI\n");

		if ((cap & ESP_BLE_ONLY_SUPPORT) && (cap & ESP_BR_EDR_ONLY_SUPPORT))
			esp_info("\t   - BT/BLE dual mode\n");
		else if (cap & ESP_BLE_ONLY_SUPPORT)
			esp_info("\t   - BLE only\n");
		else if (cap & ESP_BR_EDR_ONLY_SUPPORT)
			esp_info("\t   - BR EDR only\n");
	}
}

static void open_data_path(void)
{
	atomic_set(&tx_pending, 0);
	msleep(200);
	data_path = OPEN_DATAPATH;
}

static void close_data_path(void)
{
	data_path = CLOSE_DATAPATH;
	msleep(200);
}

static irqreturn_t spi_data_ready_interrupt_handler(int irq, void * dev)
{
#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	if (spi_context.spi_workqueue)
		queue_work(spi_context.spi_workqueue, &spi_context.spi_work);
#else
	up(&spi_sem);
#endif
	esp_verbose("\n");
 	return IRQ_HANDLED;
 }

static irqreturn_t spi_interrupt_handler(int irq, void * dev)
{
#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	if (spi_context.spi_workqueue)
		queue_work(spi_context.spi_workqueue, &spi_context.spi_work);
#else
	up(&spi_sem);
#endif
	esp_verbose("\n");
	return IRQ_HANDLED;
}

static struct sk_buff * read_packet(struct esp_adapter *adapter)
{
	struct esp_spi_context *context;
	struct sk_buff *skb = NULL;

	if (!data_path) {
		esp_verbose("datapath not yet open\n");
		return NULL;
	}

	if (!adapter || !adapter->if_context) {
		esp_err("Invalid args\n");
		return NULL;
	}

	context = adapter->if_context;

	if (context->esp_spi_dev) {
		skb = skb_dequeue(&(context->rx_q[PRIO_Q_SERIAL]));
		if (!skb)
			skb = skb_dequeue(&(context->rx_q[PRIO_Q_BT]));
		if (!skb)
			skb = skb_dequeue(&(context->rx_q[PRIO_Q_OTHERS]));
	} else {
		esp_err("Invalid args\n");
		return NULL;
	}

	return skb;
}

static int write_packet(struct esp_adapter *adapter, struct sk_buff *skb)
{
	u32 max_pkt_size = SPI_BUF_SIZE;
	struct esp_payload_header *h = (struct esp_payload_header *) skb->data;

	if (!adapter || !adapter->if_context || !skb || !skb->data || !skb->len) {
		esp_err("Invalid args\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (skb->len > max_pkt_size) {
		esp_err("Drop pkt of len[%u] > max spi transport len[%u]\n",
				skb->len, max_pkt_size);
		dev_kfree_skb(skb);
		return -EPERM;
	}

	if (!data_path) {
		esp_verbose("datapath not yet open\n");
		dev_kfree_skb(skb);
		return -EPERM;
	}

	UPDATE_HEADER_TX_PKT_NO(h);
	if (spi_context.adapter->capabilities & ESP_CHECKSUM_ENABLED) {
		uint16_t len = le16_to_cpu(h->len);
		uint16_t offset = le16_to_cpu(h->offset);
		h->checksum = 0;
		h->checksum = cpu_to_le16(compute_checksum((uint8_t*)h, len + offset));
	}

	/* Enqueue SKB in tx_q */
	if (h->if_type == ESP_SERIAL_IF) {
		skb_queue_tail(&spi_context.tx_q[PRIO_Q_SERIAL], skb);
	} else if (h->if_type == ESP_HCI_IF) {
		skb_queue_tail(&spi_context.tx_q[PRIO_Q_BT], skb);
	} else {
		skb_queue_tail(&spi_context.tx_q[PRIO_Q_OTHERS], skb);
		atomic_inc(&tx_pending);
		if (atomic_read(&tx_pending) >= TX_MAX_PENDING_COUNT) {
			esp_tx_pause();
		}
	}

	up(&spi_sem);

	return 0;
}


/* New: Handle device reinit in separate work function */
static void esp_spi_reinit_work(struct work_struct *work)
{
	struct esp_spi_context *context = container_of(work, struct esp_spi_context, reinit_work);
	uint8_t prio_q_idx = 0;

	/* Already resetting or invalid state */
	if (atomic_read(&context->device_state) != SPI_DEVICE_RUNNING)
		return;

	atomic_set(&context->device_state, SPI_DEVICE_RESETTING);

	/* Purge all queues */
	for (prio_q_idx = 0; prio_q_idx < MAX_PRIORITY_QUEUES; prio_q_idx++) {
		skb_queue_purge(&context->tx_q[prio_q_idx]);
		skb_queue_purge(&context->rx_q[prio_q_idx]);
	}

	/* Re-init queues */
	for (prio_q_idx = 0; prio_q_idx < MAX_PRIORITY_QUEUES; prio_q_idx++) {
		skb_queue_head_init(&context->tx_q[prio_q_idx]);
		skb_queue_head_init(&context->rx_q[prio_q_idx]);
	}

	/* Remove and re-add card */
	esp_remove_card(context->adapter);
	if (esp_add_card(context->adapter)) {
		esp_err("Failed to reinit card\n");
		/* Continue anyway - device will retry */
	}

	atomic_set(&context->device_state, SPI_DEVICE_RUNNING);
}

int process_init_event(u8 *evt_buf, u8 len)
{
	u8 len_left = len, tag_len;
	u8 *pos;
	struct esp_adapter *adapter = esp_get_adapter();
	int ret = 0;
	struct fw_version *fw_p;
	int fw_version_checked = 0;

	if (!evt_buf)
		return -1;

	pos = evt_buf;

	while (len_left) {
		tag_len = *(pos + 1);
		esp_info("EVENT: %d\n", *pos);
		if (*pos == ESP_PRIV_CAPABILITY) {
			adapter->capabilities = *(pos + 2);
			print_capabilities(*(pos + 2));
		} else if (*pos == ESP_PRIV_FIRMWARE_CHIP_ID){
			hardware_type = *(pos+2);
		} else if (*pos == ESP_PRIV_TEST_RAW_TP) {
			process_test_capabilities(*(pos + 2));
		} else if (*pos == ESP_PRIV_FW_DATA) {
			fw_p = (struct fw_version *)(pos + 2);
			ret = process_fw_data(fw_p, tag_len);
			if (ret) {
				esp_err("Incompatible ESP Firmware detected\n");
				return -1;
			}
			fw_version_checked = 1;
		} else {
			esp_warn("Unsupported tag in event\n");
		}
		pos += (tag_len+2);
		len_left -= (tag_len+2);
	}

	/* TODO: abort if strict firmware check is not performed */
	if ((get_fw_check_type() == FW_CHECK_STRICT) && !fw_version_checked) {
		esp_warn("ESP Firmware version was not checked");
	}

	if ((hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32S2) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32C2) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32C3) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32C5) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32C6) &&
		(hardware_type != ESP_PRIV_FIRMWARE_CHIP_ESP32S3)) {
		esp_err("ESP board type [%d] is not recognized: aborting\n", hardware_type);
		hardware_type = ESP_PRIV_FIRMWARE_CHIP_UNRECOGNIZED;
		return -1;
	}

	if (first_esp_bootup_over) {
		/* Schedule reinit work instead of doing it here */
		schedule_work(&spi_context.reinit_work);
		return 0;
	}

	/* First bootup - do direct init */
	ret = esp_add_card(spi_context.adapter);
	if (ret) {
		spi_exit();
		esp_err("Failed to add card\n");
		return ret;
	}
	first_esp_bootup_over = 1;

	process_capabilities(adapter->capabilities);
	esp_info("Slave up event processed\n");

	return 0;
}


static int process_rx_buf(struct sk_buff *skb)
{
	struct esp_payload_header *header;
	u16 len = 0;
	u16 offset = 0;

	if (!skb)
		return -EINVAL;

	header = (struct esp_payload_header *) skb->data;

	esp_hex_dump_dbg("spi_rx: ", skb->data , min(skb->len, 32));

	if (header->if_type >= ESP_MAX_IF) {
		return -EINVAL;
	}

	len = le16_to_cpu(header->len);
	if (!len) {
		return -EINVAL;
	}

	offset = le16_to_cpu(header->offset);

	/* Validate received SKB. Check len and offset fields */
	if (offset != sizeof(struct esp_payload_header)) {
		esp_err("offset_rcv[%d] != exp[%d], drop\n",
				(int)offset, (int)sizeof(struct esp_payload_header));
		esp_hex_dump_dbg("wrong offset: ", skb->data , min(skb->len, 32));
		return -EINVAL;
	}


	len += sizeof(struct esp_payload_header);
	if (len > SPI_BUF_SIZE) {
		esp_info("len[%u] > max[%u], drop\n", len, SPI_BUF_SIZE);
		esp_hex_dump_dbg("wrong len: ", skb->data , 8);
		return -EINVAL;
	}

	/* Trim SKB to actual size */
	skb_trim(skb, len);


	if (!data_path) {
		esp_verbose("datapath closed\n");
		return -EPERM;
	}

	/* enqueue skb for read_packet to pick it */
	if (header->if_type == ESP_SERIAL_IF)
		skb_queue_tail(&spi_context.rx_q[PRIO_Q_SERIAL], skb);
	else if (header->if_type == ESP_HCI_IF)
		skb_queue_tail(&spi_context.rx_q[PRIO_Q_BT], skb);
	else
		skb_queue_tail(&spi_context.rx_q[PRIO_Q_OTHERS], skb);

	/* indicate reception of new packet */
	esp_process_new_packet_intr(spi_context.adapter);

	return 0;
}

static void esp_spi_transaction(void)
{
	struct spi_transfer trans;
	struct sk_buff *tx_skb = NULL, *rx_skb = NULL;
	u8 *rx_buf;
	int ret = 0;
	volatile int rx_pending = 0;

#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	if (!mutex_trylock(&spi_lock)) {
		if (spi_context.spi_workqueue)
			queue_work(spi_context.spi_workqueue, &spi_context.spi_work);
		return;
	}

	/* Check slave readiness */
	if (!gpio_get_value(spi_context.handshake_gpio)) {
		mutex_unlock(&spi_lock);
		/* Schedule delayed work to retry after 1ms */
		if (spi_context.spi_workqueue) {
			mod_delayed_work(spi_context.spi_workqueue,
						   &spi_context.spi_delayed_work,
						   msecs_to_jiffies(10));
		}
		return;
	}
#else
	mutex_lock(&spi_lock);
	if (!gpio_get_value(spi_context.handshake_gpio)) {
		mutex_unlock(&spi_lock);
		return;
	}

	rx_pending = gpio_get_value(spi_context.dataready_gpio);
#endif

	if (data_path) {
		tx_skb = skb_dequeue(&spi_context.tx_q[PRIO_Q_SERIAL]);
		if (!tx_skb)
			tx_skb = skb_dequeue(&spi_context.tx_q[PRIO_Q_BT]);
		if (!tx_skb)
			tx_skb = skb_dequeue(&spi_context.tx_q[PRIO_Q_OTHERS]);

		if (tx_skb && atomic_read(&tx_pending)) {
			atomic_dec(&tx_pending);
			if (atomic_read(&tx_pending) < TX_RESUME_THRESHOLD)
				esp_tx_resume();
			#if TEST_RAW_TP
				esp_raw_tp_queue_resume();
			#endif
		}
	}

	if (!rx_pending && !tx_skb) {
		mutex_unlock(&spi_lock);
		return;
	}

	memset(&trans, 0, sizeof(trans));
	trans.speed_hz = spi_context.spi_clk_mhz * NUMBER_1M;

	if (tx_skb) {
		trans.tx_buf = tx_skb->data;
	} else {
		tx_skb = esp_alloc_skb(SPI_BUF_SIZE);
		trans.tx_buf = skb_put(tx_skb, SPI_BUF_SIZE);
		memset((void*)trans.tx_buf, 0, SPI_BUF_SIZE);
	}

	rx_skb = esp_alloc_skb(SPI_BUF_SIZE);
	rx_buf = skb_put(rx_skb, SPI_BUF_SIZE);
	memset(rx_buf, 0, SPI_BUF_SIZE);
	trans.rx_buf = rx_buf;
	trans.len = SPI_BUF_SIZE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	if (hardware_type == ESP_PRIV_FIRMWARE_CHIP_ESP32) {
		trans.cs_change = 1;
	}
#endif

	ret = spi_sync_transfer(spi_context.esp_spi_dev, &trans, 1);
	if (ret) {
		dev_kfree_skb(rx_skb);
		dev_kfree_skb(tx_skb);
		mutex_unlock(&spi_lock);
		return;
	}

	if (process_rx_buf(rx_skb)) {
		dev_kfree_skb(rx_skb);
	}

	if (tx_skb) {
		dev_kfree_skb(tx_skb);
	}

	mutex_unlock(&spi_lock);

#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	/* Queue next work only if there's data or slave is ready */
	if (gpio_get_value(spi_context.dataready_gpio) ||
		!skb_queue_empty(&spi_context.tx_q[PRIO_Q_SERIAL]) ||
		!skb_queue_empty(&spi_context.tx_q[PRIO_Q_BT]) ||
		!skb_queue_empty(&spi_context.tx_q[PRIO_Q_OTHERS])) {
		if (spi_context.spi_workqueue)
			queue_work(spi_context.spi_workqueue, &spi_context.spi_work);
	}
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0))
#include <linux/platform_device.h>
static int __spi_controller_match(struct device *dev, const void *data)
{
	struct spi_controller *ctlr;
	const u16 *bus_num = data;

	ctlr = container_of(dev, struct spi_controller, dev);

	if (!ctlr) {
		return 0;
	}

	return ctlr->bus_num == *bus_num;
}

static struct spi_controller *spi_busnum_to_master(u16 bus_num)
{
	struct platform_device *pdev = NULL;
	struct spi_master *master = NULL;
	struct spi_controller *ctlr = NULL;
	struct device *dev = NULL;

	pdev = platform_device_alloc("pdev", PLATFORM_DEVID_NONE);
	pdev->num_resources = 0;
	platform_device_add(pdev);

	master = spi_alloc_host(&pdev->dev, sizeof(void *));
	if (!master) {
		pr_err("Error: failed to allocate SPI master device\n");
		platform_device_del(pdev);
		platform_device_put(pdev);
		return NULL;
	}

	dev = class_find_device(master->dev.class, NULL, &bus_num, __spi_controller_match);
	if (dev) {
		ctlr = container_of(dev, struct spi_controller, dev);
	}

	spi_master_put(master);
	platform_device_del(pdev);
	platform_device_put(pdev);

	return ctlr;
}
#endif

static int spi_dev_init(struct esp_spi_context *context)
{
	int status = 0;
	struct spi_board_info esp_board = {{0}};
	struct spi_master *master = NULL;
	struct esp_adapter *adapter = NULL;

	if (!context || !context->adapter) {
		esp_info("Null spi context or adapter\n");
		return -ENODEV;
	}

	adapter = context->adapter;

	strscpy(esp_board.modalias, "esp_spi", sizeof(esp_board.modalias));
	esp_board.max_speed_hz = context->spi_clk_mhz* NUMBER_1M;
	esp_board.mode =  adapter->mod_param.spi_mode;
	esp_board.bus_num = adapter->mod_param.spi_bus;
	esp_board.chip_select = adapter->mod_param.spi_cs;

	esp_info("Config - GPIOs: resetpin[%d] Handshake[%d] Dataready[%d]\n",
		adapter->mod_param.resetpin, context->handshake_gpio,
		context->dataready_gpio);
	esp_info("Config - SPI: clock[%dMHz] bus[%d] cs[%d] mode[%d]\n",
		context->spi_clk_mhz, esp_board.bus_num,
		esp_board.chip_select, esp_board.mode);

	master = spi_busnum_to_master(esp_board.bus_num);
	if (!master) {
		esp_err("%u Failed to obtain SPI handle for Bus[%u] CS[%u]\n",
			__LINE__, esp_board.bus_num, esp_board.chip_select);
		esp_info("** Check if SPI peripheral and extra GPIO device tree correct **\n");
		esp_info("** Please refer https://github.com/espressif/esp-hosted/blob/master/esp_hosted_fg/docs/Linux_based_host/porting_guide.md **\n");
		return -ENODEV;
	}
	set_bit(ESP_SPI_BUS_CLAIMED, &spi_context.spi_flags);

	spi_context.esp_spi_dev = spi_new_device(master, &esp_board);

	if (!spi_context.esp_spi_dev) {
		esp_err("Failed to add new SPI device\n");
		return -ENODEV;
	}
	spi_context.adapter->dev = &spi_context.esp_spi_dev->dev;

	status = spi_setup(spi_context.esp_spi_dev);

	if (status) {
		esp_err("Failed to setup new SPI device\n");
		return status;
	}

	set_bit(ESP_SPI_BUS_SET, &spi_context.spi_flags);

	status = gpio_request(context->handshake_gpio, "SPI_HANDSHAKE_PIN");

	if (status) {
		esp_err("Failed to obtain GPIO for Handshake pin, err:%d\n", status);
		return status;
	}

	status = gpio_direction_input(context->handshake_gpio);

	if (status) {
		esp_err("Failed to set GPIO direction of Handshake pin, err: %d\n", status);
		return status;
	}
	set_bit(ESP_SPI_GPIO_HS_REQUESTED, &spi_context.spi_flags);

	status = request_irq(gpio_to_irq(context->handshake_gpio), spi_interrupt_handler,
			IRQF_SHARED | IRQF_TRIGGER_RISING,
			"ESP_SPI", spi_context.esp_spi_dev);
	if (status) {
		esp_err("Failed to request IRQ for Handshake pin, err:%d\n", status);
		return status;
	}
	set_bit(ESP_SPI_GPIO_HS_IRQ_DONE, &spi_context.spi_flags);

	status = gpio_request(context->dataready_gpio, "SPI_DATA_READY_PIN");
	if (status) {
		esp_err("Failed to obtain GPIO for Data ready pin, err:%d\n", status);
		return status;
	}
	set_bit(ESP_SPI_GPIO_DR_REQUESTED, &spi_context.spi_flags);

	status = gpio_direction_input(context->dataready_gpio);
	if (status) {
		esp_err("Failed to set GPIO direction of Data ready pin\n");
		return status;
	}

	status = request_irq(gpio_to_irq(context->dataready_gpio), spi_data_ready_interrupt_handler,
			IRQF_SHARED | IRQF_TRIGGER_RISING,
			"ESP_SPI_DATA_READY", spi_context.esp_spi_dev);
	if (status) {
		esp_err("Failed to request IRQ for Data ready pin, err:%d\n", status);
		return status;
	}
	set_bit(ESP_SPI_GPIO_DR_IRQ_DONE, &spi_context.spi_flags);

	open_data_path();
	//set_bit(ESP_SPI_DATAPATH_OPEN, &spi_context.spi_flags);


	return 0;
}
#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
static inline void esp_spi_work(struct work_struct *work)
{
	esp_spi_transaction();
}
#else
static int esp_spi_thread(void *data)
{
	struct esp_spi_context *context = &spi_context;

	esp_info("esp spi thread created\n");

	while (!kthread_should_stop()) {

		if (down_interruptible(&spi_sem)) {
			esp_verbose("Failed to acquire spi_sem\n");
			msleep(10);
			continue;
		}

		if (atomic_read(&context->adapter->state) != ESP_CONTEXT_READY) {
			msleep(10);
			continue;
		}

		esp_spi_transaction();
	}
	esp_info("esp spi thread cleared\n");
	do_exit(0);
	return 0;
}
#endif

static int spi_init(void)
{
	int status = 0;
	uint8_t prio_q_idx = 0;



	/* Initialize device state */
	atomic_set(&spi_context.device_state, SPI_DEVICE_RUNNING);

	/* Init reinit work */
	INIT_WORK(&spi_context.reinit_work, esp_spi_reinit_work);
#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	esp_info("ESP: Using SPI Workqueue solution\n");

	spi_context.spi_workqueue = alloc_workqueue("ESP_SPI_WORK_QUEUE",
			WQ_UNBOUND | WQ_HIGHPRI, 0);

	if (!spi_context.spi_workqueue) {
		esp_err("spi workqueue failed to create\n");
		spi_exit();
		return -EFAULT;
	}

	INIT_WORK(&spi_context.spi_work, esp_spi_work);
	INIT_DELAYED_WORK(&spi_context.spi_delayed_work, esp_spi_work);
#else
	esp_info("ESP: Using SPI semaphore solution\n");
	sema_init(&spi_sem, 0);
	spi_thread = kthread_run(esp_spi_thread, spi_context.adapter, "esp32_spi");
	if (!spi_thread) {
		esp_err("Failed to create esp32_spi thread\n");
		return -EFAULT;
	}
#endif

	esp_info("ESP: SPI host config: GPIOs: Handshake[%u] DataReady[%u]\n",
			spi_context.handshake_gpio, spi_context.dataready_gpio);

	for (prio_q_idx=0; prio_q_idx<MAX_PRIORITY_QUEUES; prio_q_idx++) {
		skb_queue_head_init(&spi_context.tx_q[prio_q_idx]);
		skb_queue_head_init(&spi_context.rx_q[prio_q_idx]);
	}


	status = spi_dev_init(&spi_context);
	if (status) {
		spi_exit();
		esp_err("Failed Init SPI device\n");
		return status;
	}

	status = esp_serial_init((void *) spi_context.adapter);
	if (status != 0) {
		spi_exit();
		esp_err("Error initialising serial interface\n");
		return status;
	}

	atomic_set(&spi_context.adapter->state, ESP_CONTEXT_READY);

	msleep(200);

	return status;
}

static void spi_exit(void)
{
	uint8_t prio_q_idx = 0;

	atomic_set(&spi_context.adapter->state, ESP_CONTEXT_DISABLED);

	if (test_bit(ESP_SPI_GPIO_HS_IRQ_DONE, &spi_context.spi_flags)) {
		disable_irq(gpio_to_irq(spi_context.handshake_gpio));
	}

	if (test_bit(ESP_SPI_GPIO_DR_IRQ_DONE, &spi_context.spi_flags)) {
		disable_irq(gpio_to_irq(spi_context.dataready_gpio));
	}

	close_data_path();
	msleep(200);

	for (prio_q_idx=0; prio_q_idx<MAX_PRIORITY_QUEUES; prio_q_idx++) {
		skb_queue_purge(&spi_context.tx_q[prio_q_idx]);
		skb_queue_purge(&spi_context.rx_q[prio_q_idx]);
	}
#if defined(CONFIG_ESP_HOSTED_USE_WORKQUEUE)
	if (spi_context.spi_workqueue) {
		flush_workqueue(spi_context.spi_workqueue);
		destroy_workqueue(spi_context.spi_workqueue);
		spi_context.spi_workqueue = NULL;
	}
#else
	up(&spi_sem);
	if (spi_thread) {
		kthread_stop(spi_thread);
		spi_thread = NULL;
	}
#endif

	esp_remove_card(spi_context.adapter);

	if (test_bit(ESP_SPI_GPIO_HS_IRQ_DONE, &spi_context.spi_flags)) {
		free_irq(gpio_to_irq(spi_context.handshake_gpio), spi_context.esp_spi_dev);
		clear_bit(ESP_SPI_GPIO_HS_IRQ_DONE, &spi_context.spi_flags);
	}

	if (test_bit(ESP_SPI_GPIO_DR_IRQ_DONE, &spi_context.spi_flags)) {
		free_irq(gpio_to_irq(spi_context.dataready_gpio), spi_context.esp_spi_dev);
		clear_bit(ESP_SPI_GPIO_DR_IRQ_DONE, &spi_context.spi_flags);
	}

	if (test_bit(ESP_SPI_GPIO_DR_REQUESTED, &spi_context.spi_flags)) {
		gpio_free(spi_context.dataready_gpio);
		clear_bit(ESP_SPI_GPIO_DR_REQUESTED, &spi_context.spi_flags);
	}

	if (test_bit(ESP_SPI_GPIO_HS_REQUESTED, &spi_context.spi_flags)) {
		gpio_free(spi_context.handshake_gpio);
		clear_bit(ESP_SPI_GPIO_HS_REQUESTED, &spi_context.spi_flags);
	}


	if (spi_context.adapter->hcidev)
		esp_deinit_bt(spi_context.adapter);

	spi_context.adapter->dev = NULL;

	if (spi_context.esp_spi_dev) {
		spi_unregister_device(spi_context.esp_spi_dev);
		spi_context.esp_spi_dev = NULL;
		msleep(400);
	}

	memset(&spi_context, 0, sizeof(spi_context));
}

int esp_init_interface_layer(struct esp_adapter *adapter)
{
	if (!adapter) {
		esp_err("null adapter\n");
		return -EINVAL;
	}

	if ((adapter->mod_param.spi_bus       == MOD_PARAM_UNINITIALISED) ||
	    (adapter->mod_param.spi_mode      == MOD_PARAM_UNINITIALISED) ||
	    (adapter->mod_param.spi_cs        == MOD_PARAM_UNINITIALISED) ||
	    (adapter->mod_param.spi_mode      == MOD_PARAM_UNINITIALISED) ||
	    (adapter->mod_param.spi_handshake == MOD_PARAM_UNINITIALISED) ||
	    (adapter->mod_param.spi_dataready == MOD_PARAM_UNINITIALISED)) {
		esp_err("Incorrect/Uncomplete SPI config.\n\n");
		esp_err("You can use one of methods:\n[A] Use module params to pass:\n\t\t1) spi_bus=<bus_instance> \n\t\t2) spi_cs=<CS_instance> \n\t\t3) spi_mode=<1/2/3> \n\t\t4) spi_handshake=<gpio_val> \n\t\t5) spi_dataready=<gpio_val> \n\t\t6) resetpin=<gpio_val>\n[B] hardcode above params in start of main.c\n");
		return -EINVAL;
	}


	memset(&spi_context, 0, sizeof(spi_context));

	adapter->if_context = &spi_context;
	adapter->if_ops = &if_ops;
	adapter->if_type = ESP_IF_TYPE_SPI;
	spi_context.adapter = adapter;
	if (adapter->mod_param.clockspeed != MOD_PARAM_UNINITIALISED)
		spi_context.spi_clk_mhz = adapter->mod_param.clockspeed;
	else
		spi_context.spi_clk_mhz = SPI_INITIAL_CLK_MHZ;

	spi_context.handshake_gpio = adapter->mod_param.spi_handshake;
	spi_context.dataready_gpio = adapter->mod_param.spi_dataready;

	if(!gpio_is_valid(spi_context.handshake_gpio)) {
		esp_err("Couldn't configure Handshake GPIO[%u]\n", spi_context.handshake_gpio);
		return -EINVAL;
	}

	if(!gpio_is_valid(spi_context.dataready_gpio)) {
		esp_err("Couldn't configure Data_Ready GPIO[%u]\n", spi_context.dataready_gpio);
		return -EINVAL;
	}

	if(!gpio_is_valid(adapter->mod_param.resetpin)) {
		esp_err("Couldn't configure Resetpin GPIO[%u]\n", adapter->mod_param.resetpin);
		return -EINVAL;
	}

	return spi_init();
}

void esp_deinit_interface_layer(void)
{
	spi_exit();
}

int is_host_sleeping(void)
{
	/* TODO: host sleep unsupported for spi yet */
	return 0;
}
