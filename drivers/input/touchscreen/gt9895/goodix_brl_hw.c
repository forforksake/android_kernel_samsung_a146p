/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "goodix_ts_core.h"


#define GOODIX_FW_VERSION_ADDR		0x10014
#define GOODIX_IC_INFO_MAX_LEN		1024
#define GOODIX_IC_INFO_ADDR			0x10070

enum brl_request_code {
	BRL_REQUEST_CODE_CONFIG = 0x01,
	BRL_REQUEST_CODE_REF_ERR = 0x02,
	BRL_REQUEST_CODE_RESET = 0x03,
	BRL_REQUEST_CODE_CLOCK = 0x04,
};

static int brl_after_event_handler(struct goodix_ts_core *cd);

static int brl_dev_confirm(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;
	int retry = GOODIX_RETRY_3;
	u8 tx_buf[8] = {0};
	u8 rx_buf[8] = {0};

	memset(tx_buf, 0xAA, sizeof(tx_buf));
	while (retry--) {
		ret = hw_ops->write(cd, 0x10000, tx_buf, sizeof(tx_buf));
		if (ret < 0)
			return ret;

		ret = hw_ops->read(cd, 0x10000, rx_buf, sizeof(rx_buf));
		if (ret < 0)
			return ret;
		if (!memcmp(tx_buf, rx_buf, sizeof(tx_buf)))
			break;
		sec_delay(2);
	}

	if (retry < 0) {
		ret = -EINVAL;
		ts_err("device confirm failed, rx_buf:%*ph", 8, rx_buf);
	}

	ts_info("device connected");
	return ret;
}

static int brl_power_on(struct goodix_ts_core *cd, bool on)
{
	int ret = 0;

	ret = cd->plat_data->power(cd->bus->dev, on);
	if (on) {
		sec_delay(5);
		ret = brl_dev_confirm(cd);
		if (ret < 0) {
			ts_err("device not exist, power off");
			cd->plat_data->power(cd->bus->dev, 0);
			return ret;
		}
		sec_delay(GOODIX_NORMAL_RESET_DELAY_MS);
	} else {
		sec_delay(10);
	}

	ts_info("%s", on ? "on" : "off");

	return ret;
}

int brl_suspend(struct goodix_ts_core *cd)
{
	int ret = 0;
	struct goodix_ts_cmd temp_cmd;
	int retry = 6;
	u8 status;

	/* enter LP mode */
	temp_cmd.len = 5;
	temp_cmd.cmd = 0x9F;
	temp_cmd.data[0] = 1;
	ret = cd->hw_ops->send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch LP mode failed");
		return ret;
	}

	while (retry--) {
		ret = cd->hw_ops->read(cd, cd->ic_info.misc.cmd_addr, &status, 1);
		if (ret == 0 && status == 0x80)
			break;
		usleep_range(5000, 5100);
	}
	if (retry < 0) {
		ts_err("switch LP mode failed, status[%02X]", status);
		return -EINVAL;
	}

	return ret;
}

int brl_resume(struct goodix_ts_core *cd)
{
	int ret = 0;
	struct goodix_ts_cmd temp_cmd;
	int retry = 6;
	u8 status;

	/* enter NP mode */
	temp_cmd.len = 5;
	temp_cmd.cmd = 0x9F;
	temp_cmd.data[0] = 0;
	ret = cd->hw_ops->send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch NP mode failed");
		return ret;
	}

	while (retry--) {
		ret = cd->hw_ops->read(cd, cd->ic_info.misc.cmd_addr, &status, 1);
		if (ret == 0 && status == 0x80)
			break;
		usleep_range(5000, 5100);
	}
	if (retry < 0) {
		ts_err("switch NP mode failed, status[%02X]", status);
		return -EINVAL;
	}

	return ret;
}

int brl_enable_idle(struct goodix_ts_core *cd, int enable)
{
	struct goodix_ts_cmd cmd;
	int ret = 0;

	cmd.cmd = 0x8D;
	cmd.len = 5;

	if (enable)
		cmd.data[0] = 1;
	else
		cmd.data[0] = 0;

	ts_info("%s idle", enable ? "enable" : "disable");

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("failed send cmd");

	return ret;
}

int brl_sense_off(struct goodix_ts_core *cd, int sen_off)
{
	struct goodix_ts_cmd cmd;
	int ret = 0;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D)
		cmd.cmd = 0x78;
	else
		cmd.cmd = 0x77;

	cmd.len = 5;

	if (sen_off)
		cmd.data[0] = 1;
	else
		cmd.data[0] = 0;

	ts_info("sense %s", sen_off ? "off" : "on");

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("failed send cmd");

	return ret;
}

int brl_ed_enable(struct goodix_ts_core *cd, int enable)
{
	struct goodix_ts_cmd cmd;
	int ret;

	cmd.len = 5;
	cmd.cmd = GOODIX_ED_MODE_ADDR;
	cmd.data[0] = enable;

	ts_info("%d", enable);

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("send ear detect cmd failed");

	return ret;
}

#define GOODIX_POKET_MODE_ADDR			0x70
int brl_pocket_mode_enable(struct goodix_ts_core *cd, int enable)
{
	struct goodix_ts_cmd cmd;
	int ret;

	cmd.len = 5;
	cmd.cmd = GOODIX_POKET_MODE_ADDR;
	if (enable)
		cmd.data[0] = 1;
	else
		cmd.data[0] = 0;

	ts_info("%s", enable ? "enable" : "disable");

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("send pocket mode cmd failed");

	return ret;
}

#define GOODIX_GESTURE_MODE_CMD		0xA6
#define GOODIX_COORD_MODE_CMD		0xA7
int brl_gesture(struct goodix_ts_core *cd, bool enable)
{
	struct goodix_ts_cmd cmd;
	int ret;

	cmd.len = 4;
	if (enable)
		cmd.cmd = GOODIX_GESTURE_MODE_CMD;
	else
		cmd.cmd = GOODIX_COORD_MODE_CMD;

	ts_info("%s", enable ? "enable" : "disable");

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("switch gsx mode failed");

	return ret;
}

static int brl_reset(struct goodix_ts_core *cd, int delay)
{
	ts_info("chip_reset, delay %dms", delay);

	cd->plat_data->power(cd->bus->dev, 0);
	sec_delay(100);	//must delay 100ms, ensure complete power down
	cd->plat_data->power(cd->bus->dev, 1);

	sec_delay(delay);

	return 0;
}

static int brl_irq_enable(struct goodix_ts_core *cd, bool enable)
{
	if (enable && !atomic_cmpxchg(&cd->irq_enabled, 0, 1)) {
		enable_irq(cd->irq);
		goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
		ts_info("Irq enabled");
		return 0;
	}

	if (!enable && atomic_cmpxchg(&cd->irq_enabled, 1, 0)) {
		goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);
		disable_irq(cd->irq);
		ts_info("Irq disabled");
		return 0;
	}
	ts_info("warnning: irq deepth inbalance!");
	return 0;
}

static int brl_read(struct goodix_ts_core *cd, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->read(bus->dev, addr, data, len);
}

static int brl_write(struct goodix_ts_core *cd, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->write(bus->dev, addr, data, len);
}

