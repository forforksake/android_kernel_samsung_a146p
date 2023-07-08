/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#ifndef _CAM_CAL_DATA_H
#define _CAM_CAL_DATA_H

#ifdef CONFIG_COMPAT
/* 64 bit */
#include <linux/fs.h>
#include <linux/compat.h>
#endif

//+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
struct stCAM_CAL_DATAINFO_STRUCT{
	u32 sensorID; // Sensor ID
	u32 deviceID; // MAIN = 0x01, SUB  = 0x02, MAIN_2 = 0x04
	u32 dataLength; //Data len
	u32 sensorVendorid; // Module ID | Pos ID | Vcm ID | Len ID
	u8  vendorByte[4]; // Module ID offset, Pos ID offset, Vcm ID offset,  Len ID offset
	u8  *dataBuffer; //It's need malloc dataLength cache
};

typedef enum{
	MODULE_ITEM = 0,
	SN_DATA,
	AWB_ITEM,
	SEGMENT_ITEM,
	AF_ITEM,
	LSC_ITEM,
	PDAF_ITEM,
	PDAF_PROC2_ITEM,   //+bug682590,zhanghengyuan.wt,ADD,2021/8/25,add n23_hi1336_rear_txd OTP
        hi5022q_XGC,
        hi5022q_QGC,
        hi5022q_OPC,
        //+bug720367,qinduilin.wt,ADD,2022/2/17,add for n26 camera bring up
        hi5021q_XGC,
        hi5021q_QGC,
        hi5021q_PGC,
	XTC_DATA,
	SENSOR_XTC_DATA,
	PDXTC_DATA,
	SWGCC_DATA,
	HWGCC_DATA,
        //-bug720367,qinduilin.wt,ADD,2022/2/17,add for n26 camera bring up
	DUALCAM_ITEM,
	TOTAL_ITEM,
	MAX_ITEM,
}stCAM_CAL_CHECKSUM_ITEM;

struct stCAM_CAL_CHECKSUM_STRUCT{
	stCAM_CAL_CHECKSUM_ITEM item;
	u32 flagAdrees;
	u32 startAdress;
	u32 endAdress;
	u32 checksumAdress;
	u8  validFlag;
};
//-bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.

struct CAM_CAL_SENSOR_INFO {
	u32 sensor_id;
};

struct stCAM_CAL_INFO_STRUCT {
	u32 u4Offset;
	u32 u4Length;
	u32 sensorID;
	/*
	 * MAIN = 0x01,
	 * SUB  = 0x02,
	 * MAIN_2 = 0x04,
	 * SUB_2 = 0x08,
	 * MAIN_3 = 0x10,
	 */
	u32 deviceID;
	u8 *pu1Params;
};

#ifdef CONFIG_COMPAT

struct COMPAT_stCAM_CAL_INFO_STRUCT {
	u32 u4Offset;
	u32 u4Length;
	u32 sensorID;
	u32 deviceID;
	compat_uptr_t pu1Params;
};
#endif

#endif/*_CAM_CAL_DATA_H*/
