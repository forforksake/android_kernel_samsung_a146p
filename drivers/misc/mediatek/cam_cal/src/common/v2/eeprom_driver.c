/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "eeprom_driver.h"
#include "eeprom_i2c_common_driver.h"
#include "cam_cal_list.h"

//+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
#include "kd_camera_feature.h"
#include <linux/file.h>
#include <linux/unistd.h>
#include "kd_imgsensor.h"
//-bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.

#include "cam_cal.h"

#define DEV_NODE_NAME_PREFIX "camera_eeprom"
#define DEV_NAME_FMT "camera_eeprom%u"
#define DEV_CLASS_NAME_FMT "camera_eepromdrv%u"
#define EEPROM_DEVICE_NNUMBER 255
//+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
#define DEV_NAME_FMT_DEV "/dev/camera_eeprom%u"
#define LOG_INF(format, args...) pr_info(PFX "[%s] " format, __func__, ##args)
#define LOG_DBG(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
#define LOG_ERR(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
//-bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.

static struct EEPROM_DRV ginst_drv[MAX_EEPROM_NUMBER];

static struct stCAM_CAL_LIST_STRUCT *get_list(struct CAM_CAL_SENSOR_INFO *sinfo)
{
	struct stCAM_CAL_LIST_STRUCT *plist;

	cam_cal_get_sensor_list(&plist);

	while (plist &&
	       (plist->sensorID != 0) &&
	       (plist->sensorID != sinfo->sensor_id))
		plist++;

	return plist;
}

static unsigned int read_region(struct EEPROM_DRV_FD_DATA *pdata,
				unsigned char *buf,
				unsigned int offset, unsigned int size)
{
	unsigned int ret;
	unsigned short dts_addr;
	struct stCAM_CAL_LIST_STRUCT *plist = get_list(&pdata->sensor_info);
	unsigned int size_limit = (plist && plist->maxEepromSize > 0)
		? plist->maxEepromSize : DEFAULT_MAX_EEPROM_SIZE_8K;

	if (offset + size > size_limit) {
		pr_debug("Error! not support address >= 0x%x!!\n", size_limit);
		return 0;
	}

	if (plist && plist->readCamCalData) {
		pr_debug("i2c addr 0x%x\n", plist->slaveID);
		mutex_lock(&pdata->pdrv->eeprom_mutex);
		dts_addr = pdata->pdrv->pi2c_client->addr;
		pdata->pdrv->pi2c_client->addr = (plist->slaveID >> 1);
		ret = plist->readCamCalData(pdata->pdrv->pi2c_client,
					    offset, buf, size);
		pdata->pdrv->pi2c_client->addr = dts_addr;
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
	} else {
		pr_debug("no customized\n");
		mutex_lock(&pdata->pdrv->eeprom_mutex);
		ret = Common_read_region(pdata->pdrv->pi2c_client,
					 offset, buf, size);
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
	}

	return ret;
}

static unsigned int write_region(struct EEPROM_DRV_FD_DATA *pdata,
				unsigned char *buf,
				unsigned int offset, unsigned int size)
{
	unsigned int ret;
	unsigned short dts_addr;
	struct stCAM_CAL_LIST_STRUCT *plist = get_list(&pdata->sensor_info);
	unsigned int size_limit = (plist && plist->maxEepromSize > 0)
		? plist->maxEepromSize : DEFAULT_MAX_EEPROM_SIZE_8K;

	if (offset + size > size_limit) {
		pr_debug("Error! not support address >= 0x%x!!\n", size_limit);
		return 0;
	}

	if (plist && plist->writeCamCalData) {
		pr_debug("i2c addr 0x%x\n", plist->slaveID);
		mutex_lock(&pdata->pdrv->eeprom_mutex);
		dts_addr = pdata->pdrv->pi2c_client->addr;
		pdata->pdrv->pi2c_client->addr = (plist->slaveID >> 1);
		ret = plist->writeCamCalData(pdata->pdrv->pi2c_client,
					    offset, buf, size);
		pdata->pdrv->pi2c_client->addr = dts_addr;
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
	} else {
		pr_debug("no customized\n");
		mutex_lock(&pdata->pdrv->eeprom_mutex);
		ret = Common_write_region(pdata->pdrv->pi2c_client,
					 offset, buf, size);
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
	}

	return ret;
}