static DEFINE_MUTEX(sponge_mutex);
static int brl_read_from_sponge(struct goodix_ts_core *cd,
		u16 offset, u8 *data, int len)
{
	int ret;
	u32 addr = cd->ic_info.sec.sponge_addr;

	mutex_lock(&sponge_mutex);
	ret = cd->hw_ops->resume(cd);
	if (ret < 0)
		ts_err("exit LPI failed");

	ret = brl_read(cd, addr + offset, data, len);
	if (ret < 0)
		ts_err("read from sponge failed");
	mutex_unlock(&sponge_mutex);
	return ret;
}

static int brl_write_to_sponge(struct goodix_ts_core *cd,
		u16 offset, u8 *data, int len)
{
	int ret;
	u32 addr = cd->ic_info.sec.sponge_addr;
	struct goodix_ts_cmd cmd;

	mutex_lock(&sponge_mutex);
	ret = cd->hw_ops->resume(cd);
	if (ret < 0)
		ts_err("exit LPI failed");

	cmd.len = 4;
	cmd.cmd = 0xF2;

	ret = brl_write(cd, addr + offset, data, len);
	if (ret < 0)
		ts_err("write to sponge failed");

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret < 0)
		ts_err("notify sponge failed");
	mutex_unlock(&sponge_mutex);
	return ret;
}

/* command ack info */
#define CMD_ACK_IDLE             0x01
#define CMD_ACK_BUSY             0x02
#define CMD_ACK_BUFFER_OVERFLOW  0x03
#define CMD_ACK_CHECKSUM_ERROR   0x04
#define CMD_ACK_OK               0x80

#define GOODIX_CMD_RETRY		6
#define CMD_ADDR				0x10174
static DEFINE_MUTEX(cmd_mutex);
static int brl_send_cmd_with_delay(struct goodix_ts_core *cd,
		struct goodix_ts_cmd *cmd, int delayms)
{
	int ret, retry, i;
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	mutex_lock(&cmd_mutex);

	cmd->state = 0;
	cmd->ack = 0;
	goodix_append_checksum(&(cmd->buf[2]), cmd->len - 2,
			CHECKSUM_MODE_U8_LE);
	ts_debug("cmd data %*ph", cmd->len, &(cmd->buf[2]));

	retry = 0;
	while (retry++ < GOODIX_CMD_RETRY) {
		ret = hw_ops->write(cd, CMD_ADDR, cmd->buf, sizeof(*cmd));
		if (ret < 0) {
			ts_err("failed write command");
			goto out;
		}
		for (i = 0; i < GOODIX_CMD_RETRY; i++) {
			/* check command result */
			ret = hw_ops->read(cd, CMD_ADDR, cmd_ack.buf, sizeof(cmd_ack));
			if (ret < 0) {
				ts_err("failed read command ack, %d", ret);
				goto out;
			}
			ts_debug("cmd ack data %*ph",
					(int)sizeof(cmd_ack), cmd_ack.buf);
			if (cmd_ack.ack == CMD_ACK_OK) {
				sec_delay(delayms);
				ret = 0;
				goto out;
			}
			if (cmd_ack.ack == CMD_ACK_BUSY ||
					cmd_ack.ack == 0x00) {
				sec_delay(1);
				continue;
			}
			if (cmd_ack.ack == CMD_ACK_BUFFER_OVERFLOW)
				sec_delay(10);
			sec_delay(1);
			break;
		}
	}
	ts_err("failed get valid cmd ack");
	ret = -EINVAL;
out:
	mutex_unlock(&cmd_mutex);
	return ret;
}

static int brl_send_cmd(struct goodix_ts_core *cd,
		struct goodix_ts_cmd *cmd)
{
	return brl_send_cmd_with_delay(cd, cmd, 20);
}

/* flash write/read interface, limit 4096 bytes */
static int package_data(int addr, int data_len, uint8_t *data_buf, uint8_t *output_buf)
{
	flash_head_info_t *head_info;

	head_info = (flash_head_info_t *)output_buf;
	head_info->address = cpu_to_le32(addr);
	head_info->length = cpu_to_le32(data_len);

	memcpy(output_buf + sizeof(flash_head_info_t), data_buf, data_len);
	head_info->checksum = cpu_to_le32(checksum16_u32(output_buf + 4, data_len + 8));
	return data_len + sizeof(flash_head_info_t);
}

static int brl_flash_cmd(struct goodix_ts_core *cd,
				unsigned char cmd, unsigned char status, int retry_count)
{
	int ret, i;
	uint8_t cmd_buf[6] = {0};
	uint8_t tmp_buf[6] = {0};
	uint16_t checksum = 0;
	int retry = 5;
	int cmd_reg = cd->ic_info.misc.cmd_addr;

	cmd_buf[0] = 0; // state
	cmd_buf[1] = 0; // ack
	cmd_buf[2] = 4; // len
	cmd_buf[3] = cmd; // len
	checksum = cmd_buf[2] + cmd_buf[3];
	cmd_buf[4] = checksum & 0xFF; // checksum_L
	cmd_buf[5] = (checksum >> 8) & 0xFF; // checksum_H

resend_cmd:
	ret = brl_write(cd, cmd_reg, cmd_buf, sizeof(cmd_buf));
	if (ret) {
		ts_err("failed send cmd 0x%x", cmd);
		return ret;
	}

	for (i = 0; i < retry_count; i++) {
		ret = brl_read(cd, cmd_reg, tmp_buf, sizeof(tmp_buf));
		if (ret) {
			ts_err("failed read cmd ack info");
			sec_delay(5);
			continue;
		}

		if ((tmp_buf[3] != cmd_buf[3]) && (--retry > 0)) {
			ts_err("command readback unequal need resend, 0x%x != 0x%x, retry %d",
			tmp_buf[3], cmd_buf[3], retry);
			goto resend_cmd;
		}

		if (tmp_buf[0] == 0) {
			sec_delay(3);
			continue;
		}

		if (tmp_buf[0] == status) {
			ts_info("get target state 0x%x", status);
			sec_delay(5);
			return 0;
		}
		ts_err("cmd error");
		return -EINVAL;
	}

	ts_err("failed get target state 0x%x != 0x%x, retry %d", tmp_buf[0], status, i);
	return -EINVAL;
}

#define FLASH_CMD_R_START               0x09
#define FLASH_CMD_W_START               0x0A
#define FLASH_CMD_RW_FINISH             0x0B

