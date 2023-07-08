// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_basic_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "mtk_charger.h"
#if defined (CONFIG_N23_CHARGER_PRIVATE)
#include <mt-plat/mtk_boot_common.h>
#include <mtk_musb.h>
#endif
#if defined (CONFIG_N26_CHARGER_PRIVATE)
#include <wingtech_charger.h>
#endif

static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

static void select_cv(struct mtk_charger *info)
{
	u32 constant_voltage;

	if (info->enable_sw_jeita)
		if (info->sw_jeita.cv != 0) {
			info->setting.cv = info->sw_jeita.cv;
			return;
		}

	constant_voltage = info->data.battery_cv;
	info->setting.cv = constant_voltage;
}

static bool is_typec_adapter(struct mtk_charger *info)
{
	int rp;

	rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
	if (info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK &&
			rp != 500 &&
			info->chr_type != POWER_SUPPLY_TYPE_USB &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_CDP)
		return true;

	return false;
}

static bool support_fast_charging(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int i = 0, state = 0;
	bool ret = false;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		chg_alg_set_current_limit(alg, &info->setting);
		state = chg_alg_is_algo_ready(alg);
		chr_debug("%s %s ret:%s\n", __func__, dev_name(&alg->dev),
			chg_alg_state_to_str(state));

		if (state == ALG_READY || state == ALG_RUNNING) {
			ret = true;
			break;
		}
	}
	return ret;
}

static bool select_charging_current_limit(struct mtk_charger *info,
	struct chg_limit_setting *setting)
{
	struct charger_data *pdata, *pdata2;
	bool is_basic = false;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret;

	select_cv(info);

	pdata = &info->chg_data[CHG1_SETTING];
	pdata2 = &info->chg_data[CHG2_SETTING];
	if (info->usb_unlimited) {
		pdata->input_current_limit =
					info->data.ac_charger_input_current;
		pdata->charging_current_limit =
					info->data.ac_charger_current;
		is_basic = true;
		goto done;
	}

	if (info->water_detected) {
		pdata->input_current_limit = info->data.usb_charger_current;
		pdata->charging_current_limit = info->data.usb_charger_current;
		is_basic = true;
		goto done;
	}

	if ((info->bootmode == 1) ||
	    (info->bootmode == 5)) {
		pdata->input_current_limit = 200000; /* 200mA */
		is_basic = true;
		goto done;
	}

	if (info->atm_enabled == true
		&& (info->chr_type == POWER_SUPPLY_TYPE_USB ||
		info->chr_type == POWER_SUPPLY_TYPE_USB_CDP)
		) {
		pdata->input_current_limit = 100000; /* 100mA */
		is_basic = true;
		goto done;
	}

	if (info->chr_type == POWER_SUPPLY_TYPE_USB) {
		pdata->input_current_limit =
				info->data.usb_charger_current;
		/* it can be larger */
		pdata->charging_current_limit =
				info->data.usb_charger_current;
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_CDP) {
		pdata->input_current_limit =
			info->data.charging_host_charger_current;
		pdata->charging_current_limit =
			info->data.charging_host_charger_current;
		is_basic = true;

	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_DCP) {
		pdata->input_current_limit =
			info->data.ac_charger_input_current;
		pdata->charging_current_limit =
			info->data.ac_charger_current;
		if (info->config == DUAL_CHARGERS_IN_SERIES) {
			pdata2->input_current_limit =
				pdata->input_current_limit;
			pdata2->charging_current_limit = 2000000;
		}
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_FLOAT) {
		/* NONSTANDARD_CHARGER */
		pdata->input_current_limit =
			info->data.usb_charger_current;
		pdata->charging_current_limit =
			info->data.usb_charger_current;
		is_basic = true;
	}