//+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
#define MAIN_OTP_DUMP 0

struct stCAM_CAL_DATAINFO_STRUCT *g_eepromMainData = NULL;
struct stCAM_CAL_DATAINFO_STRUCT *g_eepromSubData = NULL;
struct stCAM_CAL_DATAINFO_STRUCT *g_eepromMainMicroData = NULL;
struct stCAM_CAL_DATAINFO_STRUCT *g_eepromWideData = NULL;

#if MAIN_OTP_DUMP
void dumpEEPROMData(int u4Length,u8* pu1Params)
{
    int i = 0;
    for(i = 0; i < u4Length; i += 16){
        if(u4Length - i  >= 16){
            LOG_INF("cam_eeprom[%d-%d]:0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x ",
            i,i+15,pu1Params[i],pu1Params[i+1],pu1Params[i+2],pu1Params[i+3],pu1Params[i+4],pu1Params[i+5],pu1Params[i+6]
            ,pu1Params[i+7],pu1Params[i+8],pu1Params[i+9],pu1Params[i+10],pu1Params[i+11],pu1Params[i+12],pu1Params[i+13],pu1Params[i+14]
            ,pu1Params[i+15]);
        }else{
            int j = i;
            for(;j < u4Length;j++)
            LOG_INF("cam_eeprom[%d] = 0x%2x ",j,pu1Params[j]);
        }
    }
    LOG_INF("cam_eeprom end\n");
}
#endif

int imgSensorCheckEepromData(struct stCAM_CAL_DATAINFO_STRUCT* pData, struct stCAM_CAL_CHECKSUM_STRUCT* cData){
    int i = 0;
    int length = 0;
    int count;
    u32 sum = 0;

    if((pData != NULL)&&(pData->dataBuffer != NULL)&&(cData != NULL)){
        u8* buffer = pData->dataBuffer;
        //verity validflag and checksum
        for((count = 0);count < MAX_ITEM;count++){
            if(cData[count].item < MAX_ITEM) {
                if(buffer[cData[count].flagAdrees]!= cData[count].validFlag){
                    LOG_ERR("invalid otp data cItem=%d,flag=%d failed\n", cData[count].item,buffer[cData[count].flagAdrees]);
                    return -ENODEV;
                } else {
                    LOG_INF("check cTtem=%d,flag=%d otp flag data successful!\n", cData[count].item,buffer[cData[count].flagAdrees]);
                }
                sum = 0;
                length = cData[count].endAdress - cData[count].startAdress;
                for(i = 0;i <= length;i++){
                    sum += buffer[cData[count].startAdress+i];
                }
                if(((sum%0xff)+1)!= buffer[cData[count].checksumAdress]){
                    LOG_ERR("checksum cItem=%d,0x%x,length = 0x%x failed\n",cData[count].item,sum,length);
                    return -ENODEV;
                } else {
                    LOG_INF("checksum cItem=%d,0x%x,length = 0x%x successful!\n",cData[count].item,sum,length);
                }
            } else {
                break;
            }
        }
    } else {
        LOG_ERR("some data not inited!\n");
        return -ENODEV;
    }

    LOG_INF("sensor[0x%x][0x%x] eeprom checksum success\n", pData->sensorID, pData->deviceID);

    return 0;
}