#define FLASH_CMD_STATE_READY           0x04
#define FLASH_CMD_STATE_CHECKERR        0x05
#define FLASH_CMD_STATE_DENY            0x06
#define FLASH_CMD_STATE_OKAY            0x07
static int brl_write_to_flash(struct goodix_ts_core *cd,
				int addr, unsigned char *buf, int len)
{
	int ret;
	int i;
	int package_len;
	unsigned char *pack_buf = NULL;
	unsigned char write_finish_cmd[] = { 0, 0, 4, 0x0C, 0x10, 0 };
	int fw_buffer_addr = cd->ic_info.misc.fw_buffer_addr;
	int cmd_reg = cd->ic_info.misc.cmd_addr;

	//write flash_w cmd
	ret = brl_flash_cmd(cd, FLASH_CMD_W_START, FLASH_CMD_STATE_READY, 15);
	if (ret < 0) {
		ts_err("failed enter flash write state");
		return ret;
	}

	//package data
	pack_buf = kzalloc(sizeof(flash_head_info_t) + GOODIX_CFG_MAX_SIZE, GFP_KERNEL);
	if (!pack_buf)
		goto write_end;
	package_len = package_data(addr, len, buf, pack_buf);

	//write data to switching data buffer
	for (i = 0; i < 3; i++) {
		ret = brl_write(cd, fw_buffer_addr, pack_buf, package_len);
		if (!ret)
			break;
	}
	if (ret < 0)
		ts_err("fail to write data to fw buffer!");

write_end:
	//tell write data to switching buffer finished
	ret = brl_flash_cmd(cd, FLASH_CMD_RW_FINISH, FLASH_CMD_STATE_OKAY, 50);
	if (ret < 0)
		ts_err("failed send flash finish command");

	brl_write(cd, cmd_reg, write_finish_cmd, sizeof(write_finish_cmd));
	ts_info(">>>>>>send package to finish");
	kfree(pack_buf);
	sec_delay(20);

	return ret;
}

static int brl_read_from_flash(struct goodix_ts_core *cd,
				int addr, unsigned char *buf, int len)
{
	int ret;
	unsigned char *tmp_buf;
	unsigned char write_finish_cmd[] = { 0, 0, 4, 0x0C, 0x10, 0 };
	uint32_t checksum = 0;
	int fw_buffer_addr = cd->ic_info.misc.fw_buffer_addr;
	int cmd_reg = cd->ic_info.misc.cmd_addr;
	flash_head_info_t head_info;

	tmp_buf = kzalloc(len + sizeof(flash_head_info_t), GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	head_info.address = cpu_to_le32(addr);
	head_info.length = cpu_to_le32(len);
	head_info.checksum = cpu_to_le32(checksum16_u32((uint8_t *)&head_info.address, 8));

	//write flash_w cmd
	ret = brl_flash_cmd(cd, FLASH_CMD_R_START, FLASH_CMD_STATE_READY, 15);
	if (ret) {
		ts_err("failed enter flash read state");
		goto read_end;
	}

	ret = brl_write(cd, fw_buffer_addr, (uint8_t *)&head_info, sizeof(head_info));
	if (ret) {
		ts_err("failed write flash head info");
		goto read_end;
	}

	ret = brl_flash_cmd(cd, FLASH_CMD_RW_FINISH, FLASH_CMD_STATE_OKAY, 50);
	if (ret) {
		ts_err("failed read flash ready state");
		goto read_end;
	}

	ret = brl_read(cd, fw_buffer_addr, tmp_buf, len + sizeof(flash_head_info_t));
	if (ret) {
		ts_err("failed read data len %lu", len + sizeof(flash_head_info_t));
		goto read_end;
	}
	checksum = checksum16_u32(tmp_buf + 4, len + sizeof(flash_head_info_t) - 4);
	if (checksum != le32_to_cpup((__le32 *)tmp_buf)) {
		ts_err("read back data checksum error 0x%x != 0x%x", checksum, le32_to_cpup((__le32 *)tmp_buf));
		ret = -1;
		goto read_end;
	}
	memcpy(buf, tmp_buf + sizeof(flash_head_info_t), len);
	ret = 0;
read_end:
	brl_write(cd, cmd_reg, write_finish_cmd, sizeof(write_finish_cmd));
	kfree(tmp_buf);
	sec_delay(20);
	return ret;
}

#pragma  pack(1)
struct goodix_config_head {
	union {
		struct {
			u8 panel_name[8];
			u8 fw_pid[8];
			u8 fw_vid[4];
			u8 project_name[8];
			u8 file_ver[2];
			u32 cfg_id;
			u8 cfg_ver;
			u8 cfg_time[8];
			u8 reserved[15];
			u8 flag;
			u16 cfg_len;
			u8 cfg_num;
			u16 checksum;
		};
		u8 buf[64];
	};
};
#pragma pack()

#define CONFIG_CND_LEN			4
#define CONFIG_CMD_START		0x04
#define CONFIG_CMD_WRITE		0x05
#define CONFIG_CMD_EXIT			0x06
#define CONFIG_CMD_READ_START	0x07
#define CONFIG_CMD_READ_EXIT	0x08

#define CONFIG_CMD_STATUS_PASS	0x80
#define CONFIG_CMD_WAIT_RETRY	20

static int wait_cmd_status(struct goodix_ts_core *cd,
		u8 target_status, int retry)
{
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int i, ret;

	for (i = 0; i < retry; i++) {
		ret = hw_ops->read(cd, misc->cmd_addr, cmd_ack.buf,
				sizeof(cmd_ack));
		if (!ret && cmd_ack.state == target_status) {
			ts_debug("status check pass");
			return 0;
		}
		ts_debug("cmd buf %*ph", (int)sizeof(cmd_ack), cmd_ack.buf);
		sec_delay(20);
	}

	ts_err("cmd status not ready, retry %d, ack 0x%x, status 0x%x, ret %d",
			i, cmd_ack.ack, cmd_ack.state, ret);
	return -EINVAL;
}

static int send_cfg_cmd(struct goodix_ts_core *cd,
		struct goodix_ts_cmd *cfg_cmd)
{
	int ret;

	ret = cd->hw_ops->send_cmd(cd, cfg_cmd);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d", ret);
		return ret;
	}
	ret = wait_cmd_status(cd, CONFIG_CMD_STATUS_PASS,
			CONFIG_CMD_WAIT_RETRY);
	if (ret) {
		ts_err("failed wait for fw ready for config, %d", ret);
		return ret;
	}
	return 0;
}

static int brl_send_config(struct goodix_ts_core *cd, u8 *cfg, int len)
{
	int ret;
	u8 *tmp_buf;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (len > misc->fw_buffer_max_len) {
		ts_err("config len exceed limit %d > %d",
				len, misc->fw_buffer_max_len);
		return -EINVAL;
	}

	tmp_buf = kzalloc(len, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_START;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d", ret);
		goto exit;
	}

	ts_debug("try send config to 0x%x, len %d", misc->fw_buffer_addr, len);
	ret = hw_ops->write(cd, misc->fw_buffer_addr, cfg, len);
	if (ret) {
		ts_err("failed write config data, %d", ret);
		goto exit;
	}
	ret = hw_ops->read(cd, misc->fw_buffer_addr, tmp_buf, len);
	if (ret) {
		ts_err("failed read back config data");
		goto exit;
	}

	if (memcmp(cfg, tmp_buf, len)) {
		ts_err("config data read back compare file");
		ret = -EINVAL;
		goto exit;
	}
	/* notify fw for recive config */
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_WRITE;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret)
		ts_err("failed send config data ready cmd %d", ret);

exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_EXIT;
	if (send_cfg_cmd(cd, &cfg_cmd)) {
		ts_err("failed send config write end command");
		ret = -EINVAL;
	}

	if (!ret) {
		ts_info("success send config");
		sec_delay(100);
	}

	kfree(tmp_buf);
	return ret;
}