	if (support_fast_charging(info))
		is_basic = false;
	else {
		is_basic = true;
		/* AICL */
		charger_dev_run_aicl(info->chg1_dev,
			&pdata->input_current_limit_by_aicl);
		if (info->enable_dynamic_mivr) {
			if (pdata->input_current_limit_by_aicl >
				info->data.max_dmivr_charger_current)
				pdata->input_current_limit_by_aicl =
					info->data.max_dmivr_charger_current;
		}
		if (is_typec_adapter(info)) {
			if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL)
				== 3000) {
				pdata->input_current_limit = 3000000;
				pdata->charging_current_limit = 3000000;
			} else if (adapter_dev_get_property(info->pd_adapter,
				TYPEC_RP_LEVEL) == 1500) {
				pdata->input_current_limit = 1500000;
				pdata->charging_current_limit = 2000000;
			} else {
				chr_err("type-C: inquire rp error\n");
				pdata->input_current_limit = 500000;
				pdata->charging_current_limit = 500000;
			}

			chr_err("type-C:%d current:%d\n",
				info->pd_type,
				adapter_dev_get_property(info->pd_adapter,
					TYPEC_RP_LEVEL));
		}
	}

	if (info->enable_sw_jeita) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
			&& info->chr_type == POWER_SUPPLY_TYPE_USB)
			chr_debug("USBIF & STAND_HOST skip current check\n");
		else {
#if defined (CONFIG_N23_CHARGER_PRIVATE)
			if (info->sw_jeita.cc != 0) {
				if (pdata->charging_current_limit > info->sw_jeita.cc) {
					pdata->charging_current_limit = info->sw_jeita.cc;
				}
			}
#elif defined (CONFIG_N21_CHARGER_PRIVATE)
			if (info->sw_jeita.sm != TEMP_T2_TO_T3) {
				//info->setting only use for hvdcp charger
				//pdata->charging_current_limit only use for normal dcp and must lower than dcp
				if (pdata->charging_current_limit > info->sw_jeita.cc)
				{
					pdata->charging_current_limit = info->sw_jeita.cc;
				}
				info->setting.charging_current_limit1 = info->sw_jeita.cc;
			}else{
				info->setting.charging_current_limit1 = -1;
			}
			info->enable_hv_charging = true;
#else
			if (info->sw_jeita.sm == TEMP_T0_TO_T1) {
				pdata->input_current_limit = 500000;
				pdata->charging_current_limit = 350000;
			}
#endif
		}
	}
#ifndef CONFIG_MTK_DISABLE_TEMP_PROTECT
#ifndef CONFIG_N26_CHARGER_PRIVATE
	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
			pdata->charging_current_limit) {
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
			info->setting.charging_current_limit1 =
					pdata->thermal_charging_current_limit;
		}
		chr_err("thermal ctrl: set chg current = %d\n",pdata->charging_current_limit);
	} else
#ifdef CONFIG_N21_CHARGER_PRIVATE
	{
		if ((!info->enable_sw_jeita) || (IS_ENABLED(CONFIG_USBIF_COMPLIANCE))) {
			info->setting.charging_current_limit1 = -1;
		}
	}
	if((info->bootmode != 8) &&  //KERNEL_POWER_OFF_CHARGING_BOOT
		(info->bootmode != 9)) {    //LOW_POWER_OFF_CHARGING_BOOT
		if(info->lcmoff) {
			if(info->ap_thermal_lcmoff.cc < pdata->charging_current_limit)
			{
				pdata->charging_current_limit = info->ap_thermal_lcmoff.cc;
			}

			if(info->setting.charging_current_limit1 == -1)		//battery temp normal
			{
				info->setting.charging_current_limit1 = info->ap_thermal_lcmoff.cc;
			}else if(info->setting.charging_current_limit1 > info->ap_thermal_lcmoff.cc)
			{
				info->setting.charging_current_limit1 = info->ap_thermal_lcmoff.cc;
			}

		} else {
			if(info->ap_thermal_lcmon.cc < pdata->charging_current_limit)
			{
				pdata->charging_current_limit = info->ap_thermal_lcmon.cc;
			}

			if(info->setting.charging_current_limit1 == -1)		//battery temp normal
			{
				info->setting.charging_current_limit1 = info->ap_thermal_lcmon.cc;
			}else if(info->setting.charging_current_limit1 > info->ap_thermal_lcmon.cc)
			{
				info->setting.charging_current_limit1 = info->ap_thermal_lcmon.cc;
			}
		}
	}
