/*
 * iio/adc/max1363.c
 *
 * Copyright (C) 2008-2011 Jonathan Cameron
 *
 * based on linux/drivers/i2c/chips/max123x
 * Copyright (C) 2002-2004 Stefan Eletzhofer
 *
 * based on linux/drivers/acron/char/pcf8583.c
 * Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "max1363.h"

#define MAX1363_MODE_SINGLE(_num, _mask) {				\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_SINGLE_1			\
			| MAX1363_CONFIG_SE,				\
			.modemask[0] = _mask,				\
			}

#define MAX1363_MODE_SCAN_TO_CHANNEL(_num, _mask) {			\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_TO_CS			\
			| MAX1363_CONFIG_SE,				\
			.modemask[0] = _mask,				\
			}

/* note not available for max1363 hence naming */
#define MAX1236_MODE_SCAN_MID_TO_CHANNEL(_mid, _num, _mask) {		\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1236_SCAN_MID_TO_CHANNEL			\
			| MAX1363_CONFIG_SE,				\
			.modemask[0] = _mask				\
}

#define MAX1363_MODE_DIFF_SINGLE(_nump, _numm, _mask) {			\
		.conf = MAX1363_CHANNEL_SEL(_nump)			\
			| MAX1363_CONFIG_SCAN_SINGLE_1			\
			| MAX1363_CONFIG_DE,				\
			.modemask[0] = _mask				\
			}

/* Can't think how to automate naming so specify for now */
#define MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(_num, _numvals, _mask) {	\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1363_CONFIG_SCAN_TO_CS			\
			| MAX1363_CONFIG_DE,				\
			.modemask[0] = _mask				\
			}

/* note not available for max1363 hence naming */
#define MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(_num, _numvals, _mask) {	\
		.conf = MAX1363_CHANNEL_SEL(_num)			\
			| MAX1236_SCAN_MID_TO_CHANNEL			\
			| MAX1363_CONFIG_SE,				\
			.modemask[0] = _mask				\
}