/*
 * return: return config length on succes, other wise return < 0
 **/
static int brl_read_config(struct goodix_ts_core *cd, u8 *cfg, int size)
{
	int ret;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_config_head cfg_head;

	if (!cfg)
		return -EINVAL;

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_START;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret) {
		ts_err("failed send config read prepare command");
		return ret;
	}

	ret = hw_ops->read(cd, misc->fw_buffer_addr,
			cfg_head.buf, sizeof(cfg_head));
	if (ret) {
		ts_err("failed read config head %d", ret);
		goto exit;
	}

	if (checksum_cmp(cfg_head.buf, sizeof(cfg_head), CHECKSUM_MODE_U8_LE)) {
		ts_err("config head checksum error");
		ret = -EINVAL;
		goto exit;
	}

	cfg_head.cfg_len = le16_to_cpu(cfg_head.cfg_len);
	if (cfg_head.cfg_len > misc->fw_buffer_max_len ||
			cfg_head.cfg_len > size) {
		ts_err("cfg len exceed buffer size %d > %d", cfg_head.cfg_len,
				misc->fw_buffer_max_len);
		ret = -EINVAL;
		goto exit;
	}

	memcpy(cfg, cfg_head.buf, sizeof(cfg_head));
	ret = hw_ops->read(cd, misc->fw_buffer_addr + sizeof(cfg_head),
			cfg + sizeof(cfg_head), cfg_head.cfg_len);
	if (ret) {
		ts_err("failed read cfg pack, %d", ret);
		goto exit;
	}

	ts_info("config len %d", cfg_head.cfg_len);
	if (checksum_cmp(cfg + sizeof(cfg_head),
				cfg_head.cfg_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("config body checksum error");
		ret = -EINVAL;
		goto exit;
	}
	ts_info("success read config data: len %zu",
			cfg_head.cfg_len + sizeof(cfg_head));
exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_EXIT;
	if (send_cfg_cmd(cd, &cfg_cmd)) {
		ts_err("failed send config read finish command");
		ret = -EINVAL;
	}
	if (ret)
		return -EINVAL;
	return cfg_head.cfg_len + sizeof(cfg_head);
}

/*
 *	return: 0 for no error.
 *	GOODIX_EBUS when encounter a bus error
 *	GOODIX_ECHECKSUM version checksum error
 *	GOODIX_EVERSION  patch ID compare failed,
 *	in this case the sensorID is valid.
 */
static int brl_read_version(struct goodix_ts_core *cd,
		struct goodix_fw_version *version)
{
	int ret, i;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	u8 buf[sizeof(struct goodix_fw_version)] = {0};
	u8 temp_pid[8] = {0};

	for (i = 0; i < 3; i++) {
		ret = hw_ops->read(cd, GOODIX_FW_VERSION_ADDR, buf, sizeof(buf));
		if (ret) {
			ts_info("read fw version: %d, retry %d", ret, i);
			ret = -GOODIX_EBUS;
			sec_delay(5);
			continue;
		}

		if (!checksum_cmp(buf, sizeof(buf), CHECKSUM_MODE_U8_LE))
			break;

		ts_info("invalid fw version: checksum error!");
		ts_info("fw version:%*ph", (int)sizeof(buf), buf);
		ret = -GOODIX_ECHECKSUM;
		sec_delay(10);
	}
	if (ret) {
		ts_err("failed get valied fw version");
		return ret;
	}
	memcpy(version, buf, sizeof(*version));
	memcpy(temp_pid, version->rom_pid, sizeof(version->rom_pid));
	ts_info("rom_pid:%s", temp_pid);
	ts_info("rom_vid:%*ph", (int)sizeof(version->rom_vid),
			version->rom_vid);
	ts_info("pid:%s", version->patch_pid);
	ts_info("vid:%*ph", (int)sizeof(version->patch_vid),
			version->patch_vid);
	ts_info("sensor_id:%d", version->sensor_id);

	return 0;
}

