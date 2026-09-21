#ifndef RF_SWITCH_UAPI_H
#define RF_SWITCH_UAPI_H

/* 射频路径选择字符设备的内核/用户态 ABI。路径编号须与硬件电平表一起评审。 */
#include <linux/ioctl.h>
#include <linux/types.h>

enum rf_switch_path {
    RF_SWITCH_PATH_SAFE = 0,
    RF_SWITCH_PATH_OPEN = 1,
    RF_SWITCH_PATH_SHORT = 2,
    RF_SWITCH_PATH_LOAD = 3,
    RF_SWITCH_PATH_THRU = 4,
    RF_SWITCH_PATH_DUT = 5,
};

#define RF_SWITCH_PATH_MAX RF_SWITCH_PATH_DUT

#define RF_SWITCH_IOC_MAGIC 'R'
#define RF_SWITCH_SET_PATH _IOW(RF_SWITCH_IOC_MAGIC, 1, __u32)
#define RF_SWITCH_GET_PATH _IOR(RF_SWITCH_IOC_MAGIC, 2, __u32)

#endif
