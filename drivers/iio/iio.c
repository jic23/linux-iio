/* The industrial I/O core
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on elements of hwmon and input subsystems.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

static DEFINE_IDA(iio_ida);

static struct bus_type iio_bus_type = {
	.name = "iio",
};

static const char * const iio_data_type_name[] = {
	[IIO_RAW] = "raw",
	[IIO_PROCESSED] = "input",
};

static const char * const iio_chan_type_name_spec[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_ACCEL] = "accel",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_MAGN] = "magn",
	[IIO_LIGHT] = "illuminance",
	[IIO_INTENSITY] = "intensity",
	[IIO_PROXIMITY] = "proximity",
	[IIO_TEMP] = "temp",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_ANGL] = "angl",
	[IIO_TIMESTAMP] = "timestamp",
};

static const char * const iio_direction_name[] = {
	[IIO_IN] = "in",
	[IIO_OUT] = "out",
};

static const char * const iio_modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_INFRARED] = "ir",
};

static const char * const iio_chan_info_postfix[] = {
	[IIO_CHAN_INFO_SCALE] = "scale",
	[IIO_CHAN_INFO_OFFSET] = "offset",
	[IIO_CHAN_INFO_CALIBSCALE] = "calibscale",
	[IIO_CHAN_INFO_CALIBBIAS] = "calibbias",
	[IIO_CHAN_INFO_PEAK] = "peak_raw",
	[IIO_CHAN_INFO_PEAK_SCALE] = "peak_scale",
	[IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW] = "quadrature_correction_raw",
};

static void iio_device_free_read_attr(struct iio_dev *indio_dev,
						 struct iio_dev_attr *p)
{
	kfree(p->dev_attr.attr.name);
	kfree(p);
}

static void iio_device_unregister_sysfs(struct iio_dev *indio_dev)
{

	struct iio_dev_attr *p, *n;

	list_for_each_entry_safe(p, n, &indio_dev->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_free_read_attr(indio_dev, p);
	}
	kfree(indio_dev->chan_attr_group.attrs);
}

static void iio_dev_release(struct device *device)
{
	struct iio_dev *indio_dev = container_of(device, struct iio_dev, dev);
	iio_device_unregister_sysfs(indio_dev);
}

static struct device_type iio_dev_type = {
	.name = "iio_device",
	.release = iio_dev_release,
};

struct iio_dev *iio_device_allocate(int sizeof_priv)
{
	struct iio_dev *dev;
	size_t alloc_size;

	alloc_size = sizeof(struct iio_dev);
	if (sizeof_priv) {
		alloc_size = ALIGN(alloc_size, IIO_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure cacheline alignment of whole construct */
	alloc_size += IIO_ALIGN - 1;

	dev = kzalloc(alloc_size, GFP_KERNEL);

	if (dev) {
		dev->dev.groups = dev->groups;
		dev->dev.type = &iio_dev_type;
		dev->dev.bus = &iio_bus_type;
		device_initialize(&dev->dev);
		dev_set_drvdata(&dev->dev, (void *)dev);
		mutex_init(&dev->mlock);
	}

	return dev;
}
EXPORT_SYMBOL_GPL(iio_device_allocate);

void iio_device_free(struct iio_dev *dev)
{
	if (dev)
		iio_put_device(dev);
}
EXPORT_SYMBOL_GPL(iio_device_free);

ssize_t __iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s\n",
		       container_of(attr,
				    struct iio_const_attr,
				    dev_attr)->string);
}
EXPORT_SYMBOL_GPL(__iio_read_const_attr);

static ssize_t iio_read_channel_info(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val, val2;
	int ret = indio_dev->info->read_raw(indio_dev, this_attr->c,
					    &val, &val2, this_attr->address);

	if (ret < 0)
		return ret;

	if (ret == IIO_VAL_INT)
		return sprintf(buf, "%d\n", val);
	else if (ret == IIO_VAL_INT_PLUS_MICRO) {
		if (val2 < 0)
			return sprintf(buf, "-%d.%06u\n", val, -val2);
		else
			return sprintf(buf, "%d.%06u\n", val, val2);
	} else if (ret == IIO_VAL_INT_PLUS_NANO) {
		if (val2 < 0)
			return sprintf(buf, "-%d.%09u\n", val, -val2);
		else
			return sprintf(buf, "%d.%09u\n", val, val2);
	} else
		return 0;
}