#elif defined (CONFIG_N23_CHARGER_PRIVATE)
  	{
		if ((!info->enable_sw_jeita) || (IS_ENABLED(CONFIG_USBIF_COMPLIANCE))) {
			info->setting.charging_current_limit1 = -1;
		}
	}

	if((8 != info->bootmode ) &&  //KERNEL_POWER_OFF_CHARGING_BOOT
               (9 != info->bootmode )) { //LOW_POWER_OFF_CHARGING_BOOT
		if(info->lcmoff) {
			if(info->ap_thermal_lcmoff.cc < pdata->charging_current_limit)
			{
				pdata->charging_current_limit = info->ap_thermal_lcmoff.cc;
			}

		} else {
			if(info->ap_thermal_lcmon.cc < pdata->charging_current_limit)
			{
				pdata->charging_current_limit = info->ap_thermal_lcmon.cc;
			}
		}
	}
#else
		info->setting.charging_current_limit1 = -1;
#endif
	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
			pdata->input_current_limit) {
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
			info->setting.input_current_limit1 =
					pdata->input_current_limit;
			chr_err("thermal ctrl: set input current = %d\n",pdata->input_current_limit);
		}
	} else
		info->setting.input_current_limit1 = -1;

	if (pdata2->thermal_charging_current_limit != -1) {
		if (pdata2->thermal_charging_current_limit <
			pdata2->charging_current_limit) {
			pdata2->charging_current_limit =
					pdata2->thermal_charging_current_limit;
			info->setting.charging_current_limit2 =
					pdata2->charging_current_limit;
		}
	} else
		info->setting.charging_current_limit2 = -1;

	if (pdata2->thermal_input_current_limit != -1) {
		if (pdata2->thermal_input_current_limit <
			pdata2->input_current_limit) {
			pdata2->input_current_limit =
					pdata2->thermal_input_current_limit;
			info->setting.input_current_limit2 =
					pdata2->input_current_limit;
		}
	} else
		info->setting.input_current_limit2 = -1;
#endif
#endif
	if (is_basic == true && pdata->input_current_limit_by_aicl != -1) {
		if (pdata->input_current_limit_by_aicl <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->input_current_limit_by_aicl;
	}
done:

	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min) {
//+Bug 682591,xuejizhou.wt,ADD,20210817,SW JEITA configuration
#if defined (CONFIG_N23_CHARGER_PRIVATE) || defined (CONFIG_N21_CHARGER_PRIVATE)
		chr_err("min_charging_current is too low %d %d\n",
		pdata->charging_current_limit, ichg1_min);
		pdata->charging_current_limit = ichg1_min;
		is_basic = true;
		info->enable_hv_charging = false;
#else
		pdata->charging_current_limit = 0;
		chr_err("min_charging_current is too low %d %d\n",
			pdata->charging_current_limit, ichg1_min);
		is_basic = true;
#endif
//-Bug 682591,xuejizhou.wt,ADD,20210817,SW JEITA configuration
	}

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min) {
//+Bug 682591,xuejizhou.wt,ADD,20210817,SW JEITA configuration
#if defined (CONFIG_N23_CHARGER_PRIVATE) || defined (CONFIG_N21_CHARGER_PRIVATE)
		chr_err("min_input_current is too low %d ,limit at %d\n",
			pdata->input_current_limit, aicr1_min);
		pdata->input_current_limit = aicr1_min;
		is_basic = true;
		info->enable_hv_charging = false;
#else
		pdata->input_current_limit = 0;
		chr_err("min_input_current is too low %d %d\n",
			pdata->input_current_limit, aicr1_min);
		is_basic = true;
#endif
//-Bug 682591,xuejizhou.wt,ADD,20210817,SW JEITA configuration
	}
