/*
 * device_core.h - 通用的设备驱动框架
 *
 * 整体结构:
 * 	device_core.h/c		核心层
 * 	ioctl_cmd.h			命令定义
 * 	xxx_driver.c		各种外设实现
 * 	board_init.c		集中注册和初始化
 *
 * 如何新增一个外设:
 *	1. 实现device_ops的函数 (init/read/write/ioctl)
 *	2. 定义 struct device实例
 *	3. 在board_init中集中注册, 初始化可以选择实现
 */

#ifndef __DEVICE_CORE_H_
#define __DEVICE_CORE_H_

#include <stdint.h>
#include <stddef.h>

struct device;

/* ------------------------------------------------------------------ */
/*                        通知回调 - 中断中回调通知上层                                                                  */
/* ------------------------------------------------------------------ */
typedef void (*device_notify_t)(struct device *dev, uint32_t event);

/* ------------------------------------------------------------------ */
/*                        操作函数表 - 同类设备共享                                                                         */
/* ------------------------------------------------------------------ */
struct device_ops {
	int (*init) 	(struct device *dev);
	int (*read) 	(struct device *dev, void *buf, size_t len);
	int (*write)	(struct device *dev, const void *buf, size_t len);
	int (*ioctl)	(struct device *dev, int cmd, void *arg);
};

/* ------------------------------------------------------------------ */
/*                             设备基类                                                                                            */
/* ------------------------------------------------------------------ */
struct device {
	const char 				*name;				/* 每个设备独一无二的名称 */
	const struct device_ops *ops;				/* 每个设备的操作函数-同类设备共享 */
	void   					*priv;				/* 驱动私有数据 */
	device_notify_t			 notify;			/* 通知回调 */
};

/* ------------------------------------------------------------------ */
/*                             API实现                                                                                            */
/* ------------------------------------------------------------------ */
int 			device_register	(struct device *dev);
struct device*	device_find		(const char *name);
int				device_init		(struct device *dev);
int 			device_read		(struct device *dev, void *buf, size_t len);
int				device_write	(struct device *dev, const void *buf, size_t len);
int				device_ioctl	(struct device *dev, int cmd, void *arg);


#endif /* __DEVICE_CORE_H_ */