static const struct max1363_mode max1363_mode_table[] = {
	/* All of the single channel options first */
	MAX1363_MODE_SINGLE(0, 1 << 0),
	MAX1363_MODE_SINGLE(1, 1 << 1),
	MAX1363_MODE_SINGLE(2, 1 << 2),
	MAX1363_MODE_SINGLE(3, 1 << 3),
	MAX1363_MODE_SINGLE(4, 1 << 4),
	MAX1363_MODE_SINGLE(5, 1 << 5),
	MAX1363_MODE_SINGLE(6, 1 << 6),
	MAX1363_MODE_SINGLE(7, 1 << 7),
	MAX1363_MODE_SINGLE(8, 1 << 8),
	MAX1363_MODE_SINGLE(9, 1 << 9),
	MAX1363_MODE_SINGLE(10, 1 << 10),
	MAX1363_MODE_SINGLE(11, 1 << 11),

	MAX1363_MODE_DIFF_SINGLE(0, 1, 1 << 12),
	MAX1363_MODE_DIFF_SINGLE(2, 3, 1 << 13),
	MAX1363_MODE_DIFF_SINGLE(4, 5, 1 << 14),
	MAX1363_MODE_DIFF_SINGLE(6, 7, 1 << 15),
	MAX1363_MODE_DIFF_SINGLE(8, 9, 1 << 16),
	MAX1363_MODE_DIFF_SINGLE(10, 11, 1 << 17),
	MAX1363_MODE_DIFF_SINGLE(1, 0, 1 << 18),
	MAX1363_MODE_DIFF_SINGLE(3, 2, 1 << 19),
	MAX1363_MODE_DIFF_SINGLE(5, 4, 1 << 20),
	MAX1363_MODE_DIFF_SINGLE(7, 6, 1 << 21),
	MAX1363_MODE_DIFF_SINGLE(9, 8, 1 << 22),
	MAX1363_MODE_DIFF_SINGLE(11, 10, 1 << 23),

	/* The multichannel scans next */
	MAX1363_MODE_SCAN_TO_CHANNEL(1, 0x003),
	MAX1363_MODE_SCAN_TO_CHANNEL(2, 0x007),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(2, 3, 0x00C),
	MAX1363_MODE_SCAN_TO_CHANNEL(3, 0x00F),
	MAX1363_MODE_SCAN_TO_CHANNEL(4, 0x01F),
	MAX1363_MODE_SCAN_TO_CHANNEL(5, 0x03F),
	MAX1363_MODE_SCAN_TO_CHANNEL(6, 0x07F),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 7, 0x0C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(7, 0x0FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 8, 0x1C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(8, 0x1FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 9, 0x3C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(9, 0x3FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 10, 0x7C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(10, 0x7FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 11, 0xFC0),
	MAX1363_MODE_SCAN_TO_CHANNEL(11, 0xFFF),

	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(2, 2, 0x003000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(4, 3, 0x007000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(6, 4, 0x00F000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(8, 2, 0x018000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(8, 5, 0x01F000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(10, 3, 0x038000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(10, 6, 0x3F000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(3, 2, 0x0C0000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(5, 3, 0x1C0000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(7, 4, 0x3C0000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(9, 2, 0x600000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(9, 5, 0x7C0000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(11, 3, 0xE00000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(11, 6, 0xFC0000),
};

static int max1363_write_basic_config(struct i2c_client *client,
				      unsigned char d1,
				      unsigned char d2)
{
	u8 tx_buf[2] = {d1, d2};

	return i2c_master_send(client, tx_buf, 2);
}

static int max1363_set_scan_mode(struct max1363_state *st)
{
	st->configbyte &= ~(MAX1363_CHANNEL_SEL_MASK
			    | MAX1363_SCAN_MASK
			    | MAX1363_SE_DE_MASK);
	st->configbyte |= st->current_mode->conf;

	return max1363_write_basic_config(st->client,
					  st->setupbyte,
					  st->configbyte);
}

static int max1363_read_single_chan(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    int *val,
				    long m)
{
	int ret = 0;
	s32 data;
	char rxbuf[2];
	struct max1363_state *st = iio_priv(indio_dev);
	struct i2c_client *client = st->client;

	mutex_lock(&indio_dev->mlock);

	/* Check to see if current scan mode is correct */
	if (st->current_mode != &max1363_mode_table[chan->address]) {
		/* Update scan mode if needed */
		st->current_mode = &max1363_mode_table[chan->address];
		ret = max1363_set_scan_mode(st);
		if (ret < 0)
			goto error_ret;
	}
	if (st->chip_info->bits != 8) {
		/* Get reading */
		data = i2c_master_recv(client, rxbuf, 2);
		if (data < 0) {
			ret = data;
			goto error_ret;
		}
		data = (s32)(rxbuf[1]) | ((s32)(rxbuf[0] & 0x0F)) << 8;
	} else {
		/* Get reading */
		data = i2c_master_recv(client, rxbuf, 1);
		if (data < 0) {
			ret = data;
			goto error_ret;
		}
		data = rxbuf[0];
	}

	*val = data;
error_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;

}

static int max1363_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long m)
{
	struct max1363_state *st = iio_priv(indio_dev);
	int ret;
	switch (m) {
	case 0:
		ret = max1363_read_single_chan(indio_dev, chan, val, m);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if ((1 << (st->chip_info->bits + 1)) >
		    st->chip_info->int_vref_mv) {
			*val = 0;
			*val2 = 500000;
			return IIO_VAL_INT_PLUS_MICRO;
		} else {
			*val = (st->chip_info->int_vref_mv)
				>> st->chip_info->bits;
			return IIO_VAL_INT;
		}
	default:
		return -EINVAL;
	}
	return 0;
}

/* Applies to max1363 */
static const enum max1363_modes max1363_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
};

/*
 * Bits and scan inded not currently used but left here to
 * reduce churn during merge.
 */
#define MAX1363_CHAN_U(num, addr, scan_index, bits)			\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = num,						\
		.address = addr,					\
		.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,		\
		.datasheet_name = "AIN"#num				\
	}								\

/* bipolar channel */
#define MAX1363_CHAN_B(num, num2, addr, scan_index, bits)		\
	{								\
		.type = IIO_VOLTAGE,					\
		.differential = 1,					\
		.indexed = 1,						\
		.channel = num,						\
		.channel2 = num2,					\
		.address = addr,					\
		.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,		\
		.datasheet_name = "AIN"#num"-AIN"#num2			\
	}


#define MAX1363_CHAN_UNIPOLAR(index)
#define MAX1363_4X_CHANS(bits) {		\
	MAX1363_CHAN_U(0, _s0, 0, bits),	\
	MAX1363_CHAN_U(1, _s1, 1, bits),	\
	MAX1363_CHAN_U(2, _s2, 2, bits),	\
	MAX1363_CHAN_U(3, _s3, 3, bits),	\
	MAX1363_CHAN_B(0, 1, d0m1, 4, bits),	\
	MAX1363_CHAN_B(2, 3, d2m3, 5, bits),	\
	MAX1363_CHAN_B(1, 0, d1m0, 6, bits),	\
	MAX1363_CHAN_B(3, 2, d3m2, 7, bits),	\
	}

static struct iio_chan_spec max1036_channels[] = MAX1363_4X_CHANS(8);
static struct iio_chan_spec max1136_channels[] = MAX1363_4X_CHANS(10);
static struct iio_chan_spec max1236_channels[] = MAX1363_4X_CHANS(12);

/* Appies to max1236, max1237 */
static const enum max1363_modes max1236_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
	s2to3,
};

/* Applies to max1238, max1239 */
static const enum max1363_modes max1238_mode_list[] = {
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _s8, _s9, _s10, _s11,
	s0to1, s0to2, s0to3, s0to4, s0to5, s0to6,
	s0to7, s0to8, s0to9, s0to10, s0to11,
	d0m1, d2m3, d4m5, d6m7, d8m9, d10m11,
	d1m0, d3m2, d5m4, d7m6, d9m8, d11m10,
	d0m1to2m3, d0m1to4m5, d0m1to6m7, d0m1to8m9, d0m1to10m11,
	d1m0to3m2, d1m0to5m4, d1m0to7m6, d1m0to9m8, d1m0to11m10,
	s6to7, s6to8, s6to9, s6to10, s6to11,
	d6m7to8m9, d6m7to10m11, d7m6to9m8, d7m6to11m10,
};

#define MAX1363_12X_CHANS(bits) {			\
	MAX1363_CHAN_U(0, _s0, 0, bits),		\
	MAX1363_CHAN_U(1, _s1, 1, bits),		\
	MAX1363_CHAN_U(2, _s2, 2, bits),		\
	MAX1363_CHAN_U(3, _s3, 3, bits),		\
	MAX1363_CHAN_U(4, _s4, 4, bits),		\
	MAX1363_CHAN_U(5, _s5, 5, bits),		\
	MAX1363_CHAN_U(6, _s6, 6, bits),		\
	MAX1363_CHAN_U(7, _s7, 7, bits),		\
	MAX1363_CHAN_U(8, _s8, 8, bits),		\
	MAX1363_CHAN_U(9, _s9, 9, bits),		\
	MAX1363_CHAN_U(10, _s10, 10, bits),		\
	MAX1363_CHAN_U(11, _s11, 11, bits),		\
	MAX1363_CHAN_B(0, 1, d0m1, 12, bits),		\
	MAX1363_CHAN_B(2, 3, d2m3, 13, bits),		\
	MAX1363_CHAN_B(4, 5, d4m5, 14, bits),		\
	MAX1363_CHAN_B(6, 7, d6m7, 15, bits),		\
	MAX1363_CHAN_B(8, 9, d8m9, 16, bits),		\
	MAX1363_CHAN_B(10, 11, d10m11, 17, bits),	\
	MAX1363_CHAN_B(1, 0, d1m0, 18, bits),		\
	MAX1363_CHAN_B(3, 2, d3m2, 19, bits),		\
	MAX1363_CHAN_B(5, 4, d5m4, 20, bits),		\
	MAX1363_CHAN_B(7, 6, d7m6, 21, bits),		\
	MAX1363_CHAN_B(9, 8, d9m8, 22, bits),		\
	MAX1363_CHAN_B(11, 10, d11m10, 23, bits),	\
	}

static struct iio_chan_spec max1038_channels[] = MAX1363_12X_CHANS(8);
static struct iio_chan_spec max1138_channels[] = MAX1363_12X_CHANS(10);
static struct iio_chan_spec max1238_channels[] = MAX1363_12X_CHANS(12);

static const enum max1363_modes max11607_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	s2to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
};

static const enum max1363_modes max11608_mode_list[] = {
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7,
	s0to1, s0to2, s0to3, s0to4, s0to5, s0to6, s0to7,
	s6to7,
	d0m1, d2m3, d4m5, d6m7,
	d1m0, d3m2, d5m4, d7m6,
	d0m1to2m3, d0m1to4m5, d0m1to6m7,
	d1m0to3m2, d1m0to5m4, d1m0to7m6,
};

#define MAX1363_8X_CHANS(bits) {		\
	MAX1363_CHAN_U(0, _s0, 0, bits),	\
	MAX1363_CHAN_U(1, _s1, 1, bits),	\
	MAX1363_CHAN_U(2, _s2, 2, bits),	\
	MAX1363_CHAN_U(3, _s3, 3, bits),	\
	MAX1363_CHAN_U(4, _s4, 4, bits),	\
	MAX1363_CHAN_U(5, _s5, 5, bits),	\
	MAX1363_CHAN_U(6, _s6, 6, bits),	\
	MAX1363_CHAN_U(7, _s7, 7, bits),	\
	MAX1363_CHAN_B(0, 1, d0m1, 8, bits),	\
	MAX1363_CHAN_B(2, 3, d2m3, 9, bits),	\
	MAX1363_CHAN_B(4, 5, d4m5, 10, bits),	\
	MAX1363_CHAN_B(6, 7, d6m7, 11, bits),	\
	MAX1363_CHAN_B(1, 0, d1m0, 12, bits),	\
	MAX1363_CHAN_B(3, 2, d3m2, 13, bits),	\
	MAX1363_CHAN_B(5, 4, d5m4, 14, bits),	\
	MAX1363_CHAN_B(7, 6, d7m6, 15, bits),	\
}
static struct iio_chan_spec max11602_channels[] = MAX1363_8X_CHANS(8);
static struct iio_chan_spec max11608_channels[] = MAX1363_8X_CHANS(10);
static struct iio_chan_spec max11614_channels[] = MAX1363_8X_CHANS(12);

static const enum max1363_modes max11644_mode_list[] = {
	_s0, _s1, s0to1, d0m1, d1m0,
};

#define MAX1363_2X_CHANS(bits) {			\
	MAX1363_CHAN_U(0, _s0, 0, bits),		\
	MAX1363_CHAN_U(1, _s1, 1, bits),		\
	MAX1363_CHAN_B(0, 1, d0m1, 2, bits),		\
	MAX1363_CHAN_B(1, 0, d1m0, 3, bits),		\
	}

static struct iio_chan_spec max11646_channels[] = MAX1363_2X_CHANS(10);
static struct iio_chan_spec max11644_channels[] = MAX1363_2X_CHANS(12);

enum { max1361,
       max1362,
       max1363,
       max1364,
       max1036,
       max1037,
       max1038,
       max1039,
       max1136,
       max1137,
       max1138,
       max1139,
       max1236,
       max1237,
       max1238,
       max1239,
       max11600,
       max11601,
       max11602,
       max11603,
       max11604,
       max11605,
       max11606,
       max11607,
       max11608,
       max11609,
       max11610,
       max11611,
       max11612,
       max11613,
       max11614,
       max11615,
       max11616,
       max11617,
       max11644,
       max11645,
       max11646,
       max11647
};

static const struct iio_info max1238_info = {
	.read_raw = &max1363_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct iio_info max1363_info = {
	.read_raw = &max1363_read_raw,
	.driver_module = THIS_MODULE,
};

/* max1363 and max1368 tested - rest from data sheet */
static const struct max1363_chip_info max1363_chip_info_tbl[] = {
	[max1361] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
		.info = &max1363_info,
	},
	[max1362] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
		.info = &max1363_info,
	},
	[max1363] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
		.info = &max1363_info,
	},
	[max1364] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
		.info = &max1363_info,
	},
	[max1036] = {
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1036_channels,
		.num_channels = ARRAY_SIZE(max1036_channels),
	},
	[max1037] = {
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1036_channels,
		.num_channels = ARRAY_SIZE(max1036_channels),
	},
	[max1038] = {
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1038_channels,
		.num_channels = ARRAY_SIZE(max1038_channels),
	},
	[max1039] = {
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1038_channels,
		.num_channels = ARRAY_SIZE(max1038_channels),
	},
	[max1136] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
	},
	[max1137] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
	},
	[max1138] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1138_channels,
		.num_channels = ARRAY_SIZE(max1138_channels),
	},
	[max1139] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1138_channels,
		.num_channels = ARRAY_SIZE(max1138_channels),
	},
	[max1236] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
	},
	[max1237] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
	},
	[max1238] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max1239] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11600] = {
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1036_channels,
		.num_channels = ARRAY_SIZE(max1036_channels),
	},
	[max11601] = {
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1036_channels,
		.num_channels = ARRAY_SIZE(max1036_channels),
	},
	[max11602] = {
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11602_channels,
		.num_channels = ARRAY_SIZE(max11602_channels),
	},
	[max11603] = {
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11602_channels,
		.num_channels = ARRAY_SIZE(max11602_channels),
	},
	[max11604] = {
		.bits = 8,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11605] = {
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11606] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
	},
	[max11607] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1136_channels,
		.num_channels = ARRAY_SIZE(max1136_channels),
	},
	[max11608] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11608_channels,
		.num_channels = ARRAY_SIZE(max11608_channels),
	},
	[max11609] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11608_channels,
		.num_channels = ARRAY_SIZE(max11608_channels),
	},
	[max11610] = {
		.bits = 10,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11611] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11612] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
	},
	[max11613] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.info = &max1238_info,
		.channels = max1236_channels,
		.num_channels = ARRAY_SIZE(max1236_channels),
	},
	[max11614] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11614_channels,
		.num_channels = ARRAY_SIZE(max11614_channels),
	},
	[max11615] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.info = &max1238_info,
		.channels = max11614_channels,
		.num_channels = ARRAY_SIZE(max11614_channels),
	},
	[max11616] = {
		.bits = 12,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11617] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.info = &max1238_info,
		.channels = max1238_channels,
		.num_channels = ARRAY_SIZE(max1238_channels),
	},
	[max11644] = {
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max11644_mode_list,
		.num_modes = ARRAY_SIZE(max11644_mode_list),
		.default_mode = s0to1,
		.info = &max1238_info,
		.channels = max11644_channels,
		.num_channels = ARRAY_SIZE(max11644_channels),
	},
	[max11645] = {
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max11644_mode_list,
		.num_modes = ARRAY_SIZE(max11644_mode_list),
		.default_mode = s0to1,
		.info = &max1238_info,
		.channels = max11644_channels,
		.num_channels = ARRAY_SIZE(max11644_channels),
	},
	[max11646] = {
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max11644_mode_list,
		.num_modes = ARRAY_SIZE(max11644_mode_list),
		.default_mode = s0to1,
		.info = &max1238_info,
		.channels = max11646_channels,
		.num_channels = ARRAY_SIZE(max11646_channels),
	},
	[max11647] = {
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max11644_mode_list,
		.num_modes = ARRAY_SIZE(max11644_mode_list),
		.default_mode = s0to1,
		.info = &max1238_info,
		.channels = max11646_channels,
		.num_channels = ARRAY_SIZE(max11646_channels),
	},
};

