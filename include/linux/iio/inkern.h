/*
 * The industrial I/O core - in kernel channel mapping infrastructure
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _IIO_INKERN_H_
#define _IIO_INKERN_H_
#include <linux/device.h>
#include <linux/list.h>
#include <linux/iio/types.h>

struct iio_dev;
struct iio_chan_spec;

struct iio_channel {
	struct iio_dev *indio_dev;
	const struct iio_chan_spec *channel;
};

extern struct list_head iio_map_list;

struct iio_map {
	/* iio device side */
	struct device *adc_dev;
	const char *adc_dev_name;
	const char *adc_channel_label;
	int channel_number; /*naughty starting point */

	/* consumer side */
	struct device *consumer_dev;
	const char *consumer_dev_name;
	const char *consumer_channel;
	/* management - probably neater ways of doing this */
	struct list_head l;
};

void iio_map_array_register(struct iio_map *map, int nummaps);
/**
 * iio_channel_get() - get an opaque reference to a specified device.
 */
struct iio_channel *iio_channel_get(const struct device *dev,
				    const char *name,
				    const char *consumer_channel);
void iio_channel_release(struct iio_channel *chan);

/**
 * iio_channel_get_all() - get all channels associated with a client
 *
 * returns a null terminated array of pointers to iio_channel structures.
 */
struct iio_channel **iio_channel_get_all(const struct device *dev,
					const char *name);

void iio_channel_release_all(struct iio_channel **chan);

/**
 * iio_st_read_channel_raw() - read from a given channel
 * @channel:	the channel being queried.
 * @val:	value read back.
 *
 * Note raw reads from iio channels are in adc counts and hence
 * scale will need to be applied if standard units required.
 *
 * Maybe want to pass the type as a sanity check.
 */
int iio_read_channel_raw(struct iio_channel *chan,
			    int *val);

/**
 * iio_get_channel_type() - get the type of a channel
 * @channel:	the channel being queried.
 *
 * returns the enum iio_chan_type of the channel
 */
enum iio_chan_type iio_get_channel_type(struct iio_channel *channel);

/**
 * iio_read_channel_scale() - read the scale value for a channel
 * @channel:	the channel being queried.
 * @val:	first part of value read back.
 * @val2:	second part of value read back.
 *
 * Note returns a description of what is in val and val2, such
 * as IIO_VAL_INT_PLUS_MICRO telling us we have a value of val
 * + val2/1e6
 */
int iio_read_channel_scale(struct iio_channel *chan, int *val,
			      int *val2);

#endif
