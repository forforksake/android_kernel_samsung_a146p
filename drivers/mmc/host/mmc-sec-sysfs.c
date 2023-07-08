// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Specific feature
 *
 * Copyright (C) 2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *      Storage Driver <storage.sec@samsung.com>
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/sec_class.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>

#include "../core/host.h"
#include "../core/card.h"
#include "mmc-sec-sysfs.h"

#define MSDC_SD            (1)

static inline void mmc_check_error_count(struct mmc_card_error_log *err_log,
		unsigned long long *total_c_cnt, unsigned long long *total_t_cnt)
{
	int i = 0;
	//Only sbc(0,1)/cmd(2,3)/data(4,5) is checked.
	for (i = 0; i < 6; i++) {
		if (err_log[i].err_type == -EILSEQ && *total_c_cnt < MAX_CNT_U64)
			*total_c_cnt += err_log[i].count;
		if (err_log[i].err_type == -ETIMEDOUT && *total_t_cnt < MAX_CNT_U64)
			*total_t_cnt += err_log[i].count;
	}
}

/* SYSFS about SD Card Detection */
static struct device *sdcard_sec_dev;
/* SYSFS about SD Card Information */
static struct device *sdinfo_sec_dev;
/* SYSFS about SD Card error Information */
static struct device *sddata_sec_dev;

static ssize_t error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	struct mmc_card_error_log *err_log;
	u64 total_c_cnt = 0;
	u64 total_t_cnt = 0;
	int total_len = 0;
	int i = 0;
	static const char *const req_types[] = {
		"sbc  ", "cmd  ", "data ", "stop ", "busy "
	};

	if (!card) {
		total_len = snprintf(buf, PAGE_SIZE, "no card\n");
		goto out;
	}

	err_log = card->err_log;

	total_len += snprintf(buf, PAGE_SIZE,
			"type: err    status: first_issue_time:  last_issue_time:      count\n");

	/*
	 * Init err_log[]
	 * //sbc
	 * err_log[0].err_type = -EILSEQ;
	 * err_log[1].err_type = -ETIMEDOUT;
	 * ...
	 */
	for (i = 0; i < MAX_ERR_LOG_INDEX; i++) {
		strncpy(card->err_log[i].type,
			req_types[i / MAX_ERR_TYPE_INDEX], sizeof(char) * 5);
		card->err_log[i].err_type =
			(i % MAX_ERR_TYPE_INDEX == 0) ?	-EILSEQ : -ETIMEDOUT;

		total_len += snprintf(buf + total_len, PAGE_SIZE - total_len,
				"%5s:%4d 0x%08x %16llu, %16llu, %10d\n",
				err_log[i].type, err_log[i].err_type,
				err_log[i].status,
				err_log[i].first_issue_time,
				err_log[i].last_issue_time,
				err_log[i].count);
	}

	mmc_check_error_count(err_log, &total_c_cnt, &total_t_cnt);

	total_len += snprintf(buf + total_len, PAGE_SIZE - total_len,
			"GE:%d,CC:%d,ECC:%d,WP:%d,OOR:%d,CRC:%lld,TMO:%lld,"
			"HALT:%d,CQEN:%d,RPMB:%d,RST:%d\n",
			err_log[0].ge_cnt, err_log[0].cc_cnt, err_log[0].ecc_cnt,
			err_log[0].wp_cnt, err_log[0].oor_cnt, total_c_cnt, total_t_cnt,
			err_log[0].halt_cnt, err_log[0].cq_cnt, err_log[0].rpmb_cnt,
			err_log[0].hw_rst_cnt);

out:
	return total_len;
}

static ssize_t sdcard_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct device_node *np = mmc->parent->of_node;
	int cd_gpio;

	cd_gpio = of_get_named_gpio(np, "cd-gpios", 0);

	if (cd_gpio) {
		if (mmc_gpio_get_cd(mmc)) {
			if (mmc->card) {
				pr_err("SD card inserted.\n");
				return sprintf(buf, "Insert\n");
			} else {
				pr_err("SD card removed.\n");
				return sprintf(buf, "Remove\n");
			}
		} else {
			pr_err("SD slot tray Removed.\n");
			return sprintf(buf, "Notray\n");
		}
	} else {
		if (mmc->card) {
			pr_err("SD card inserted.\n");
			return sprintf(buf, "Insert\n");
		} else {
			pr_err("SD card removed.\n");
			return sprintf(buf, "Remove\n");
		}
	}
}