static ssize_t iio_write_channel_info(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret, integer = 0, fract = 0, fract_mult = 100000;
	bool integer_part = true, negative = false;

	/* Assumes decimal - precision based on number of digits */
	if (!indio_dev->info->write_raw)
		return -EINVAL;

	if (indio_dev->info->write_raw_get_fmt)
		switch (indio_dev->info->write_raw_get_fmt(indio_dev,
			this_attr->c, this_attr->address)) {
		case IIO_VAL_INT_PLUS_MICRO:
			fract_mult = 100000;
			break;
		case IIO_VAL_INT_PLUS_NANO:
			fract_mult = 100000000;
			break;
		default:
			return -EINVAL;
		}

	if (buf[0] == '-') {
		negative = true;
		buf++;
	}

	while (*buf) {
		if ('0' <= *buf && *buf <= '9') {
			if (integer_part)
				integer = integer*10 + *buf - '0';
			else {
				fract += fract_mult*(*buf - '0');
				if (fract_mult == 1)
					break;
				fract_mult /= 10;
			}
		} else if (*buf == '\n') {
			if (*(buf + 1) == '\0')
				break;
			else
				return -EINVAL;
		} else if (*buf == '.') {
			integer_part = false;
		} else {
			return -EINVAL;
		}
		buf++;
	}
	if (negative) {
		if (integer)
			integer = -integer;
		else
			fract = -fract;
	}

	ret = indio_dev->info->write_raw(indio_dev, this_attr->c,
					 integer, fract, this_attr->address);
	if (ret)
		return ret;

	return len;
}

