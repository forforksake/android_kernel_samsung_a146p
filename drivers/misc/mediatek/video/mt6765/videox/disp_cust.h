/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#ifndef _DISP_CUST_H_
#define _DISP_CUST_H_

extern void set_lcm(struct LCM_setting_table_V3 *para_tbl,
			unsigned int size, bool hs, bool need_lock);
extern int read_lcm(unsigned char cmd, unsigned char *buf,
		unsigned char buf_size, bool sendhs, bool need_lock,
		unsigned char offset);

#endif
