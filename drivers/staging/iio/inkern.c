/* The industrial I/O core in kernel channel mapping
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include "inkern.h"
#include <linux/err.h>

LIST_HEAD(iio_map_list);
EXPORT_SYMBOL_GPL(iio_map_list);
void iio_map_array_register(struct iio_map *map, int nummaps)
{
	int i;
	for (i = 0; i < nummaps; i++)
		list_add(&map[i].l, &iio_map_list);
}
EXPORT_SYMBOL(iio_map_array_register);