#if defined (CONFIG_N26_CHARGER_PRIVATE)
	switch(info->chr_type){
		case POWER_SUPPLY_TYPE_USB:
			pdata->input_current_limit = info->data.usb_charger_current;
			pdata->charging_current_limit =	info->data.usb_charger_current;
			if(info->pd_type == MTK_PD_CONNECT_PE_READY_SNK || 
			   info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
			   info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO){
			   	if(get_vbus(info) > 8000){
					chr_err("[wtchg]: pd type is %d(9000mv), fast charging!\n",info->pd_type);
					pdata->input_current_limit = info->data.fast_charger_input_current;
					pdata->charging_current_limit =	info->data.fast_charger_current;
			   	}
			}
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			pdata->input_current_limit = info->data.charging_host_charger_current;
			pdata->charging_current_limit =	info->data.charging_host_charger_current;
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:
			pdata->input_current_limit = info->data.ac_charger_input_current;
			pdata->charging_current_limit =	info->data.ac_charger_current;
			if(info->pd_type == MTK_PD_CONNECT_PE_READY_SNK || 
			   info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
			   info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO){
				chr_err("[wtchg]: pd type is %d, fast charging!\n",info->pd_type);
				pdata->input_current_limit = info->data.fast_charger_input_current;
				pdata->charging_current_limit =	info->data.fast_charger_current;
			}
			break;
		case POWER_SUPPLY_TYPE_USB_FLOAT:
			pdata->input_current_limit = info->data.usb_charger_current;
			pdata->charging_current_limit =	info->data.usb_charger_current;
			break;
		case POWER_SUPPLY_TYPE_USB_AFC:
			pdata->input_current_limit = info->data.fast_charger_input_current;
			pdata->charging_current_limit =	info->data.fast_charger_current;
			break;
		default:
			pdata->input_current_limit = info->data.usb_charger_current;
			pdata->charging_current_limit =	info->data.usb_charger_current;
	}
	chr_err("[wtchg]: type=%d, input_current_limit=%d,charging_current_limit=%d,sw_jeita.cc=%d\n",
		info->chr_type,pdata->input_current_limit,pdata->charging_current_limit,info->sw_jeita.cc);
#ifndef CONFIG_MTK_DISABLE_TEMP_PROTECT
	if(pdata->charging_current_limit > info->sw_jeita.cc)
		pdata->charging_current_limit = info->sw_jeita.cc;
#endif
	chr_err("[wtchg]: charging_current_limit=%d\n",pdata->charging_current_limit);
#endif

	chr_err("m:%d chg1:%d,%d,%d,%d chg2:%d,%d,%d,%d type:%d:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d bm:%d b:%d\n",
		info->config,
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		_uA_to_mA(pdata2->thermal_input_current_limit),
		_uA_to_mA(pdata2->thermal_charging_current_limit),
		_uA_to_mA(pdata2->input_current_limit),
		_uA_to_mA(pdata2->charging_current_limit),
		info->chr_type, info->pd_type,
		info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled,
		info->bootmode, is_basic);

	return is_basic;
}

static int do_algorithm(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	struct charger_data *pdata;
	struct chg_alg_notify notify;
	bool is_basic = true;
	bool chg_done = false;
	int i;
	int ret;
	int val = 0;

	pdata = &info->chg_data[CHG1_SETTING];
	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	is_basic = select_charging_current_limit(info, &info->setting);

	if (info->is_chg_done != chg_done) {
		if (chg_done) {
			charger_dev_do_event(info->chg1_dev, EVENT_FULL, 0);
			chr_err("%s battery full\n", __func__);
		} else {
			charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
			chr_err("%s battery recharge\n", __func__);
		}
	}

	chr_err("%s is_basic:%d\n", __func__, is_basic);
	if (is_basic != true) {
		is_basic = true;
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;

			if (!info->enable_hv_charging ||
			    pdata->charging_current_limit == 0 ||
			    pdata->input_current_limit == 0) {
				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000)
					chg_alg_stop_algo(alg);
				chr_err("%s: alg:%s alg_vbus:%d\n", __func__,
					dev_name(&alg->dev), val);
				continue;
			}

			if (chg_done != info->is_chg_done) {
				if (chg_done) {
					notify.evt = EVT_FULL;
					notify.value = 0;
				} else {
					notify.evt = EVT_RECHARGE;
					notify.value = 0;
				}
				chg_alg_notifier_call(alg, &notify);
				chr_err("%s notify:%d\n", __func__, notify.evt);
			}

			chg_alg_set_current_limit(alg, &info->setting);
			ret = chg_alg_is_algo_ready(alg);

			chr_err("%s %s ret:%s\n", __func__,
				dev_name(&alg->dev),
				chg_alg_state_to_str(ret));

			if (ret == ALG_INIT_FAIL || ret == ALG_TA_NOT_SUPPORT) {
				/* try next algorithm */
				continue;
			} else if (ret == ALG_TA_CHECKING || ret == ALG_DONE ||
						ret == ALG_NOT_READY) {
				/* wait checking , use basic first */
				is_basic = true;
				break;
			} else if (ret == ALG_READY || ret == ALG_RUNNING) {
				is_basic = false;
				//chg_alg_set_setting(alg, &info->setting);
				chg_alg_start_algo(alg);
				break;
			} else {
				chr_err("algorithm ret is error");
				is_basic = true;
			}
		}
	} else {
		if (info->enable_hv_charging != true ||
		    pdata->charging_current_limit == 0 ||
		    pdata->input_current_limit == 0) {
			for (i = 0; i < MAX_ALG_NO; i++) {
				alg = info->alg[i];
				if (alg == NULL)
					continue;

				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000 && chg_alg_is_algo_running(alg))
					chg_alg_stop_algo(alg);

				chr_err("%s: Stop hv charging. en_hv:%d alg:%s alg_vbus:%d\n",
					__func__, info->enable_hv_charging,
					dev_name(&alg->dev), val);
			}
		}
	}
	info->is_chg_done = chg_done;