int imgSensorReadEepromData(struct stCAM_CAL_DATAINFO_STRUCT* pData, struct stCAM_CAL_CHECKSUM_STRUCT* checkData){
        //struct EEPROM_DRV *pinst;
        struct EEPROM_DRV_FD_DATA *fd_pdata = NULL;
        int i4RetValue = -1;
        u32 vendorID = 0;
        u8 tmpBuf[4] = {0};
        struct file *f = NULL;
        unsigned int index = 0;
        char device_drv_name[DEV_NAME_STR_LEN_MAX] = { 0 };

        if((pData == NULL)||(checkData == NULL)){
            LOG_ERR("pData or checkData not inited!\n");
            return -EFAULT;
        }

        LOG_INF("SensorID=0x%x DeviceID=0x%x\n",pData->sensorID, pData->deviceID);
        index = IMGSENSOR_SENSOR_IDX_MAP(pData->deviceID);
        LOG_DBG("index=%d",index);
        if (index >= MAX_EEPROM_NUMBER) {
                LOG_ERR("node index out of bound\n");
                return -EINVAL;
        }

        i4RetValue = snprintf(device_drv_name, DEV_NAME_STR_LEN_MAX - 1,
            DEV_NAME_FMT_DEV, index);
        LOG_INF("device_drv_name=%s",device_drv_name);
        if (i4RetValue < 0) {
                LOG_ERR(
                "[eeprom]%s error, ret = %d", __func__, i4RetValue);
                return -EFAULT;
        }

            //get i2c client
            /*index = IMGSENSOR_SENSOR_IDX_MAP(pData->deviceID);
            LOG_INF("zyk index=%d",index);
            pinst = &ginst_drv[index];*/

            //1st open file
            if (f == NULL){
                f = filp_open(device_drv_name, O_RDWR, 0);
            }
            if (IS_ERR(f)){
                LOG_ERR("fail to open %s\n", device_drv_name);
                return -EFAULT;
            }

            fd_pdata = (struct EEPROM_DRV_FD_DATA *) f->private_data;
            if(NULL == fd_pdata){
                LOG_ERR("fp_pdata is null %s\n");
                filp_close(f,NULL);
                return -EFAULT;
            }
            fd_pdata->sensor_info.sensor_id = pData->sensorID;
            //2nd verity vendorID
            //u8 *kbuf = kmalloc(1, GFP_KERNEL);
            LOG_INF("read vendorId!\n");
            //f->f_pos = 1;
            i4RetValue = read_region(fd_pdata, &tmpBuf[0], 1, 1);
            if (i4RetValue != 1) {
                LOG_ERR("vendorID read failed 0x%x != 0x%x,i4RetValue=%d\n",tmpBuf[0], pData->sensorVendorid >> 24,i4RetValue);
                filp_close(f,NULL);
                return -EFAULT;
            }
            vendorID = tmpBuf[0];
            if(vendorID != pData->sensorVendorid >> 24){
                LOG_ERR("vendorID cmp failed 0x%x != 0x%x\n",vendorID, pData->sensorVendorid >> 24);
                filp_close(f,NULL);
                return -EFAULT;
            }

            //3rd get eeprom data
            //f->f_pos = 0;
            if (pData->dataBuffer == NULL){
                pData->dataBuffer = kmalloc(pData->dataLength, GFP_KERNEL);
                if (pData->dataBuffer == NULL) {
                    LOG_ERR("pData->dataBuffer is malloc fail\n");
                    return -EFAULT;
                }
            }
            i4RetValue = read_region(fd_pdata, pData->dataBuffer, 0x0, pData->dataLength);
            if (i4RetValue != pData->dataLength) {
                kfree(pData->dataBuffer);
                pData->dataBuffer = NULL;
                LOG_ERR("all eeprom data read failed\n");
                filp_close(f,NULL);
                return -EFAULT;
            }else{
                //4th do checksum
                LOG_DBG("all eeprom data read ok\n");
                if(imgSensorCheckEepromData(pData,checkData) != 0){
                    kfree(pData->dataBuffer);
                    pData->dataBuffer = NULL;
                    LOG_ERR("checksum failed\n");
                    filp_close(f,NULL);
                    return -EFAULT;
                }
                LOG_INF("SensorID=%x DeviceID=%x read otp data success\n",pData->sensorID, pData->deviceID);
            }
            filp_close(f,NULL);
            return i4RetValue;
}