static int max1363_initial_setup(struct max1363_state *st)
{
	st->setupbyte = MAX1363_SETUP_AIN3_IS_AIN3_REF_IS_VDD
		| MAX1363_SETUP_POWER_UP_INT_REF
		| MAX1363_SETUP_INT_CLOCK
		| MAX1363_SETUP_UNIPOLAR
		| MAX1363_SETUP_NORESET;

	/* Set scan mode writes the config anyway so wait until then*/
	st->setupbyte = MAX1363_SETUP_BYTE(st->setupbyte);
	st->current_mode = &max1363_mode_table[st->chip_info->default_mode];
	st->configbyte = MAX1363_CONFIG_BYTE(st->configbyte);

	return max1363_set_scan_mode(st);
}

static int __devinit max1363_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret, i;
	struct max1363_state *st;
	struct iio_dev *indio_dev;

	indio_dev = iio_device_allocate(sizeof(struct max1363_state));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	st->reg = regulator_get(&client->dev, "vcc");
	if (IS_ERR(st->reg)) {
		ret = PTR_ERR(st->reg);
		goto error_free_device;
	}

	ret = regulator_enable(st->reg);
	if (ret < 0)
		goto error_put_reg;

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	st->chip_info = &max1363_chip_info_tbl[id->driver_data];
	st->client = client;

	indio_dev->available_scan_masks
		= kzalloc(BITS_TO_LONGS(MAX1363_MAX_CHANNELS)*sizeof(long)*
			  (st->chip_info->num_modes + 1), GFP_KERNEL);
	if (!indio_dev->available_scan_masks) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	for (i = 0; i < st->chip_info->num_modes; i++)
		bitmap_copy(indio_dev->available_scan_masks +
			    BITS_TO_LONGS(MAX1363_MAX_CHANNELS)*i,
			    max1363_mode_table[st->chip_info->mode_list[i]]
			    .modemask, MAX1363_MAX_CHANNELS);
	/* Estabilish that the iio_dev is a child of the i2c device */
	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;

	indio_dev->info = st->chip_info->info;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	ret = max1363_initial_setup(st);
	if (ret < 0)
		goto error_free_available_scan_masks;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_available_scan_masks;

	return 0;

