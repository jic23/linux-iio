/*
 * iio/adc/ad799x.c
 * Copyright (C) 2010-1011 Michael Hennerich, Analog Devices Inc.
 *
 * based on iio/adc/max1363
 * Copyright (C) 2008-2010 Jonathan Cameron
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
 *
 * ad799x.c
 *
 * Support for ad7991, ad7995, ad7999, ad7992, ad7993, ad7994, ad7997,
 * ad7998 and similar chips.
 *
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/ad799x.h>

#define AD799X_CHANNEL_SHIFT			4
/*
 * AD7991, AD7995 and AD7999 defines
 */

#define AD7991_REF_SEL				0x08
#define AD7991_FLTR				0x04
#define AD7991_BIT_TRIAL_DELAY			0x02
#define AD7991_SAMPLE_DELAY			0x01

/*
 * AD7992, AD7993, AD7994, AD7997 and AD7998 defines
 */

#define AD7998_FLTR				0x08
#define AD7998_ALERT_EN				0x04
#define AD7998_BUSY_ALERT			0x02
#define AD7998_BUSY_ALERT_POL			0x01

#define AD7998_CONV_RES_REG			0x0
#define AD7998_ALERT_STAT_REG			0x1
#define AD7998_CONF_REG				0x2
#define AD7998_CYCLE_TMR_REG			0x3
#define AD7998_DATALOW_CH1_REG			0x4
#define AD7998_DATAHIGH_CH1_REG			0x5
#define AD7998_HYST_CH1_REG			0x6
#define AD7998_DATALOW_CH2_REG			0x7
#define AD7998_DATAHIGH_CH2_REG			0x8
#define AD7998_HYST_CH2_REG			0x9
#define AD7998_DATALOW_CH3_REG			0xA
#define AD7998_DATAHIGH_CH3_REG			0xB
#define AD7998_HYST_CH3_REG			0xC
#define AD7998_DATALOW_CH4_REG			0xD
#define AD7998_DATAHIGH_CH4_REG			0xE
#define AD7998_HYST_CH4_REG			0xF

#define AD7998_CYC_MASK				0x7
#define AD7998_CYC_DIS				0x0
#define AD7998_CYC_TCONF_32			0x1
#define AD7998_CYC_TCONF_64			0x2
#define AD7998_CYC_TCONF_128			0x3
#define AD7998_CYC_TCONF_256			0x4
#define AD7998_CYC_TCONF_512			0x5
#define AD7998_CYC_TCONF_1024			0x6
#define AD7998_CYC_TCONF_2048			0x7

#define AD7998_ALERT_STAT_CLEAR			0xFF

/*
 * AD7997 and AD7997 defines
 */

#define AD7997_8_READ_SINGLE			0x80
#define AD7997_8_READ_SEQUENCE			0x70
/* TODO: move this into a common header */
#define RES_MASK(bits)	((1 << (bits)) - 1)

enum {
	ad7991,
	ad7995,
	ad7999,
	ad7992,
	ad7993,
	ad7994,
	ad7997,
	ad7998
};

struct ad799x_state;

/**
 * struct ad799x_chip_info - chip specifc information
 * @channel:		channel specification
 * @num_channels:	number of channels
 * @int_vref_mv:	the internal reference voltage
 */
struct ad799x_chip_info {
	struct iio_chan_spec		channel[9];
	int				num_channels;
	u16				int_vref_mv;
};

struct ad799x_state {
	struct i2c_client		*client;
	const struct ad799x_chip_info	*chip_info;
	struct regulator		*reg;
	u16				int_vref_mv;
	unsigned			id;
	char				*name;
	u16				config;
};

/*
 * ad799x register access by I2C
 */
static int ad799x_i2c_read16(struct ad799x_state *st, u8 reg, u16 *data)
{
	struct i2c_client *client = st->client;
	int ret = 0;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = swab16((u16)ret);

	return 0;
}