int imgSensorSetEepromData(struct stCAM_CAL_DATAINFO_STRUCT* pData){
    int i4RetValue = 0;
    LOG_INF("pData->deviceID = %d\n",pData->deviceID);
    if(pData->deviceID == 0x01){
        if(g_eepromMainData != NULL){
            return -ETXTBSY;
        }
		LOG_DBG("set g_eepromMainData successed\n");
        g_eepromMainData = pData;
    }else if(pData->deviceID == 0x02){
        if(g_eepromSubData != NULL){
            return -ETXTBSY;
        }
        LOG_DBG("set g_eepromSubData successed\n");
        g_eepromSubData= pData;
    }else if(pData->deviceID == 0x10){
        if(g_eepromMainMicroData != NULL){
            return -ETXTBSY;
        }
		LOG_DBG("set g_eepromMainMicroData successed\n");
        g_eepromMainMicroData= pData;
    }else if(pData->deviceID == 0x08){
        if(g_eepromWideData != NULL){
            return -ETXTBSY;
        }
		LOG_DBG("set g_eepromWideData successed\n");
        g_eepromWideData= pData;
    }else{
        LOG_ERR("we don't have this devices\n");
        return -ENODEV;
    }
#if MAIN_OTP_DUMP
    if(pData->dataBuffer)
        dumpEEPROMData(pData->dataLength,pData->dataBuffer);
#endif
    return i4RetValue;
}
//-bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.