static ssize_t sd_cid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int len = 0;

	if (!card) {
		len = snprintf(buf, PAGE_SIZE, "no card\n");
		goto out;
	}

	len = snprintf(buf, PAGE_SIZE,
			"%08x%08x%08x%08x\n",
			card->raw_cid[0], card->raw_cid[1],
			card->raw_cid[2], card->raw_cid[3]);
out:
	return len;
}

static ssize_t sd_health_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	struct mmc_card_error_log *err_log;
	u64 total_c_cnt = 0;
	u64 total_t_cnt = 0;
	int len = 0;

	if (!card) {
		//There should be no spaces in 'No Card'(Vold Team).
		len = snprintf(buf, PAGE_SIZE, "NOCARD\n");
		goto out;
	}

	err_log = card->err_log;

	mmc_check_error_count(err_log, &total_c_cnt, &total_t_cnt);

	if (err_log[0].ge_cnt > 100 || err_log[0].ecc_cnt > 0 ||
		err_log[0].wp_cnt > 0 || err_log[0].oor_cnt > 10 ||
		total_t_cnt > 100 || total_c_cnt > 100)
		len = snprintf(buf, PAGE_SIZE, "BAD\n");
	else
		len = snprintf(buf, PAGE_SIZE, "GOOD\n");

out:
	return len;
}

/* SYSFS for service center support */
static ssize_t sd_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	struct mmc_card_error_log *err_log;
	u64 total_cnt = 0;
	int len = 0;
	int i = 0;

	if (!card) {
		len = snprintf(buf, PAGE_SIZE, "no card\n");
		goto out;
	}

	err_log = card->err_log;

	//Only sbc(0,1)/cmd(2,3)/data(4,5) is checked.
	for (i = 0; i < 6; i++) {
		if (total_cnt < MAX_CNT_U64)
			total_cnt += err_log[i].count;
	}
	len = snprintf(buf, PAGE_SIZE, "%lld\n", total_cnt);

out:
	return len;
}

static DEVICE_ATTR(err_count, 0444, error_count_show, NULL);

static DEVICE_ATTR(status, 0444, sdcard_status_show, NULL);

static DEVICE_ATTR(data, 0444, sd_cid_show, NULL);
static DEVICE_ATTR(fc, 0444, sd_health_show, NULL);
static DEVICE_ATTR(sd_count, 0444, sd_count_show, NULL);

static struct attribute *sdcard_attributes[] = {
	&dev_attr_status.attr,
	&dev_attr_err_count.attr,
	NULL,
};

static struct attribute_group sdcard_attr_group = {
	.attrs = sdcard_attributes,
};

static struct attribute *sdinfo_attributes[] = {
	&dev_attr_data.attr,
	&dev_attr_fc.attr,
	&dev_attr_sd_count.attr,
	NULL,
};

static struct attribute_group sdinfo_attr_group = {
	.attrs = sdinfo_attributes,
};

void msdc_sec_create_sysfs_group(struct mmc_host *mmc, struct device **dev,
		const struct attribute_group *dev_attr_group, const char *str)
{
	*dev = sec_device_create(NULL, str);
	if (IS_ERR(*dev))
		pr_err("%s: Failed to create device!\n", __func__);
	else {
		if (sysfs_create_group(&(*dev)->kobj, dev_attr_group))
			pr_err("%s: Failed to create %s sysfs group\n", __func__, str);
		else
			dev_set_drvdata(*dev, mmc);
	}
}

void mmc_sec_init_sysfs(struct mmc_host *mmc)
{
	if (mmc->host_function == MSDC_SD) {
		msdc_sec_create_sysfs_group(mmc, &sdcard_sec_dev,
				&sdcard_attr_group, "sdcard");
		msdc_sec_create_sysfs_group(mmc, &sdinfo_sec_dev,
				&sdinfo_attr_group, "sdinfo");
	}
}
