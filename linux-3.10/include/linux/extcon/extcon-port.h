/*
 * include/linux/extcon/extcon-port.h
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chanwoo Choi <cw00.choi@samsung.com>
 *
 * based on include/linux/jack.h
 * Copyright (C) 2009 Samsung Electronics
 * Minkyu Kang <mk7.samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXTCON_PORT_H_
#define __EXTCON_PORT_H_

struct jack_platform_data {
	int usb_online;
	int charger_online;
	int hdmi_online;
	int earjack_online;
	int earkey_online;
	int ums_online;
	int cdrom_online;
	int jig_online;
	int host_online;
	int cradle_online;

	char *extcon_name_muic;
	char *extcon_name_jack;
	char *extcon_name_hdmi;
};

#ifdef CONFIG_EXTCON_PORT
extern int jack_get_data(const char *name);
extern void jack_event_handler(char *name, int value);
#else
static int jack_get_data(const char *name)
{
	return 0;
}
static void jack_event_handler(char *name, int value) {}
#endif
#endif