static int eeprom_open(struct inode *a_inode, struct file *a_file)
{
	struct EEPROM_DRV_FD_DATA *pdata;
	struct EEPROM_DRV *pdrv;

	pr_debug("open\n");

	pdata = kmalloc(sizeof(struct EEPROM_DRV_FD_DATA), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	pdrv = container_of(a_inode->i_cdev, struct EEPROM_DRV, cdev);

	pdata->pdrv = pdrv;
	pdata->sensor_info.sensor_id = 0;

	a_file->private_data = pdata;

	return 0;
}

static int eeprom_release(struct inode *a_inode, struct file *a_file)
{
	struct EEPROM_DRV_FD_DATA *pdata =
		(struct EEPROM_DRV_FD_DATA *) a_file->private_data;

	pr_debug("release\n");

	kfree(pdata);

	return 0;
}

static ssize_t eeprom_read(struct file *a_file, char __user *user_buffer,
			   size_t size, loff_t *offset)
{
	struct EEPROM_DRV_FD_DATA *pdata =
		(struct EEPROM_DRV_FD_DATA *) a_file->private_data;
	u8 *kbuf = kmalloc(size, GFP_KERNEL);

	pr_debug("read %lu %llu\n", size, *offset);

	if (kbuf == NULL)
		return -ENOMEM;
         //+bug767771, liangyiyi.wt, ADD, 2022/07/21, add hi1336 otp bringup code.
         //+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
         LOG_INF("eeprom_read SensorID=%x\n", pdata->sensor_info.sensor_id);
         LOG_INF("eeprom_read %d,1CAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
            *offset, size);
         if((g_eepromMainData != NULL)&&((W2S5KJN1REARTRULY_SENSOR_ID == pdata->sensor_info.sensor_id) ||
             (W2S5KJN1REARST_SENSOR_ID == pdata->sensor_info.sensor_id) ||
             (W2HI5022QREARTXD_SENSOR_ID == pdata->sensor_info.sensor_id))){
             u32 totalLength = (u32)*offset+ (u32)size;

             if((g_eepromMainData->dataBuffer)&&(totalLength <= g_eepromMainData->dataLength)){
                 if(*offset == 1){//check id
                     if(copy_to_user(user_buffer, (u8*)&g_eepromMainData->sensorVendorid, 4)){
                         return -EFAULT;
                     }
                     LOG_INF("%d,zyk1:ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset,size);
                 } else {//read otp data
                     if(copy_to_user(user_buffer, g_eepromMainData->dataBuffer+(u32)*offset, size)){
                         return -EFAULT;
                     }
                     LOG_INF("%d,zyk2:ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset, size);
                 }
             } else {
                 LOG_INF("maybe some error buf(%p)read(%d)have(%d) \n",g_eepromMainData->dataBuffer,totalLength,g_eepromMainData->dataLength);
                 kfree(kbuf);
                 return -EFAULT;
             }
         } else if((g_eepromSubData != NULL)&&((W2SC501CSFRONTSY_SENSOR_ID == pdata->sensor_info.sensor_id) ||
		 		(W2HI1336FRONTTRULY_SENSOR_ID == pdata->sensor_info.sensor_id) ||
                (W2SC1300MCSFRONTTXD_SENSOR_ID == pdata->sensor_info.sensor_id) ||
				(W2SC1300MCSFRONTST_SENSOR_ID == pdata->sensor_info.sensor_id))){
             u32 totalLength = (u32)*offset+ (u32)size;

             if((g_eepromSubData->dataBuffer)&&(totalLength <= g_eepromSubData->dataLength)){
                 if(*offset == 1){//check id
                     if(copy_to_user(user_buffer, (u8*)&g_eepromSubData->sensorVendorid, 4)){
                         return -EFAULT;
                     }
                     LOG_DBG("%d,zyk1:ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset,size);
                 } else {//read otp data
                     if(copy_to_user(user_buffer, g_eepromSubData->dataBuffer+(u32)*offset, size)){
                         return -EFAULT;
                     }
                     LOG_DBG("%d,zyk2:ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset, size);
                 }
             } else {
                 LOG_INF("maybe some error buf(%p)read(%d)have(%d) \n",g_eepromSubData->dataBuffer,totalLength,g_eepromSubData->dataLength);
                 kfree(kbuf);
                 return -EFAULT;
             }
         } else if((g_eepromMainMicroData != NULL)&&((W2SC202CSMICROLH_SENSOR_ID == pdata->sensor_info.sensor_id)
			 || (W2GC02M1MICROCXT_SENSOR_ID == pdata->sensor_info.sensor_id) || (W2BF2253LMICROSJ_SENSOR_ID == pdata->sensor_info.sensor_id))){
             u32 totalLength = (u32)*offset+ (u32)size;

             if((g_eepromMainMicroData->dataBuffer)&&(totalLength <= g_eepromMainMicroData->dataLength)){
                 if(*offset == 1){//check id
                     if(copy_to_user(user_buffer, (u8*)&g_eepromMainMicroData->sensorVendorid, 4)){
                         return -EFAULT;
                     }
                     LOG_DBG("%d,ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset,size);
                 } else {//read otp data
                     if(copy_to_user(user_buffer, g_eepromMainMicroData->dataBuffer+(u32)*offset, size)){
                         return -EFAULT;
                     }
                     LOG_DBG("%d,ifCAM_CALIOC_G_READ start! offset=%llu, length=%lu\n",__LINE__,
                         *offset, size);
                 }
             } else {
                 LOG_INF("maybe some error buf(%p)read(%d)have(%d) \n",g_eepromMainMicroData->dataBuffer,totalLength,g_eepromMainMicroData->dataLength);
                 kfree(kbuf);
                 return -EFAULT;
             }
         }
		 else {
                 LOG_INF("the camera sensor not have otp data sensor_id:0x%x \n", pdata->sensor_info.sensor_id);
                 kfree(kbuf);
                 return -EFAULT;
         }
         //+bug767771, liudijin.wt, ADD, 2022/07/06, add eeprom bringup code.
         //-bug767771, liangyiyi.wt, ADD, 2022/07/21, add hi1336 otp bringup code.

	*offset += size;
	kfree(kbuf);
	return size;
}

static ssize_t eeprom_write(struct file *a_file, const char __user *user_buffer,
			    size_t size, loff_t *offset)
{
	struct EEPROM_DRV_FD_DATA *pdata =
		(struct EEPROM_DRV_FD_DATA *) a_file->private_data;
	u8 *kbuf = kmalloc(size, GFP_KERNEL);

	pr_debug("write %lu %llu\n", size, *offset);

	if (kbuf == NULL)
		return -ENOMEM;

	if (copy_from_user(kbuf, user_buffer, size) ||
	    write_region(pdata, kbuf, *offset, size) != size) {
		kfree(kbuf);
		return -EFAULT;
	}

	*offset += size;
	kfree(kbuf);
	return size;
}

static loff_t eeprom_seek(struct file *a_file, loff_t offset, int whence)
{
#define MAX_LENGTH 16192 /*MAX 16k bytes*/
	loff_t new_pos = 0;

	switch (whence) {
	case 0: /* SEEK_SET: */
		new_pos = offset;
		break;
	case 1: /* SEEK_CUR: */
		new_pos = a_file->f_pos + offset;
		break;
	case 2: /* SEEK_END: */
		new_pos = MAX_LENGTH + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0)
		return -EINVAL;

	a_file->f_pos = new_pos;

	return new_pos;
}

static long eeprom_ioctl(struct file *a_file, unsigned int a_cmd,
			 unsigned long a_param)
{
	void *pBuff = NULL;
	struct EEPROM_DRV_FD_DATA *pdata =
		(struct EEPROM_DRV_FD_DATA *) a_file->private_data;

	pr_debug("ioctl\n");

	if (_IOC_DIR(a_cmd) == _IOC_NONE)
		return -EFAULT;

	pBuff = kmalloc(_IOC_SIZE(a_cmd), GFP_KERNEL);
	if (pBuff == NULL)
		return -ENOMEM;
	memset(pBuff, 0, _IOC_SIZE(a_cmd));

	if ((_IOC_WRITE & _IOC_DIR(a_cmd)) &&
	    copy_from_user(pBuff,
			   (void *)a_param,
			   _IOC_SIZE(a_cmd))) {

		kfree(pBuff);
		pr_debug("ioctl copy from user failed\n");
		return -EFAULT;
	}

	switch (a_cmd) {
	case CAM_CALIOC_S_SENSOR_INFO:
		pdata->sensor_info.sensor_id =
			((struct CAM_CAL_SENSOR_INFO *)pBuff)->sensor_id;
		pr_debug("sensor id = 0x%x\n",
		       pdata->sensor_info.sensor_id);
		break;
	default:
		kfree(pBuff);
		pr_debug("No such command %d\n", a_cmd);
		return -EPERM;
	}

	kfree(pBuff);
	return 0;
}

#ifdef CONFIG_COMPAT
static long eeprom_compat_ioctl(struct file *a_file, unsigned int a_cmd,
				unsigned long a_param)
{
	pr_debug("compat ioctl\n");

	return 0;
}
#endif

static const struct file_operations geeprom_file_operations = {
	.owner = THIS_MODULE,
	.open = eeprom_open,
	.read = eeprom_read,
	.write = eeprom_write,
	.llseek = eeprom_seek,
	.release = eeprom_release,
	.unlocked_ioctl = eeprom_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = eeprom_compat_ioctl
#endif
};

static inline int retrieve_index(struct i2c_client *client,
				 unsigned int *index)
{
	const char *node_name = client->dev.of_node->name;
	const size_t prefix_len = strlen(DEV_NODE_NAME_PREFIX);

	if (strncmp(node_name, DEV_NODE_NAME_PREFIX, prefix_len) == 0 &&
	    kstrtouint(node_name + prefix_len, 10, index) == 0) {
		pr_debug("index = %u\n", *index);
		return 0;
	}

	pr_err("invalid node name format\n");
	*index = 0;
	return -EINVAL;
}

static inline int eeprom_driver_register(struct i2c_client *client,
					 unsigned int index)
{
	int ret = 0;
	struct EEPROM_DRV *pinst;
	char device_drv_name[DEV_NAME_STR_LEN_MAX] = { 0 };
	char class_drv_name[DEV_NAME_STR_LEN_MAX] = { 0 };

	if (index >= MAX_EEPROM_NUMBER) {
		pr_err("node index out of bound\n");
		return -EINVAL;
	}

	ret = snprintf(device_drv_name, DEV_NAME_STR_LEN_MAX - 1,
		DEV_NAME_FMT, index);
	if (ret < 0) {
		pr_info(
		"[eeprom]%s error, ret = %d", __func__, ret);
		return -EFAULT;
	}
	ret = snprintf(class_drv_name, DEV_NAME_STR_LEN_MAX - 1,
		DEV_CLASS_NAME_FMT, index);
	if (ret < 0) {
		pr_info(
		"[eeprom]%s error, ret = %d", __func__, ret);
		return -EFAULT;
	}

	ret = 0;
	pinst = &ginst_drv[index];
	pinst->dev_no = MKDEV(EEPROM_DEVICE_NNUMBER, index);

	if (alloc_chrdev_region(&pinst->dev_no, 0, 1, device_drv_name)) {
		pr_err("Allocate device no failed\n");
		return -EAGAIN;
	}

	/* Attatch file operation. */
	cdev_init(&pinst->cdev, &geeprom_file_operations);

	/* Add to system */
	if (cdev_add(&pinst->cdev, pinst->dev_no, 1)) {
		pr_err("Attatch file operation failed\n");
		unregister_chrdev_region(pinst->dev_no, 1);
		return -EAGAIN;
	}

	memcpy(pinst->class_name, class_drv_name, DEV_NAME_STR_LEN_MAX);
	pinst->pclass = class_create(THIS_MODULE, pinst->class_name);
	if (IS_ERR(pinst->pclass)) {
		ret = PTR_ERR(pinst->pclass);

		pr_err("Unable to create class, err = %d\n", ret);
		return ret;
	}

	device_create(pinst->pclass, NULL, pinst->dev_no, NULL,
		      device_drv_name);

	pinst->pi2c_client = client;
	mutex_init(&pinst->eeprom_mutex);

	return ret;
}

static inline int eeprom_driver_unregister(unsigned int index)
{
	struct EEPROM_DRV *pinst = NULL;

	if (index >= MAX_EEPROM_NUMBER) {
		pr_err("node index out of bound\n");
		return -EINVAL;
	}

	pinst = &ginst_drv[index];

	/* Release char driver */
	unregister_chrdev_region(pinst->dev_no, 1);

	device_destroy(pinst->pclass, pinst->dev_no);
	class_destroy(pinst->pclass);

	return 0;
}

static int eeprom_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	unsigned int index = 0;

	pr_debug("probe start name: %s\n", client->dev.of_node->name);

	if (retrieve_index(client, &index) < 0)
		return -EINVAL;
	else
		return eeprom_driver_register(client, index);
}

static int eeprom_remove(struct i2c_client *client)
{
	unsigned int index = 0;

	pr_debug("remove name: %s\n", client->dev.of_node->name);

	if (retrieve_index(client, &index) < 0)
		return -EINVAL;
	else
		return eeprom_driver_unregister(index);
}

static const struct of_device_id eeprom_of_match[] = {
	{ .compatible = "mediatek,camera_eeprom", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eeprom_of_match);

static struct i2c_driver eeprom_i2c_init = {
	.driver = {
		.name   = "mediatek,camera_eeprom",
		.of_match_table = of_match_ptr(eeprom_of_match),
	},
	.probe      = eeprom_probe,
	.remove     = eeprom_remove,
};

module_i2c_driver(eeprom_i2c_init);

MODULE_DESCRIPTION("camera eeprom driver");
MODULE_AUTHOR("Mediatek");
MODULE_LICENSE("GPL v2");

