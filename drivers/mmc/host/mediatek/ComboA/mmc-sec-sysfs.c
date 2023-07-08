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
#include "mtk_sd.h"
#include "mmc-sec-sysfs.h"

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

/* SYSFS about eMMC info */
static struct device *mmc_sec_dev;
/* SYSFS about SD Card Detection */
static struct device *sdcard_sec_dev;
/* SYSFS about SD Card Information */
static struct device *sdinfo_sec_dev;

#define UN_LENGTH 20
static char un_buf[UN_LENGTH + 1];
static int __init un_boot_state_param(char *line)
{
	if (strlen(line) == UN_LENGTH)
		strncpy(un_buf, line, UN_LENGTH);
	else
		pr_err("%s: androidboot.un %s does not match the UN_LENGTH.\n", __func__, line);

	return 1;
}
__setup("androidboot.un=", un_boot_state_param);

static ssize_t mmc_gen_unique_number_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	ssize_t n = 0;

	n = sprintf(buf, "W%02X%02X%02X%X%02X%08X%02X\n",
			card->cid.manfid, card->cid.prod_name[0], card->cid.prod_name[1],
			card->cid.prod_name[2] >> 4, card->cid.prv, card->cid.serial,
			UNSTUFF_BITS(card->raw_cid, 8, 8));

	if (strncmp(un_buf, buf, UN_LENGTH) != 0) {
		pr_info("%s: eMMC UN mismatch\n", __func__);
		BUG_ON(1);
	}

	return n;
}

static ssize_t sdcard_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);
	bool level;

	if (cd_gpio) {
		// level - inserted : true, removed: false
		level = (host->hw->cd_level == __gpio_get_value(cd_gpio)) ? true : false;
		if (level && mmc->card) {
			pr_err("SD card inserted.\n");
			return sprintf(buf, "Insert\n");
		} else if (level && !mmc->card) {
			pr_err("SD card removed.\n");
			return sprintf(buf, "Remove\n");
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

static DEVICE_ATTR(un, 0440, mmc_gen_unique_number_show, NULL);

static DEVICE_ATTR(status, 0444, sdcard_status_show, NULL);

static DEVICE_ATTR(data, 0444, sd_cid_show, NULL);
static DEVICE_ATTR(fc, 0444, sd_health_show, NULL);
static DEVICE_ATTR(sd_count, 0444, sd_count_show, NULL);

static struct attribute *mmc_attributes[] = {
	&dev_attr_un.attr,
	NULL,
};

static struct attribute_group mmc_attr_group = {
	.attrs = mmc_attributes,
};

static struct attribute *sdcard_attributes[] = {
	&dev_attr_status.attr,
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
	struct msdc_host *host = mmc_priv(mmc);

	if (host->hw->host_function == MSDC_EMMC)
		msdc_sec_create_sysfs_group(mmc, &mmc_sec_dev,
				&mmc_attr_group, "mmc");

	if (host->hw->host_function == MSDC_SD) {
		msdc_sec_create_sysfs_group(mmc, &sdcard_sec_dev,
				&sdcard_attr_group, "sdcard");
		msdc_sec_create_sysfs_group(mmc, &sdinfo_sec_dev,
				&sdinfo_attr_group, "sdinfo");
	}
}