static
int __iio_device_attr_init(struct device_attribute *dev_attr,
			   const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   bool generic)
{
	int ret;
	char *name_format, *full_postfix;
	sysfs_attr_init(&dev_attr->attr);

	/* Build up postfix of <extend_name>_<modifier>_postfix */
	if (chan->modified && !generic) {
		if (chan->extend_name)
			full_postfix = kasprintf(GFP_KERNEL, "%s_%s_%s",
						 iio_modifier_names[chan
								    ->channel2],
						 chan->extend_name,
						 postfix);
		else
			full_postfix = kasprintf(GFP_KERNEL, "%s_%s",
						 iio_modifier_names[chan
								    ->channel2],
						 postfix);
	} else {
		if (chan->extend_name == NULL)
			full_postfix = kstrdup(postfix, GFP_KERNEL);
		else
			full_postfix = kasprintf(GFP_KERNEL,
						 "%s_%s",
						 chan->extend_name,
						 postfix);
	}
	if (full_postfix == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	if (chan->differential) { /* Differential  can not have modifier */
		if (generic)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s-%s_%s",
					    iio_direction_name[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
		else if (chan->indexed)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s%d-%s%d_%s",
					    iio_direction_name[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    chan->channel,
					    iio_chan_type_name_spec[chan->type],
					    chan->channel2,
					    full_postfix);
		else {
			WARN_ON("Differential channels must be indexed\n");
			ret = -EINVAL;
			goto error_free_full_postfix;
		}
	} else { /* Single ended */
		if (generic)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s_%s",
					    iio_direction_name[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
		else if (chan->indexed)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s%d_%s",
					    iio_direction_name[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    chan->channel,
					    full_postfix);
		else
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s_%s",
					    iio_direction_name[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
	}
	if (name_format == NULL) {
		ret = -ENOMEM;
		goto error_free_full_postfix;
	}
	dev_attr->attr.name = kasprintf(GFP_KERNEL,
					name_format,
					chan->channel,
					chan->channel2);
	if (dev_attr->attr.name == NULL) {
		ret = -ENOMEM;
		goto error_free_name_format;
	}

	if (readfunc) {
		dev_attr->attr.mode |= S_IRUGO;
		dev_attr->show = readfunc;
	}

	if (writefunc) {
		dev_attr->attr.mode |= S_IWUSR;
		dev_attr->store = writefunc;
	}
	kfree(name_format);
	kfree(full_postfix);

	return 0;

error_free_name_format:
	kfree(name_format);
error_free_full_postfix:
	kfree(full_postfix);
error_ret:
	return ret;
}

static void __iio_device_attr_deinit(struct device_attribute *dev_attr)
{
	kfree(dev_attr->attr.name);
}

static
int __iio_add_chan_devattr(const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   u64 mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list)
{
	int ret;
	struct iio_dev_attr *iio_attr, *t;

	iio_attr = kzalloc(sizeof *iio_attr, GFP_KERNEL);
	if (iio_attr == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = __iio_device_attr_init(&iio_attr->dev_attr,
				     postfix, chan,
				     readfunc, writefunc, generic);
	if (ret)
		goto error_iio_dev_attr_free;
	iio_attr->c = chan;
	iio_attr->address = mask;
	list_for_each_entry(t, attr_list, l)
		if (strcmp(t->dev_attr.attr.name,
			   iio_attr->dev_attr.attr.name) == 0) {
			if (!generic)
				dev_err(dev, "tried to double register : %s\n",
					t->dev_attr.attr.name);
			ret = -EBUSY;
			goto error_device_attr_deinit;
		}

	list_add(&iio_attr->l, attr_list);

	return 0;

error_device_attr_deinit:
	__iio_device_attr_deinit(&iio_attr->dev_attr);
error_iio_dev_attr_free:
	kfree(iio_attr);
error_ret:
	return ret;
}

static int iio_device_add_channel_sysfs(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan)
{
	int ret, i, attrcount = 0;

	if (chan->channel < 0)
		return 0;
	ret = __iio_add_chan_devattr(iio_data_type_name[chan->processed_val],
				     chan,
				     &iio_read_channel_info,
				     (chan->output ?
				      &iio_write_channel_info : NULL),
				     0,
				     0,
				     &indio_dev->dev,
				     &indio_dev->channel_attr_list);
	if (ret)
		goto error_ret;
	attrcount++;

	for_each_set_bit(i, &chan->info_mask,
			 ARRAY_SIZE(iio_chan_info_postfix)*2) {
		ret = __iio_add_chan_devattr(iio_chan_info_postfix[i/2],
					     chan,
					     &iio_read_channel_info,
					     &iio_write_channel_info,
					     i/2,
					     !(i%2),
					     &indio_dev->dev,
					     &indio_dev->channel_attr_list);
		if (ret == -EBUSY && (i%2 == 0)) {
			ret = 0;
			continue;
		}
		if (ret < 0)
			goto error_ret;
		attrcount++;
	}
	ret = attrcount;
error_ret:
	return ret;
}

static ssize_t iio_show_dev_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", indio_dev->name);
}

static DEVICE_ATTR(name, S_IRUGO, iio_show_dev_name, NULL);

static int iio_device_register_sysfs(struct iio_dev *indio_dev)
{
	int i, ret = 0, attrcount, attrn, attrcount_orig = 0;
	struct iio_dev_attr *p, *n;
	struct attribute **attr;

	/* First count elements in any existing group */
	if (indio_dev->info->attrs) {
		attr = indio_dev->info->attrs->attrs;
		while (*attr++ != NULL)
			attrcount_orig++;
	}
	attrcount = attrcount_orig;

	INIT_LIST_HEAD(&indio_dev->channel_attr_list);
	if (indio_dev->channels)
		for (i = 0; i < indio_dev->num_channels; i++) {
			ret = iio_device_add_channel_sysfs(indio_dev,
							   &indio_dev
							   ->channels[i]);
			if (ret < 0)
				goto error_clear_attrs;
			attrcount += ret;
		}
	if (indio_dev->name)
		attrcount++;

	indio_dev->chan_attr_group.attrs
		= kzalloc(sizeof(indio_dev->chan_attr_group.attrs[0])*
			  (attrcount + 1),
			  GFP_KERNEL);
	if (indio_dev->chan_attr_group.attrs == NULL) {
		ret = -ENOMEM;
		goto error_clear_attrs;
	}
	/* Copy across original attributes */
	if (indio_dev->info->attrs)
		memcpy(indio_dev->chan_attr_group.attrs,
		       indio_dev->info->attrs->attrs,
		       sizeof(indio_dev->chan_attr_group.attrs[0])
		       *attrcount_orig);
	attrn = attrcount_orig;
	/* Add all elements from the list. */
	list_for_each_entry(p, &indio_dev->channel_attr_list, l)
		indio_dev->chan_attr_group.attrs[attrn++] = &p->dev_attr.attr;
	if (indio_dev->name)
		indio_dev->chan_attr_group.attrs[attrn++] = &dev_attr_name.attr;

	indio_dev->groups[indio_dev->groupcounter++] =
		&indio_dev->chan_attr_group;

	return 0;

error_clear_attrs:
	list_for_each_entry_safe(p, n,
				 &indio_dev->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_free_read_attr(indio_dev, p);
	}

	return ret;
}

int iio_device_register(struct iio_dev *indio_dev)
{
	int ret;

	indio_dev->id = ida_simple_get(&iio_ida, 0, 0, GFP_KERNEL);
	if (indio_dev->id < 0) {
		ret = indio_dev->id;
		goto error_ret;
	}

	dev_set_name(&indio_dev->dev, "iio:device%d", indio_dev->id);

	ret = iio_device_register_sysfs(indio_dev);
	if (ret) {
		dev_err(indio_dev->dev.parent,
			"Failed to register sysfs interfaces\n");
		goto error_free_ida;
	}
	ret = device_add(&indio_dev->dev);
	if (ret)
		goto error_free_sysfs;

	return 0;

error_free_sysfs:
	iio_device_unregister_sysfs(indio_dev);
error_free_ida:
	ida_simple_remove(&iio_ida, indio_dev->id);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_device_register);

void iio_device_unregister(struct iio_dev *indio_dev)
{
	device_unregister(&indio_dev->dev);
}
EXPORT_SYMBOL(iio_device_unregister);

static int __init iio_init(void)
{
	return bus_register(&iio_bus_type);
}
subsys_initcall(iio_init);

static void __exit iio_exit(void)
{
	bus_unregister(&iio_bus_type);
}
module_exit(iio_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Industrial I/O core");
MODULE_LICENSE("GPL");
