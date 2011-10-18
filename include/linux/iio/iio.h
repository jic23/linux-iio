/*
 * The industrial I/O core
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _IIO_H_
#define _IIO_H_

#include <linux/klist.h>
#include <linux/device.h>
#include <linux/iio/types.h>

/* Minimum alignment of priv within iio_dev */
#define IIO_ALIGN L1_CACHE_BYTES

enum iio_data_type {
	IIO_RAW,
	IIO_PROCESSED,
};

enum iio_chan_info_enum {
	IIO_CHAN_INFO_SCALE = 1,
	IIO_CHAN_INFO_OFFSET,
	IIO_CHAN_INFO_CALIBSCALE,
	IIO_CHAN_INFO_CALIBBIAS,
	IIO_CHAN_INFO_PEAK,
	IIO_CHAN_INFO_PEAK_SCALE,
	IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW,
};

#define IIO_CHAN_INFO_SHARED_BIT(type) BIT(type*2)
#define IIO_CHAN_INFO_SEPARATE_BIT(type) BIT(type*2 + 1)

#define IIO_CHAN_INFO_SCALE_SEPARATE_BIT		\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_SCALE)
#define IIO_CHAN_INFO_SCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_SCALE)
#define IIO_CHAN_INFO_OFFSET_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_OFFSET)
#define IIO_CHAN_INFO_OFFSET_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_OFFSET)
#define IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_CALIBSCALE)
#define IIO_CHAN_INFO_CALIBSCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_CALIBSCALE)
#define IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_CALIBBIAS)
#define IIO_CHAN_INFO_CALIBBIAS_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_CALIBBIAS)
#define IIO_CHAN_INFO_PEAK_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_PEAK)
#define IIO_CHAN_INFO_PEAK_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_PEAK)
#define IIO_CHAN_INFO_PEAKSCALE_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_PEAKSCALE)
#define IIO_CHAN_INFO_PEAKSCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_PEAKSCALE)
#define IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW_SEPARATE_BIT	\
	IIO_CHAN_INFO_SEPARATE_BIT(				\
		IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW)
#define IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW_SHARED_BIT	\
	IIO_CHAN_INFO_SHARED_BIT(				\
		IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW)
#define IIO_CHAN_INFO_AVERAGE_RAW_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_AVERAGE_RAW)
#define IIO_CHAN_INFO_AVERAGE_RAW_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_AVERAGE_RAW)


enum iio_direction {
	IIO_IN,
	IIO_OUT,
};

/**
 * struct iio_chan_spec - specification of a single channel
 * @type:		What type of measurement is the channel making.
 * @channel:		What number or name do we wish to asign the channel.
 * @channel2:		If there is a second number for a differential
 *			channel then this is it. If modified is set then the
 *			value here specifies the modifier.
 * @address:		Driver specific identifier.
 * @scan_type:		Description of data format.
 * @info_mask:		What information is to be exported about this channel.
 *			This includes calibbias, scale etc.
 * @extend_name:	Allows labeling of channel attributes with an
 *			informative name. Note this has no effect codes etc,
 *			unlike modifiers.
 * @processed_val:	Flag to specify the data access attribute should be
 *			*_input rather than *_raw.
 * @modified:		Does a modifier apply to this channel. What these are
 *			depends on the channel type.  Modifier is set in
 *			channel2. Examples are IIO_MOD_X for axial sensors about
 *			the 'x' axis.
 * @indexed:		Specify the channel has a numerical index. If not,
 *			the value in channel will be suppressed for attribute
 *			but not for event codes. Typically set it to 0 when
 *			the index is false.
 * @output:		Specify the channel is an output channel (DAC).
 * @differential:	Is the channel a differential channel. Cannot coexist
 *			with modified and requires indexed.
 */