#ifdef CONFIG_MTK_DISABLE_TEMP_PROTECT	
#if defined (CONFIG_N26_CHARGER_PRIVATE)
	if(1){
		struct wtchg_info *wt_info = wt_get_wtchg_info();
		if (!IS_ERR_OR_NULL(wt_info)){
			if(wt_info->bat_temp > 45)
				info->setting.cv = info->data.jeita_temp_t3_to_t4_cv;
			else
				info->setting.cv = info->data.jeita_temp_t2_to_t3_cv;
		}
	}
	chr_err("## cv = %d\n",info->setting.cv);
#endif			
#endif
	if (is_basic == true) {
		charger_dev_set_input_current(info->chg1_dev,
			pdata->input_current_limit);
		charger_dev_set_charging_current(info->chg1_dev,
			pdata->charging_current_limit);
		if(get_battery_voltage(info)*1000 < info->setting.cv)
			charger_dev_set_constant_voltage(info->chg1_dev,
				info->setting.cv);
		else
			chr_err("%s: cv < vbat!, hold back set cv !!!\n",__func__);
	}

	if (pdata->input_current_limit == 0 ||
	    pdata->charging_current_limit == 0)
		charger_dev_enable(info->chg1_dev, false);
	else
		charger_dev_enable(info->chg1_dev, true);

	if (info->chg1_dev != NULL)
		charger_dev_dump_registers(info->chg1_dev);

	if (info->chg2_dev != NULL)
		charger_dev_dump_registers(info->chg2_dev);

	return 0;
}

static int enable_charging(struct mtk_charger *info,
						bool en)
{
	int i;
	struct chg_alg_device *alg;


	chr_err("%s %d\n", __func__, en);

	if (en == false) {
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;
			chg_alg_stop_algo(alg);
		}
		charger_dev_enable(info->chg1_dev, false);
		charger_dev_do_event(info->chg1_dev, EVENT_DISCHARGE, 0);
	} else {
		charger_dev_enable(info->chg1_dev, true);
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
	}

	return 0;
}

static int charger_dev_event(struct notifier_block *nb, unsigned long event,
				void *v)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	struct mtk_charger *info =
			container_of(nb, struct mtk_charger, chg1_nb);
	struct chgdev_notify *data = v;
	int i;

	chr_err("%s %d\n", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		notify.evt = EVT_FULL;
		notify.value = 0;
	for (i = 0; i < 10; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
	}

		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		pr_info("%s: safety timer timeout\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		pr_info("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}



int mtk_basic_charger_init(struct mtk_charger *info)
{

	info->algo.do_algorithm = do_algorithm;
	info->algo.enable_charging = enable_charging;
	info->algo.do_event = charger_dev_event;
	//info->change_current_setting = mtk_basic_charging_current;
	return 0;
}



