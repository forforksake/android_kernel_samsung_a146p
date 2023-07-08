
#ifndef BUILD_LK
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
/* #include <linux/jiffies.h> */
/* #include <linux/delay.h> */
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#endif
#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include "disp_dts_gpio.h"
#endif

#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_pm_ldo.h>	/* hwPowerOn */
#include <mt-plat/upmu_common.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#endif
#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_gpio.h>
#include <cust_gpio_usage.h>
#include <cust_i2c.h>
#endif
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "lcm_define.h"
#include "lcm_drv.h"
#include "lcm_i2c.h"




/*****************************************************************************
 * Define
 *****************************************************************************/
#define LCM_GPIO_DEVICES	"lcm_mode1"
#define LCM_GPIO_MODE_00	0
#define MAX_LCM_GPIO_MODE	6
#define LCM_I2C_ID_NAME "sm5109"
/*****************************************************************************
 * Function GPIO
 *****************************************************************************/
 static struct pinctrl *_lcm_gpio;
/*static struct pinctrl_state *_lcm_gpio_enp_mode;
 static struct pinctrl_state *_lcm_gpio_enn_mode;
 static struct pinctrl_state *_lcm_gpio_rst_mode;*/
 static struct pinctrl_state *_lcm_gpio_mode[MAX_LCM_GPIO_MODE];
 static unsigned char _lcm_gpio_mode_list[MAX_LCM_GPIO_MODE][128] = {
	 "state_enp_output0",
	 "state_enp_output1",
	 "state_enn_output0",
	 "state_enn_output1",
	 "state_reset_output0",
	 "state_reset_output1",
 };
 
 
 /* function definitions */
 static int __init _lcm_gpio_init(void);
 static void __exit _lcm_gpio_exit(void);
 static int _lcm_gpio_probe(struct platform_device *pdev);
 static int _lcm_gpio_remove(struct platform_device *pdev);

 static const struct of_device_id _lcm_gpio_of_idss[] = {
	 {.compatible = "mediatek,lcm_gpio",},
	 {},
 };
 //MODULE_DEVICE_TABLE(of, _lcm_gpio_of_idss);
 
 
 static struct platform_driver _lcm_gpio_driver = {
	 .driver = {
		 .name = LCM_GPIO_DEVICES,
		 .owner  = THIS_MODULE,
		 .of_match_table = of_match_ptr(_lcm_gpio_of_idss),
	 },
	 .probe = _lcm_gpio_probe,
	 .remove = _lcm_gpio_remove,
 };
 //module_platform_driver(_lcm_gpio_driver);
 
 /*****************************************************************************
  * Function I2C
  *****************************************************************************/
 
 static struct i2c_client *_lcm_i2c_client;
 static int _lcm_i2c_probe(struct i2c_client *client,
	 const struct i2c_device_id *id);
 static int _lcm_i2c_remove(struct i2c_client *client);
 
 struct _lcm_i2c_dev {
	 struct i2c_client *client;
 
 };
 
 static const struct of_device_id _lcm_i2c_of_match[] = {
	 {
	  .compatible = "mediatek,I2C_LCD_BIAS",
	  },
 };
 
 static const struct i2c_device_id _lcm_i2c_id[] = {
	 {LCM_I2C_ID_NAME, 0},
	 {}
 };
 static struct i2c_driver _lcm_i2c_driver = {
	 .id_table = _lcm_i2c_id,
	 .probe = _lcm_i2c_probe,
	 .remove = _lcm_i2c_remove,
	 .driver = {
			.owner = THIS_MODULE,
			.name = LCM_I2C_ID_NAME,
#ifdef CONFIG_MTK_LEGACY
#else
			.of_match_table = _lcm_i2c_of_match,
#endif
			},
 
 };
 static int _lcm_i2c_probe(struct i2c_client *client,
	 const struct i2c_device_id *id)
 {
	 pr_debug("[LCM][I2C] _lcm_i2c_probe\n");
	 pr_debug("[LCM][I2C] NT: info==>name=%s addr=0x%x\n",
		 client->name, client->addr);
	 _lcm_i2c_client = client;
	 return 0;
 }
 
 
 static int _lcm_i2c_remove(struct i2c_client *client)
 {
	 pr_debug("[LCM][I2C] _lcm_i2c_remove\n");
	 _lcm_i2c_client = NULL;
	 i2c_unregister_device(client);
	 return 0;
 }
 static int _lcm_i2c_write_bytes(unsigned char addr, unsigned char value)
 {
	 int ret = 0;
	 struct i2c_client *client = _lcm_i2c_client;
	 char write_data[2] = { 0 };
 
	 if (client == NULL) {
		 pr_debug("ERROR!! _lcm_i2c_client is null\n");
		 return 0;
	 }
	// pr_err("[LCM][I2C] __lcm_i2c_write_bytes\n");
 
	 write_data[0] = addr;
	 write_data[1] = value;
	 ret = i2c_master_send(client, write_data, 2);
	 if (ret < 0)
		 pr_info("[LCM][ERROR] _lcm_i2c write data fail !!\n");
 
	 return ret;
 }
 static int _lcm_i2c_read_bytes(unsigned char addr, unsigned char value)
 {
	 int ret = 0;
	 struct i2c_client *client = _lcm_i2c_client;
	 char read_data[2] = { 0 };
	 //pr_err("[LCM][I2C] __lcm_i2c_read_bytes\n");
 
	 if (client == NULL) {
		 pr_debug("ERROR!! _lcm_i2c_client is null\n");
		 return 0;
	 }
 
	 read_data[0] = addr;
	 read_data[1] = value;
	 ret = i2c_master_recv(client, read_data, 2);
	 if (ret < 0)
		 pr_info("[LCM][ERROR] _lcm_i2c read data fail !!\n");
 
	 return ret;
 }
 static int SM5109_REG_MASK (unsigned char addr, unsigned char val, unsigned char mask)
 {
		 unsigned char SM5109_reg=0;
		 unsigned char ret = 0;
		// pr_err("[LCM][I2C] SM5109_REG_MASK\n");
 
		 ret = _lcm_i2c_read_bytes(addr, SM5109_reg);
 
		 SM5109_reg &= ~mask;
		 SM5109_reg |= val;
 
		 ret = _lcm_i2c_write_bytes(addr, SM5109_reg);
 
		 return ret;
 }
  int lcm_power_enable(void){
 	int ret=0;
	//pr_debug("[LCM]power enable\n");
	pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[1]);    //enable enp
	mdelay(2);
	pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[3]);	   //enable enn
	mdelay(2);
	/* set AVDD*/
	/*4.0V + 20* 100mV*/
	ret = SM5109_REG_MASK(0x00, 20, (0x1F << 0));
	if (ret < 0)
			pr_debug("ili9881h----cmd=%0x--i2c write error----\n", 0x00);

	/* set AVEE */
	/*-4.0V - 20* 100mV*/
	ret = SM5109_REG_MASK(0x01, 20, (0x1F << 0));
	if (ret < 0)
			pr_debug("ili9881h----cmd=%0x--i2c write error----\n", 0x01);

	/* enable AVDD & AVEE discharge*/
	ret = SM5109_REG_MASK(0x03, (1<<0) | (1<<1), (1<<0) | (1<<1));
	if (ret < 0)
			pr_debug("ili9881h----cmd=%0x--i2c write error----\n", 0x03);

	return ret;

 }
  EXPORT_SYMBOL(lcm_power_enable);
 int lcm_power_disable(void){
 //	pr_debug("[LCM]power disable\n");
	pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[2]);  //pull down enn
	mdelay(2);
	pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[0]);  //pull down enp
	return 0;
 }
 EXPORT_SYMBOL(lcm_power_disable);
 enum Color {
   LOW,
   HIGH
 };
  void lcm_reset_pin(unsigned int mode)
 {
//	 pr_debug("[LCM][GPIO]lcm_reset_pin mode:%d !\n",mode);
	 if((mode==0)||(mode==1)){
		 switch (mode){
			 case LOW :
				 pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[4]);
				 break;
			 case HIGH:
				 pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[5]);
				 break;
			 default:
				 break;
 
		 }

 	}
 }
 EXPORT_SYMBOL(lcm_reset_pin);