#define LE16_TO_CPU(x)  (x = le16_to_cpu(x))
#define LE32_TO_CPU(x)  (x = le32_to_cpu(x))
static int convert_ic_info(struct goodix_ic_info *info, const u8 *data)
{
	int i;
	struct goodix_ic_info_version *version = &info->version;
	struct goodix_ic_info_feature *feature = &info->feature;
	struct goodix_ic_info_param *parm = &info->parm;
	struct goodix_ic_info_misc *misc = &info->misc;
	struct goodix_ic_info_sec *sec = &info->sec;

	info->length = le16_to_cpup((__le16 *)data);

	data += 2;
	memcpy(version, data, sizeof(*version));
	version->config_id = le32_to_cpu(version->config_id);

	data += sizeof(struct goodix_ic_info_version);
	memcpy(feature, data, sizeof(*feature));
	feature->freqhop_feature =
		le16_to_cpu(feature->freqhop_feature);
	feature->calibration_feature =
		le16_to_cpu(feature->calibration_feature);
	feature->gesture_feature =
		le16_to_cpu(feature->gesture_feature);
	feature->side_touch_feature =
		le16_to_cpu(feature->side_touch_feature);
	feature->stylus_feature =
		le16_to_cpu(feature->stylus_feature);

	data += sizeof(struct goodix_ic_info_feature);
	parm->drv_num = *(data++);
	parm->sen_num = *(data++);
	parm->button_num = *(data++);
	parm->force_num = *(data++);
	parm->active_scan_rate_num = *(data++);
	if (parm->active_scan_rate_num > MAX_SCAN_RATE_NUM) {
		ts_err("invalid scan rate num %d > %d",
				parm->active_scan_rate_num, MAX_SCAN_RATE_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->active_scan_rate_num; i++)
		parm->active_scan_rate[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->active_scan_rate_num * 2;
	parm->mutual_freq_num = *(data++);
	if (parm->mutual_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid mntual freq num %d > %d",
				parm->mutual_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->mutual_freq_num; i++)
		parm->mutual_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->mutual_freq_num * 2;
	parm->self_tx_freq_num = *(data++);
	if (parm->self_tx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid tx freq num %d > %d",
				parm->self_tx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_tx_freq_num; i++)
		parm->self_tx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_tx_freq_num * 2;
	parm->self_rx_freq_num = *(data++);
	if (parm->self_rx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid rx freq num %d > %d",
				parm->self_rx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_rx_freq_num; i++)
		parm->self_rx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_rx_freq_num * 2;
	parm->stylus_freq_num = *(data++);
	if (parm->stylus_freq_num > MAX_FREQ_NUM_STYLUS) {
		ts_err("invalid stylus freq num %d > %d",
				parm->stylus_freq_num, MAX_FREQ_NUM_STYLUS);
		return -EINVAL;
	}
	for (i = 0; i < parm->stylus_freq_num; i++)
		parm->stylus_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->stylus_freq_num * 2;
	memcpy(misc, data, sizeof(*misc));
	misc->cmd_addr = le32_to_cpu(misc->cmd_addr);
	misc->cmd_max_len = le16_to_cpu(misc->cmd_max_len);
	misc->cmd_reply_addr = le32_to_cpu(misc->cmd_reply_addr);
	misc->cmd_reply_len = le16_to_cpu(misc->cmd_reply_len);
	misc->fw_state_addr = le32_to_cpu(misc->fw_state_addr);
	misc->fw_state_len = le16_to_cpu(misc->fw_state_len);
	misc->fw_buffer_addr = le32_to_cpu(misc->fw_buffer_addr);
	misc->fw_buffer_max_len = le16_to_cpu(misc->fw_buffer_max_len);
	misc->frame_data_addr = le32_to_cpu(misc->frame_data_addr);
	misc->frame_data_head_len = le16_to_cpu(misc->frame_data_head_len);

	misc->fw_attr_len = le16_to_cpu(misc->fw_attr_len);
	misc->fw_log_len = le16_to_cpu(misc->fw_log_len);
	misc->stylus_struct_len = le16_to_cpu(misc->stylus_struct_len);
	misc->mutual_struct_len = le16_to_cpu(misc->mutual_struct_len);
	misc->self_struct_len = le16_to_cpu(misc->self_struct_len);
	misc->noise_struct_len = le16_to_cpu(misc->noise_struct_len);
	misc->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	misc->touch_data_head_len = le16_to_cpu(misc->touch_data_head_len);
	misc->point_struct_len = le16_to_cpu(misc->point_struct_len);
	LE32_TO_CPU(misc->mutual_rawdata_addr);
	LE32_TO_CPU(misc->mutual_diffdata_addr);
	LE32_TO_CPU(misc->mutual_refdata_addr);
	LE32_TO_CPU(misc->self_rawdata_addr);
	LE32_TO_CPU(misc->self_diffdata_addr);
	LE32_TO_CPU(misc->self_refdata_addr);
	LE32_TO_CPU(misc->iq_rawdata_addr);
	LE32_TO_CPU(misc->iq_refdata_addr);
	LE32_TO_CPU(misc->im_rawdata_addr);
	LE16_TO_CPU(misc->im_readata_len);
	LE32_TO_CPU(misc->noise_rawdata_addr);
	LE16_TO_CPU(misc->noise_rawdata_len);
	LE32_TO_CPU(misc->stylus_rawdata_addr);
	LE16_TO_CPU(misc->stylus_rawdata_len);
	LE32_TO_CPU(misc->noise_data_addr);
	LE32_TO_CPU(misc->esd_addr);

	data += sizeof(*misc);
	memcpy(sec, data, sizeof(*sec));
	LE16_TO_CPU(sec->ic_vendor);
	LE32_TO_CPU(sec->total_checksum);
	LE32_TO_CPU(sec->sponge_addr);
	LE16_TO_CPU(sec->sponge_len);
	return 0;
}

static void print_ic_info(struct goodix_ic_info *ic_info)
{
	struct goodix_ic_info_version *version = &ic_info->version;
	struct goodix_ic_info_feature *feature = &ic_info->feature;
	struct goodix_ic_info_param *parm = &ic_info->parm;
	struct goodix_ic_info_misc *misc = &ic_info->misc;
	struct goodix_ic_info_sec *sec = &ic_info->sec;

	ts_info("ic_info_length:                %d",
			ic_info->length);
	ts_info("info_customer_id:              0x%01X",
			version->info_customer_id);
	ts_info("info_version_id:               0x%01X",
			version->info_version_id);
	ts_info("ic_die_id:                     0x%01X",
			version->ic_die_id);
	ts_info("ic_version_id:                 0x%01X",
			version->ic_version_id);
	ts_info("config_id:                     0x%4X",
			version->config_id);
	ts_info("config_version:                0x%01X",
			version->config_version);
	ts_info("frame_data_customer_id:        0x%01X",
			version->frame_data_customer_id);
	ts_info("frame_data_version_id:         0x%01X",
			version->frame_data_version_id);
	ts_info("touch_data_customer_id:        0x%01X",
			version->touch_data_customer_id);
	ts_info("touch_data_version_id:         0x%01X",
			version->touch_data_version_id);

	ts_info("freqhop_feature:               0x%04X",
			feature->freqhop_feature);
	ts_info("calibration_feature:           0x%04X",
			feature->calibration_feature);
	ts_info("gesture_feature:               0x%04X",
			feature->gesture_feature);
	ts_info("side_touch_feature:            0x%04X",
			feature->side_touch_feature);
	ts_info("stylus_feature:                0x%04X",
			feature->stylus_feature);

	ts_info("Drv*Sen,Button,Force num:      %d x %d, %d, %d",
			parm->drv_num, parm->sen_num,
			parm->button_num, parm->force_num);

	ts_info("Cmd:                           0x%04X, %d",
			misc->cmd_addr, misc->cmd_max_len);
	ts_info("Cmd-Reply:                     0x%04X, %d",
			misc->cmd_reply_addr, misc->cmd_reply_len);
	ts_info("FW-State:                      0x%04X, %d",
			misc->fw_state_addr, misc->fw_state_len);
	ts_info("FW-Buffer:                     0x%04X, %d",
			misc->fw_buffer_addr, misc->fw_buffer_max_len);
	ts_info("Touch-Data:                    0x%04X, %d",
			misc->touch_data_addr, misc->touch_data_head_len);
	ts_info("point_struct_len:              %d",
			misc->point_struct_len);
	ts_info("mutual_rawdata_addr:           0x%04X",
			misc->mutual_rawdata_addr);
	ts_info("mutual_diffdata_addr:          0x%04X",
			misc->mutual_diffdata_addr);
	ts_info("self_rawdata_addr:             0x%04X",
			misc->self_rawdata_addr);
	ts_info("self_diffdata_addr:            0x%04X",
			misc->self_diffdata_addr);
	ts_info("stylus_rawdata_addr:           0x%04X, %d",
			misc->stylus_rawdata_addr, misc->stylus_rawdata_len);
	ts_info("esd_addr:                      0x%04X",
			misc->esd_addr);

	ts_info("[SEC] : vendor:0x%04X, ic name:0x%02X, project:0x%02X, module:0x%02X, fw ver:0x%02X",
			sec->ic_vendor, sec->ic_name_list, sec->project_id,
			sec->module_version, sec->firmware_version);
	ts_info("firmware_checksum:             0x%04x",
			sec->total_checksum);
	ts_info("[SEC] : sponge_addr:0x%04X, sponge_len:%d",
			sec->sponge_addr, sec->sponge_len);
}

static int brl_get_ic_info(struct goodix_ts_core *cd,
		struct goodix_ic_info *ic_info)
{
	int ret, i;
	u16 length = 0;
	u32 ic_addr = GOODIX_IC_INFO_ADDR;
	u8 afe_data[GOODIX_IC_INFO_MAX_LEN] = {0};
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	for (i = 0; i < GOODIX_RETRY_3; i++) {
		ret = hw_ops->read(cd, ic_addr,
				(u8 *)&length, sizeof(length));
		if (ret) {
			ts_info("failed get ic info length, %d", ret);
			sec_delay(5);
			continue;
		}
		length = le16_to_cpu(length);
		if (length >= GOODIX_IC_INFO_MAX_LEN) {
			ts_info("invalid ic info length %d, retry %d",
					length, i);
			continue;
		}

		ret = hw_ops->read(cd, ic_addr, afe_data, length);
		if (ret) {
			ts_info("failed get ic info data, %d", ret);
			sec_delay(5);
			continue;
		}
		/* judge whether the data is valid */
		if (is_risk_data((const uint8_t *)afe_data, length)) {
			ts_info("fw info data invalid");
			sec_delay(5);
			continue;
		}
		if (checksum_cmp((const uint8_t *)afe_data,
					length, CHECKSUM_MODE_U8_LE)) {
			ts_info("fw info checksum error!");
			sec_delay(5);
			continue;
		}
		break;
	}
	if (i == GOODIX_RETRY_3) {
		ts_err("failed get ic info");
		return -EINVAL;
	}

	ret = convert_ic_info(ic_info, afe_data);
	if (ret) {
		ts_err("convert ic info encounter error");
		return ret;
	}

	cd->plat_data->x_node_num = ic_info->parm.drv_num;
	cd->plat_data->y_node_num = ic_info->parm.sen_num;

	cd->plat_data->img_version_of_ic[0] = ic_info->sec.ic_vendor;
	cd->plat_data->img_version_of_ic[1] = ic_info->sec.project_id;
	cd->plat_data->img_version_of_ic[2] = ic_info->sec.module_version;
	cd->plat_data->img_version_of_ic[3] = ic_info->sec.firmware_version;

	print_ic_info(ic_info);

	/* check some key info */
	if (!ic_info->misc.cmd_addr || !ic_info->misc.fw_buffer_addr ||
			!ic_info->misc.touch_data_addr) {
		ts_err("cmd_addr fw_buf_addr and touch_data_addr is null");
		return -EINVAL;
	}

	return 0;
}

#define GOODIX_ESD_TICK_WRITE_DATA		0xFF
#define GOODIX_ESD_TICK_WRITE_DATA_BD	0xAA

static int brl_esd_check(struct goodix_ts_core *cd)
{
	int ret;
	u32 esd_addr;
	u8 esd_value;
	u8 check_value = GOODIX_ESD_TICK_WRITE_DATA;

	if (!cd->ic_info.misc.esd_addr)
		return 0;

	esd_addr = cd->ic_info.misc.esd_addr;
	ret = cd->hw_ops->read(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed get esd value, %d", ret);
		return ret;
	}

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D)
		check_value = GOODIX_ESD_TICK_WRITE_DATA_BD;

	if (esd_value == check_value) {
		ts_err("esd check failed, 0x%x", esd_value);
		return -EINVAL;
	}
	esd_value = check_value;
	ret = cd->hw_ops->write(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed refrash esd value");
		return ret;
	}
	return 0;
}

#define GOODIX_TOUCH_EVENT			0x80
#define GOODIX_CMD_RAWDATA	2
#define GOODIX_CMD_COORD	0
static int brl_get_capacitance_data(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	int ret;
	int retry = 20;
	struct goodix_ts_cmd temp_cmd;
	u32 flag_addr = cd->ic_info.misc.touch_data_addr;
	u32 raw_addr = cd->ic_info.misc.mutual_rawdata_addr;
	u32 diff_addr = cd->ic_info.misc.mutual_diffdata_addr;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	int size = tx * rx;
	u8 val;

	if (!info) {
		ts_err("input null ptr");
		return -EIO;
	}

	/* disable irq & close esd */
	brl_irq_enable(cd, false);

	/* switch rawdata mode */
	temp_cmd.cmd = GOODIX_CMD_RAWDATA;
	temp_cmd.len = 4;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch rawdata mode failed, exit!");
		goto exit;
	}

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit!");
		goto exit;
	}

	while (retry--) {
		sec_delay(5);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("rawdata is not ready val:0x%02x, exit!", val);
		goto exit;
	}

	/* obtain rawdata & diff_rawdata */
	info->buff[0] = rx;
	info->buff[1] = tx;
	info->used_size = 2;

	ret = brl_read(cd, raw_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian raw_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	ret = brl_read(cd, diff_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian diff_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

exit:
	/* switch coor mode */
	temp_cmd.cmd = GOODIX_CMD_COORD;
	temp_cmd.len = 4;
	brl_send_cmd(cd, &temp_cmd);
	/* clean touch event flag */
	val = 0;
	brl_write(cd, flag_addr, &val, 1);
	/* enable irq & esd */
	brl_irq_enable(cd, true);
	return ret;
}

#define IRQ_EVENT_HEAD_LEN			8
#define BYTES_PER_POINT				16
#define COOR_DATA_CHECKSUM_SIZE		2

#define GOODIX_TOUCH_EVENT			0x80
#define GOODIX_REQUEST_EVENT		0x40
#define GOODIX_GESTURE_EVENT		0x20
#define GOODIX_HOTKNOT_EVENT		0x10
#define POINT_TYPE_STYLUS_HOVER		0x01
#define POINT_TYPE_STYLUS			0x03

static void goodix_parse_finger(struct goodix_ts_core *cd, unsigned int tid, u8 *buf)
{
	struct sec_ts_plat_data *pdata = cd->plat_data;
	u8 touch_type;

	if (cd->debug_flag & GOODIX_TS_DEBUG_PRINT_ALLEVENT) {
		ts_info("%s : ALL(%d): %02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, tid, buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);
	}

	pdata->prev_coord[tid] = pdata->coord[tid];

	touch_type = (buf[7] >> 6) + ((buf[6] & 0xC0) >> 4);

	pdata->coord[tid].id = tid;
	pdata->coord[tid].ttype = touch_type;
	pdata->coord[tid].action = (buf[0] & 0xC0) >> 6;
	pdata->coord[tid].x = buf[1] << 4 | (buf[3] & 0xF0) >> 4;
	pdata->coord[tid].y = buf[2] << 4 | (buf[3] & 0x0F);
	pdata->coord[tid].major = buf[4];
	pdata->coord[tid].minor = buf[5];
	pdata->coord[tid].z = buf[6] & 0x3F;
//	pdata->coord[tid].max_energy_flag = (buf[7] >> 5) & 0x01;
//	pdata->coord[tid].max_strength = max(pdata->coord[tid].max_strength, p_event_coord->max_strength);
//	pdata->coord[tid].noise_level = max(pdata->coord[tid].noise_level,
//							p_event_coord->noise_level);
//	pdata->coord[tid].hover_id_num = max(pdata->coord[tid].hover_id_num,
//							(u8)p_event_coord->hover_id_num);

	if (!pdata->coord[tid].palm && (pdata->coord[tid].ttype == SEC_TS_TOUCHTYPE_PALM))
		pdata->coord[tid].palm_count++;

	pdata->coord[tid].palm = (touch_type == SEC_TS_TOUCHTYPE_PALM);
	if (pdata->coord[tid].palm)
		pdata->palm_flag |= (1 << tid);
	else
		pdata->palm_flag &= ~(1 << tid);

	if (pdata->coord[tid].z <= 0)
		pdata->coord[tid].z = 1;

	cd->ts_event.event_type |= EVENT_TOUCH;
}

static void goodix_ts_report_finger(struct goodix_ts_core *cd, unsigned int tid)
{
	int i;
	int recal_tc = 0;
	static int prev_tc;

	sec_input_coord_event(cd->bus->dev, tid);

	ts_debug("press_cnt: %d", cd->plat_data->touch_count);

//	for checking total touch count
	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (cd->plat_data->coord[i].action == SEC_TS_COORDINATE_ACTION_PRESS ||
				cd->plat_data->coord[i].action == SEC_TS_COORDINATE_ACTION_MOVE) {
			recal_tc++;
		}
	}

	if (recal_tc != cd->plat_data->touch_count && prev_tc != cd->plat_data->touch_count)
		ts_err("recal_tc:%d != tc:%d", recal_tc, cd->plat_data->touch_count);

	prev_tc = cd->plat_data->touch_count;
}

static void goodix_parse_status(struct goodix_ts_core *cd, u8 *buf)
{
	struct goodix_ts_event *ts_event = &cd->ts_event;

	ts_event->status_type = (buf[0] >> 2) & 0x0F;
	ts_event->status_id = buf[1];
	ts_event->status_data[0] = buf[2];
	ts_event->status_data[1] = buf[3];
	ts_event->status_data[2] = buf[4];
	ts_event->status_data[3] = buf[5];
	ts_event->status_data[4] = buf[6];
	ts_event->event_type |= EVENT_STATUS;

	ts_info("status : type(%d), id(%d), data[0x%X 0x%X 0x%X 0x%X 0x%X]",
			ts_event->status_type, ts_event->status_id,
			ts_event->status_data[0], ts_event->status_data[1],
			ts_event->status_data[2], ts_event->status_data[3],
			ts_event->status_data[4]);
}

static void goodix_ts_report_status(struct goodix_ts_core *cd, struct goodix_ts_event *ts_event)
{
	if (ts_event->status_type == TYPE_STATUS_EVENT_INFO) {
		if (ts_event->status_id == SEC_TS_ACK_WET_MODE) {
			cd->plat_data->wet_mode = ts_event->status_data[0];
			ts_info("wet mode changed[%d]", cd->plat_data->wet_mode);
			if (cd->plat_data->wet_mode)
				cd->plat_data->hw_param.wet_count++;
		}
	} else if (ts_event->status_type == TYPE_STATUS_EVENT_VENDOR_INFO) {
		if (ts_event->status_id == STATUS_EVENT_VENDOR_PROXIMITY) {
			cd->ts_event.hover_event = ts_event->status_data[0];
			sec_input_proximity_report(cd->bus->dev, ts_event->status_data[0]);
		} else if (ts_event->status_id == STATUS_EVENT_VENDOR_STATE_CHANGED) {
			if (ts_event->status_data[0] == 2 && ts_event->status_data[1] == 2) {
				ts_info("Normal changed");
			} else if (ts_event->status_data[0] == 5 && ts_event->status_data[1] == 2) {
				ts_info("lp changed");
			} else if (ts_event->status_data[0] == 6) {
				ts_info("sleep changed");
			}
		} else if (ts_event->status_id == STATUS_EVENT_VENDOR_ACK_NOISE_STATUS_NOTI) {
			cd->plat_data->touch_noise_status = ts_event->status_data[0];
			ts_info("TSP NOISE MODE %s[%02x %02x]", cd->plat_data->touch_noise_status ? "ON" : "OFF",
					ts_event->status_data[1], ts_event->status_data[2]);

			if (cd->plat_data->touch_noise_status)
				cd->plat_data->hw_param.noise_count++;
		} else if (ts_event->status_id == STATUS_EVENT_VENDOR_ACK_PRE_NOISE_STATUS_NOTI) {
			cd->plat_data->touch_pre_noise_status = ts_event->status_data[0];
			ts_info("TSP PRE NOISE MODE %s", cd->plat_data->touch_pre_noise_status ? "ON" : "OFF");
		} else if (ts_event->status_id == STATUS_EVENT_VENDOR_ACK_CHARGER_STATUS_NOTI) {
			ts_info("TSP CHARGER MODE %d", ts_event->status_data[0]);
		}
	}
}

static void goodix_parse_gesture(struct goodix_ts_core *cd, u8 *buf)
{
	struct goodix_ts_event *ts_event = &cd->ts_event;

	ts_event->gesture_type = (buf[0] >> 2) & 0x0F;
	ts_event->gesture_id = buf[1];
	ts_event->gesture_data[0] = buf[2];
	ts_event->gesture_data[1] = buf[3];
	ts_event->gesture_data[2] = buf[4];
	ts_event->gesture_data[3] = buf[5];
	ts_event->event_type |= EVENT_GESTURE;

	ts_info("gesture : type(%d), id(%d), data[0x%X 0x%X 0x%X 0x%X]",
			ts_event->gesture_type, ts_event->gesture_id,
			ts_event->gesture_data[0], ts_event->gesture_data[1],
			ts_event->gesture_data[2], ts_event->gesture_data[3]);
}

static void goodix_ts_report_gesture(struct goodix_ts_core *cd, struct goodix_ts_event *ts_event)
{
	switch (ts_event->gesture_type) {
	case SEC_TS_GESTURE_CODE_SWIPE:
		ts_event->scrub_id = SPONGE_EVENT_TYPE_SPAY;
		sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id, 0, 0);
		break;
	case SEC_TS_GESTURE_CODE_DOUBLE_TAP:
		if (ts_event->gesture_id == SEC_TS_GESTURE_ID_AOD) {
			ts_event->scrub_id = SPONGE_EVENT_TYPE_AOD_DOUBLETAB;
			ts_event->scrub_x = (ts_event->gesture_data[0] << 4) | (ts_event->gesture_data[2] >> 4);
			ts_event->scrub_y = (ts_event->gesture_data[1] << 4) | (ts_event->gesture_data[2] & 0x0F);
			sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id,
							ts_event->scrub_x, ts_event->scrub_y);
		} else if (ts_event->gesture_id == SEC_TS_GESTURE_ID_DOUBLETAP_TO_WAKEUP) {
			ts_info("AOT");
			input_report_key(cd->input_dev, KEY_WAKEUP, 1);
			input_sync(cd->input_dev);
			input_report_key(cd->input_dev, KEY_WAKEUP, 0);
			input_sync(cd->input_dev);
		}
		break;
	case SEC_TS_GESTURE_CODE_SINGLE_TAP:
		ts_event->scrub_id = SPONGE_EVENT_TYPE_SINGLE_TAP;
		ts_event->scrub_x = (ts_event->gesture_data[0] << 4) | (ts_event->gesture_data[2] >> 4);
		ts_event->scrub_y = (ts_event->gesture_data[1] << 4) | (ts_event->gesture_data[2] & 0x0F);
		sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id,
						ts_event->scrub_x, ts_event->scrub_y);
		break;
	case SEC_TS_GESTURE_CODE_PRESS:
		// #0 : long press, #1 : press, #2 : release, #3 : out of area
		if (ts_event->gesture_id == 0 || ts_event->gesture_id == 1) {
			ts_event->scrub_id = SPONGE_EVENT_TYPE_FOD_PRESS;
			ts_event->scrub_x = (ts_event->gesture_data[0] << 4) | (ts_event->gesture_data[2] >> 4);
			ts_event->scrub_y = (ts_event->gesture_data[1] << 4) | (ts_event->gesture_data[2] & 0x0F);
			sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id,
							ts_event->scrub_x, ts_event->scrub_y);
			ts_info("FOD %sPRESS", ts_event->gesture_id ? "" : "LONG");
		} else if (ts_event->gesture_id == 2) {
			ts_event->scrub_id = SPONGE_EVENT_TYPE_FOD_RELEASE;
			ts_event->scrub_x = (ts_event->gesture_data[0] << 4) | (ts_event->gesture_data[2] >> 4);
			ts_event->scrub_y = (ts_event->gesture_data[1] << 4) | (ts_event->gesture_data[2] & 0x0F);
			sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id,
							ts_event->scrub_x, ts_event->scrub_y);
		} else if (ts_event->gesture_id == 3) {
			ts_event->scrub_id = SPONGE_EVENT_TYPE_FOD_OUT;
			ts_event->scrub_x = (ts_event->gesture_data[0] << 4) | (ts_event->gesture_data[2] >> 4);
			ts_event->scrub_y = (ts_event->gesture_data[1] << 4) | (ts_event->gesture_data[2] & 0x0F);
			sec_input_gesture_report(cd->bus->dev, ts_event->scrub_id,
							ts_event->scrub_x, ts_event->scrub_y);
		} else if (ts_event->gesture_id == 4) {
			ts_info("FOD VI");
		}
		break;
	default:
		ts_info("unsupported gesture:%x", ts_event->gesture_type);
		break;
	}
}

