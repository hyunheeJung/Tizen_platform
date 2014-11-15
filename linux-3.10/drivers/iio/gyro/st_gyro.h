/*
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 * Licensed under the GPL-2.
 */

#ifndef ST_GYRO_H
#define ST_GYRO_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

#define L3G4200D_GYRO_DEV_NAME		"l3g4200d"
#define LSM330D_GYRO_DEV_NAME		"lsm330d-gyro"
#define LSM330DL_GYRO_DEV_NAME		"lsm330dl-gyro"
#define LSM330DLC_GYRO_DEV_NAME		"lsm330dlc-gyro"
#define L3GD20_GYRO_DEV_NAME		"l3gd20"
#define L3GD20H_GYRO_DEV_NAME		"l3gd20h"
#define L3G4IS_GYRO_DEV_NAME		"l3g4is-ui"
#define LSM330_GYRO_DEV_NAME		"lsm330-gyro"

int st_gyro_common_probe(struct iio_dev *indio_dev);
void st_gyro_common_remove(struct iio_dev *indio_dev);

#ifdef CONFIG_IIO_BUFFER
int st_gyro_allocate_ring(struct iio_dev *indio_dev);
void st_gyro_deallocate_ring(struct iio_dev *indio_dev);
int st_gyro_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_GYRO_TRIGGER_SET_STATE (&st_gyro_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_gyro_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void st_gyro_deallocate_ring(struct iio_dev *indio_dev)
{
}
#define ST_GYRO_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* ST_GYRO_H */