struct iio_chan_spec {
	enum iio_chan_type	type;
	int			channel;
	int			channel2;
	unsigned long		address;
	/**
	 * struct scan_type - description of the data format
	 * @sign:	Set if signed value
	 * @realbits:	Number of valid bits of data
	 * @shift:	Shift right by this before masking out realbits.
	 */
	struct {
		char		sign;
		u8		realbits;
		u8		shift;
	} scan_type;
	long                    info_mask;
	char			*extend_name;
	const char		*datasheet_name;
	unsigned		processed_val:1;
	unsigned		modified:1;
	unsigned		indexed:1;
	unsigned		output:1;
	unsigned		differential:1;
};

struct iio_dev;

/**
 * struct iio_info - constant information about device
 * @driver_module:	module structure used to ensure correct
 *			ownership of chrdevs etc
 * @attrs:		general purpose device attributes
 * @read_raw:		function to request a value from the device.
 *			mask specifies which value. Note 0 means a reading of
 *			the channel in question.  Return value will specify the
 *			type of value returned by the device. val and val2 will
 *			contain the elements making up the returned value.
 * @write_raw:		function to write a value to the device.
 *			Parameters are the same as for read_raw.
 * @write_raw_get_fmt:	callback function to query the expected
 *			format/precision. If not set by the driver, write_raw
 *			returns IIO_VAL_INT_PLUS_MICRO.
 **/
struct iio_info {
	struct module			*driver_module;
	const struct attribute_group	*attrs;

	int (*read_raw)(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask);

	int (*write_raw)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int val,
			 int val2,
			 long mask);

	int (*write_raw_get_fmt)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 long mask);
};

/**
 * struct iio_dev - industrial I/O device
 * @id:			[INTERN] used to identify device internally
 * @dev:		[DRIVER] device structure, should be assigned a parent
 *			and owner
 * @mlock:		[INTERN] lock used to prevent simultaneous device state
 *			changes
 * @available_scan_masks: [DRIVER] optional array of allowed bitmasks
 * @channels:		[DRIVER] channel specification structure table
 * @num_channels:	[DRIVER] number of chanels specified in @channels
 * @channel_attr_list:	[INTERN] keep track of automatically created channel
 *			attributes
 * @chan_attr_group:	[INTERN] group for all attrs in base directory
 * @name:		[DRIVER] name of the device
 * @info:		[DRIVER] callbacks and constant info from driver
 * @groups:		[INTERN] attribute groups
 * @groupcounter:	[INTERN] index of next attribute group
 **/
struct iio_dev {
	int				id;
	struct device			dev;
	struct mutex			mlock;
	unsigned long			*available_scan_masks;
	struct iio_chan_spec const	*channels;
	int				num_channels;
	struct list_head		channel_attr_list;
	struct attribute_group		chan_attr_group;
	const char			*name;
	const struct iio_info		*info;
#define IIO_MAX_GROUPS 1
	const struct attribute_group	*groups[IIO_MAX_GROUPS + 1];
	int				groupcounter;
};

/**
 * iio_device_allocate() - allocate an iio_dev from a driver
 * @sizeof_priv:	Space to allocate for private structure.
 **/
struct iio_dev *iio_device_allocate(int sizeof_priv);

static inline void *iio_priv(const struct iio_dev *dev)
{
	return (char *)dev + ALIGN(sizeof(struct iio_dev), IIO_ALIGN);
}

/**
 * iio_device_free() - free an iio_dev from a driver
 * @dev: the iio_dev associated with the device
 **/
void iio_device_free(struct iio_dev *dev);

/**
 * iio_device_register() - register a device with the IIO subsystem
 * @indio_dev:		Device structure filled by the device driver
 **/
int iio_device_register(struct iio_dev *indio_dev);

/**
 * iio_device_unregister() - unregister a device from the IIO subsystem
 * @indio_dev:		Device structure representing the device.
 **/
void iio_device_unregister(struct iio_dev *indio_dev);

/**
 * iio_put_device() - reference counted deallocation of struct device
 * @indio_dev: the iio_device containing the device
 **/
static inline void iio_put_device(struct iio_dev *indio_dev)
{
	if (indio_dev)
		put_device(&indio_dev->dev);
};

#endif /* _IIO_H_ */