static int brl_event_handler(struct goodix_ts_core *cd, struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	u8 buffer[IRQ_EVENT_HEAD_LEN + BYTES_PER_POINT * 16 + 2];
	int pre_read_len;	// read 2 finger data at first time
	int event_num;
	int ret;
	int retry = 2;

restart:
	pre_read_len = IRQ_EVENT_HEAD_LEN + BYTES_PER_POINT * 2 + COOR_DATA_CHECKSUM_SIZE;
	ret = hw_ops->read(cd, misc->touch_data_addr, buffer, pre_read_len);
	if (ret) {
		ts_debug("failed get event head data");
		return ret;
	}

	if (buffer[0] == 0) {
		ts_err("empty buffer[0] data");
		return -EINVAL;
	}

	if (checksum_cmp(buffer, IRQ_EVENT_HEAD_LEN, CHECKSUM_MODE_U8_LE)) {
		ts_err("touch head checksum err[0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X]",
				buffer[0], buffer[1], buffer[2], buffer[3],
				buffer[4], buffer[5], buffer[6], buffer[7]);
		if (--retry)
			goto restart;
		brl_after_event_handler(cd);
		return -EINVAL;
	}

	event_num = buffer[2] & 0x0F;
	if (unlikely(event_num > 2)) {
		ts_debug("read remain event event_num(%d)", event_num);
		ret = hw_ops->read(cd, misc->touch_data_addr + pre_read_len, &buffer[pre_read_len],
								(event_num - 2) * BYTES_PER_POINT);
		if (ret) {
			ts_err("failed to get remain event");
			return ret;
		}
	}

	// read done
	if (!cd->tools_ctrl_sync)
		brl_after_event_handler(cd);

	if (event_num > 0) {
		int event_read_len = event_num * BYTES_PER_POINT + COOR_DATA_CHECKSUM_SIZE;
		if (checksum_cmp(&buffer[IRQ_EVENT_HEAD_LEN], event_read_len, CHECKSUM_MODE_U8_LE)) {
			ts_err("event data checksum error");
			return -EINVAL;
		}
	}

	/* handler all event */
	if ((buffer[0] & 0x80) && (event_num > 0)) {
		unsigned char *event_data = buffer + IRQ_EVENT_HEAD_LEN;
		u8 eid;

		do {
			eid = event_data[0] & 0x03;
			switch (eid) {
			case SEC_TS_COORDINATE_EVENT:
				if (!atomic_read(&cd->suspended)) {
					unsigned int tid = (event_data[0] & 0x3C) >> 2;

					cd->lpm_coord_event_cnt = 0;

					if (tid >= GOODIX_MAX_TOUCH) {
						ts_err("invalid touch id = %d", tid);
						break;
					}
					goodix_parse_finger(cd, tid, event_data);
					goodix_ts_report_finger(cd, tid);
				} else {
					if (++cd->lpm_coord_event_cnt >= 5) {
						cd->lpm_coord_event_cnt = 0;
						/* if receive touch event, should resend gsx cmd */
						ts_err("touch event occurred in NP mode, resend gsx cmd");
						/* reinit */
						cd->plat_data->init(cd);
					}
				}
				break;
			case SEC_TS_STATUS_EVENT:
				cd->lpm_coord_event_cnt = 0;
				goodix_parse_status(cd, event_data);
				goodix_ts_report_status(cd, ts_event);
				break;
			case SEC_TS_GESTURE_EVENT:
				cd->lpm_coord_event_cnt = 0;
				goodix_parse_gesture(cd, event_data);
				goodix_ts_report_gesture(cd, ts_event);
				break;
			default:
				ts_debug("invalid event id[%x]", eid);
				break;
			}

			event_data += BYTES_PER_POINT;
		} while (--event_num);
	} else if ((buffer[0] & 0x80) && (event_num == 0)) {
		/* if event_num equal 0, realease all fingers */
		ts_debug("event num is zero [%02x%02x%02x]", buffer[0], buffer[1], buffer[2]);
		ts_event->event_type |= EVENT_EMPTY;

	} else {
		ts_err("invalid event flag[%x]", buffer[0]);
	}
	return 0;
}