error_free_available_scan_masks:
	kfree(indio_dev->available_scan_masks);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
error_free_device:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int max1363_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct max1363_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	
	kfree(indio_dev->available_scan_masks);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}

	iio_device_free(indio_dev);

	return 0;
}

static const struct i2c_device_id max1363_id[] = {
	{ "max1361", max1361 },
	{ "max1362", max1362 },
	{ "max1363", max1363 },
	{ "max1364", max1364 },
	{ "max1036", max1036 },
	{ "max1037", max1037 },
	{ "max1038", max1038 },
	{ "max1039", max1039 },
	{ "max1136", max1136 },
	{ "max1137", max1137 },
	{ "max1138", max1138 },
	{ "max1139", max1139 },
	{ "max1236", max1236 },
	{ "max1237", max1237 },
	{ "max1238", max1238 },
	{ "max1239", max1239 },
	{ "max11600", max11600 },
	{ "max11601", max11601 },
	{ "max11602", max11602 },
	{ "max11603", max11603 },
	{ "max11604", max11604 },
	{ "max11605", max11605 },
	{ "max11606", max11606 },
	{ "max11607", max11607 },
	{ "max11608", max11608 },
	{ "max11609", max11609 },
	{ "max11610", max11610 },
	{ "max11611", max11611 },
	{ "max11612", max11612 },
	{ "max11613", max11613 },
	{ "max11614", max11614 },
	{ "max11615", max11615 },
	{ "max11616", max11616 },
	{ "max11617", max11617 },
	{}
};

MODULE_DEVICE_TABLE(i2c, max1363_id);

static struct i2c_driver max1363_driver = {
	.driver = {
		.name = "max1363",
	},
	.probe = max1363_probe,
	.remove = max1363_remove,
	.id_table = max1363_id,
};

static __init int max1363_init(void)
{
	return i2c_add_driver(&max1363_driver);
}
module_init(max1363_init);

static __exit void max1363_exit(void)
{
	i2c_del_driver(&max1363_driver);
}
module_exit(max1363_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Maxim 1363 ADC");
MODULE_LICENSE("GPL v2");