static int ad799x_scan_direct(struct ad799x_state *st, unsigned ch)
{
	u16 rxbuf;
	u8 cmd;
	int ret;

	switch (st->id) {
	case ad7991:
	case ad7995:
	case ad7999:
		cmd = st->config | ((1 << ch) << AD799X_CHANNEL_SHIFT);
		break;
	case ad7992:
	case ad7993:
	case ad7994:
		cmd = (1 << ch) << AD799X_CHANNEL_SHIFT;
		break;
	case ad7997:
	case ad7998:
		cmd = (ch << AD799X_CHANNEL_SHIFT) | AD7997_8_READ_SINGLE;
		break;
	default:
		return -EINVAL;
	}

	ret = ad799x_i2c_read16(st, cmd, &rxbuf);
	if (ret < 0)
		return ret;

	return rxbuf;
}

static int ad799x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad799x_state *st = iio_priv(indio_dev);
	unsigned int scale_uv;

	switch (m) {
	case 0:
		mutex_lock(&indio_dev->mlock);
		ret = ad799x_scan_direct(st, chan->channel);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;
		*val = (ret >> chan->scan_type.shift) &
			RES_MASK(chan->scan_type.realbits);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->int_vref_mv * 1000) >> chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static const struct iio_info ad799X_info = {
	.read_raw = &ad799x_read_raw,
	.driver_module = THIS_MODULE,
};


static const struct ad799x_chip_info ad799x_chip_info_tbl[] = {
	[ad7991] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type =  {
					.realbits = 12,
					.shift = 0,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
		},
		.num_channels = 5,
		.int_vref_mv = 4096,
	},
	[ad7995] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
	},
	[ad7999] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 8,
					.shift = 4,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 8,
					.shift = 4,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 8,
					.shift = 4,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 8,
					.shift = 4,
				},
			},
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
	},
	[ad7992] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
		},
		.num_channels = 3,
		.int_vref_mv = 4096,
	},
	[ad7993] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
	},
	[ad7994] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
		},
		.num_channels = 5,
		.int_vref_mv = 4096,
	},
	[ad7997] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[4] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 4,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[5] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 5,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[6] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 6,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
			[7] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 7,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 10,
					.shift = 2,
				},
			},
		},
		.num_channels = 9,
		.int_vref_mv = 1024,
	},
	[ad7998] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[4] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 4,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[5] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 5,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[6] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 6,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
			[7] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 7,
				.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT,
				.scan_type = {
					.realbits = 12,
					.shift = 0,
				},
			},
		},
		.num_channels = 9,
		.int_vref_mv = 4096,
	},
};

static int __devinit ad799x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret;
	struct ad799x_platform_data *pdata = client->dev.platform_data;
	struct ad799x_state *st;
	struct iio_dev *indio_dev = iio_device_allocate(sizeof(*st));

	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	st->id = id->driver_data;
	st->chip_info = &ad799x_chip_info_tbl[st->id];

	/* TODO: Add pdata options for filtering and bit delay */

	if (pdata)
		st->int_vref_mv = pdata->vref_mv;
	else
		st->int_vref_mv = st->chip_info->int_vref_mv;

	st->reg = regulator_get(&client->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;
	}
	st->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ad799X_info;

	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = st->chip_info->num_channels;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	iio_device_free(indio_dev);

	return ret;
}

static __devexit int ad799x_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ad799x_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

	return 0;
}

static const struct i2c_device_id ad799x_id[] = {
	{ "ad7991", ad7991 },
	{ "ad7995", ad7995 },
	{ "ad7999", ad7999 },
	{ "ad7992", ad7992 },
	{ "ad7993", ad7993 },
	{ "ad7994", ad7994 },
	{ "ad7997", ad7997 },
	{ "ad7998", ad7998 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad799x_id);

static struct i2c_driver ad799x_driver = {
	.driver = {
		.name = "ad799x",
	},
	.probe = ad799x_probe,
	.remove = __devexit_p(ad799x_remove),
	.id_table = ad799x_id,
};

static __init int ad799x_init(void)
{
	return i2c_add_driver(&ad799x_driver);
}

static __exit void ad799x_exit(void)
{
	i2c_del_driver(&ad799x_driver);
}

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD799x ADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:ad799x");

module_init(ad799x_init);
module_exit(ad799x_exit);