static int brl_after_event_handler(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	u8 sync_clean = 0;

	return hw_ops->write(cd, misc->touch_data_addr, &sync_clean, 1);
}

static struct goodix_ts_hw_ops brl_hw_ops = {
	.power_on = brl_power_on,
	.dev_confirm = brl_dev_confirm,
	.resume = brl_resume,
	.suspend = brl_suspend,
	.gesture = brl_gesture,
	.reset = brl_reset,
	.irq_enable = brl_irq_enable,
	.read = brl_read,
	.write = brl_write,
	.read_from_sponge = brl_read_from_sponge,
	.write_to_sponge = brl_write_to_sponge,
	.write_to_flash = brl_write_to_flash,
	.read_from_flash = brl_read_from_flash,
	.send_cmd = brl_send_cmd,
	.send_cmd_delay = brl_send_cmd_with_delay,
	.send_config = brl_send_config,
	.read_config = brl_read_config,
	.read_version = brl_read_version,
	.get_ic_info = brl_get_ic_info,
	.esd_check = brl_esd_check,
	.event_handler = brl_event_handler,
	.after_event_handler = brl_after_event_handler,
	.get_capacitance_data = brl_get_capacitance_data,
	.enable_idle = brl_enable_idle,
	.sense_off = brl_sense_off,
	.ed_enable = brl_ed_enable,
	.pocket_mode_enable = brl_pocket_mode_enable,
};

struct goodix_ts_hw_ops *goodix_get_hw_ops(void)
{
	return &brl_hw_ops;
}