void lcm_bais_enp_enable(unsigned int mode)
{
	pr_debug("[LCM][GPIO]lcm_bais_enp_enable mode:%d !\n",mode);
	if((mode==0)||(mode==1)){
		switch (mode){
			case LOW :
				pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[0]);
				break;
			case HIGH:
				pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[1]);
				break;
		 }
	}
}
EXPORT_SYMBOL(lcm_bais_enp_enable);

void lcm_bais_enn_enable(unsigned int mode)
{
    pr_debug("[LCM][GPIO]lcm_bais_enn_enable mode:%d !\n",mode);
	if((mode==0)||(mode==1)){
		switch (mode){
			case LOW :
				pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[2]);
				break;
			case HIGH:
				pinctrl_select_state(_lcm_gpio, _lcm_gpio_mode[3]);
				break;
		}
	}
}
EXPORT_SYMBOL(lcm_bais_enn_enable);

int wingtech_bright_to_bl(int level,int max_bright,int min_bright,int bl_max,int bl_min) {
	if(!level){
		return 0;
	}else{
		if(level <= min_bright){
			return bl_min;
		}else{
			return ((((int)bl_max - (int)bl_min)*level + ((int)max_bright*(int)bl_min -
			(int)min_bright*(int)bl_max))/((int)max_bright - (int)min_bright));
		}
	}
}
EXPORT_SYMBOL(wingtech_bright_to_bl);

 static int _lcm_gpio_probe(struct platform_device *pdev){
		 int ret;
		 unsigned int mode;
		 struct device	 *dev = &pdev->dev;
	 
//		 pr_debug("[LCM][GPIO] enter %s, %d\n", __func__, __LINE__);
	 
		 _lcm_gpio = devm_pinctrl_get(dev);
		 if (IS_ERR(_lcm_gpio)) {
			 ret = PTR_ERR(_lcm_gpio);
		 pr_err("[LCM][ERROR] Cannot find _lcm_gpio!\n");
			 return ret;
		 }
		 for (mode = LCM_GPIO_MODE_00; mode < MAX_LCM_GPIO_MODE; mode++) {
			 _lcm_gpio_mode[mode] =
				 pinctrl_lookup_state(_lcm_gpio,
					 _lcm_gpio_mode_list[mode]);
			 if (IS_ERR(_lcm_gpio_mode[mode]))
				 pr_err("[LCM][ERROR] Cannot find lcm_mode:%d! skip it.\n",
				 mode);
		 }
	 //	pr_debug("[LCM][GPIO] exit %s, %d\n", __func__, __LINE__);
 	//	pr_debug("MediaTek LCM I2C driver init\n");
	 		ret = i2c_add_driver(&_lcm_i2c_driver);
	// 	pr_debug("MediaTek LCM I2C driver init ret=%d\n",ret);
	 		if(ret !=0){
		 		pr_debug("[LCM][I2C] _lcm_i2c_init fail\n");
		 		return -1;
	 		}
	 return 0;
 } 
  static int _lcm_gpio_remove(struct platform_device *pdev)
 {
 
	 return 0;
 }
 
 static int __init _lcm_gpio_init(void)
 {
	// pr_debug("[LCM] LCM GPIO driver init\n");
	 if (platform_driver_register(&_lcm_gpio_driver) != 0) {
		 pr_info("[LCM]unable to register LCM GPIO driver.\n");
		 return -1;
	 }
	 return 0;
 }
 
 static void __exit _lcm_gpio_exit(void)
 {
	 //pr_debug("MediaTek LCM GPIO driver exit\n");
	 platform_driver_unregister(&_lcm_gpio_driver);
	// pr_debug("[LCM][I2C] _lcm_i2c_exit\n");
	 i2c_del_driver(&_lcm_i2c_driver);
 
 }
 
 module_init(_lcm_gpio_init);
 module_exit(_lcm_gpio_exit);
 
 MODULE_LICENSE("GPL");
 MODULE_DESCRIPTION("ADD SM5109 BIAS driver");
 MODULE_AUTHOR("lianghao2@wingtech.com>");

