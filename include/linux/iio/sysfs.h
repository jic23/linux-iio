/*
 * The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * General attributes
 */

#include <linux/klist.h>
#include <linux/device.h>

#ifndef _IIO_SYSFS_H_
#define _IIO_SYSFS_H_

struct iio_chan_spec;

/**
 * struct iio_dev_attr - iio specific device attribute
 * @dev_attr:	underlying device attribute
 * @address:	associated register address
 * @l:		list head for maintaining list of dynamically created attrs
 * @c:		channel spec for channel with which attr is associated if any
 */
struct iio_dev_attr {
	struct device_attribute dev_attr;
	int address;
	struct list_head l;
	struct iio_chan_spec const *c;
};

#define to_iio_dev_attr(_dev_attr)				\
	container_of(_dev_attr, struct iio_dev_attr, dev_attr)

#define IIO_ATTR(_name, _mode, _show, _store, _addr)		\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .address = _addr }

#define IIO_DEVICE_ATTR(_name, _mode, _show, _store, _addr)	\
	struct iio_dev_attr iio_dev_attr_##_name		\
	= IIO_ATTR(_name, _mode, _show, _store, _addr)

/**
 * struct iio_const_attr - constant device specific attribute
 *                         often used for things like available modes
 * @string:	attribute string
 * @dev_attr:	underlying device attribute
 */
struct iio_const_attr {
	const char *string;
	struct device_attribute dev_attr;
};

#define to_iio_const_attr(_dev_attr) \
	container_of(_dev_attr, struct iio_const_attr, dev_attr)

ssize_t __iio_read_const_attr(struct device *dev,
			      struct device_attribute *attr,
			      char *len);

#define IIO_CONST_ATTR(_name, _string)					\
	struct iio_const_attr iio_const_attr_##_name			\
	= { .string = _string,						\
	    .dev_attr = __ATTR(_name, S_IRUGO, __iio_read_const_attr, NULL) }
#endif /* _IIO_SYSFS_H_ */
