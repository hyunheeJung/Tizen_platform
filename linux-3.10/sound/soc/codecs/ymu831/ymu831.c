/*
 * YMU831 ASoC codec driver
 *
 * Copyright (c) 2012-2013 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.	In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgment in the product documentation would be
 *	appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include <linux/types.h>
#include <linux/workqueue.h>
#include <sound/hwdep.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/jack.h>

#include "ymu831_cfg.h"
#include "ymu831_path_cfg.h"
#include "ymu831.h"
#include "ymu831_priv.h"
#include "mcdefs.h"
#include "mcresctrl.h"
#include <linux/spi/spi.h>

#define MC_ASOC_DRIVER_VERSION	"2.0.1"

#define MC_ASOC_IMPCLASS_THRESHOLD	3

#define MC_ASOC_RATE	(SNDRV_PCM_RATE_8000_192000)
#define MC_ASOC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

#define MC_ASOC_HWDEP_ID	"ymu831"

#define get_port_id(id)	(id-1)
#define	PORT_MUSIC	(0)
#define	PORT_EXT	(1)
#define	PORT_VOICE	(2)
#define	PORT_HIFI	(3)

#define	DIO_MUSIC	(0)
#define	DIO_VOICE	(1)
#define	DIO_EXT		(2)
#define	LOUT1		(3)
#define	LOUT2		(4)
#define	LIN1		(3)
#define	LIN1_LOUT1	(3)
#define	LIN1_LOUT2	(5)

#define	DSP_PRM_OUTPUT	(0)
#define	DSP_PRM_INPUT	(1)
#define	DSP_PRM_VC_1MIC	(2)
#define	DSP_PRM_VC_2MIC	(3)
#define	DSP_PRM_BASE	(0)
#define	DSP_PRM_USER	(1)

#define	MICOPEN_4POLE

struct mc_asoc_info_store {
	UINT32	get;
	UINT32	set;
	size_t	offset;
	UINT32	flags;
};
static const struct mc_asoc_info_store	info_store_tbl[]	= {
	{
		MCDRV_GET_CLOCKSW,
		MCDRV_SET_CLOCKSW,
		offsetof(struct mc_asoc_data, clocksw_store),
		0
	},
	{
		MCDRV_GET_DIGITALIO,
		MCDRV_SET_DIGITALIO,
		offsetof(struct mc_asoc_data, dio_store),
		0xfff
	},
	{
		MCDRV_GET_DIGITALIO_PATH,
		MCDRV_SET_DIGITALIO_PATH,
		offsetof(struct mc_asoc_data, diopath_store),
		0x7ff
	},
	{
		MCDRV_GET_PATH,	MCDRV_SET_PATH,
		offsetof(struct mc_asoc_data, path_store),
		0
	},
	{
		MCDRV_GET_VOLUME,
		MCDRV_SET_VOLUME,
		offsetof(struct mc_asoc_data, vol_store),
		0
	},
	{
		MCDRV_GET_SWAP,
		MCDRV_SET_SWAP,
		offsetof(struct mc_asoc_data, swap_store),
		0x7fff
	},
};
#define MC_ASOC_N_INFO_STORE \
	(sizeof(info_store_tbl) / sizeof(struct mc_asoc_info_store))

static const char	* const firmware_name[]	= {
	"",	/*	dummy	*/
	"aec_config.dat",
	"aec_control_path_off.dat",
	"aec_control_path_resume.dat",
	"aec_hp_dac0.dat",
	"aec_line1_dac0.dat",
	"aec_line2_dac1.dat",
	"aec_rc_dac0.dat",
	"aec_sp_dac1.dat",
	"aec_adc.dat",
	"aec_hf50_vbox_nb.dat",
	"aec_hf50_vbox_wb.dat",
	"aec_hf50_chsel.dat",
	"aec_hf50_nb_type0_rc_2mic_2.dat",
	"aec_hf50_nb_type0_sp_2mic_2.dat",
	"aec_hf50_nb_type1_rc_2mic_2.dat",
	"aec_hf50_nb_type1_sp_2mic_2.dat",
	"aec_hf50_wb_type0_rc_2mic_2.dat",
	"aec_hf50_wb_type0_sp_2mic_2.dat",
	"aec_hf50_wb_type1_rc_2mic_2.dat",
	"aec_hf50_wb_type1_sp_2mic_2.dat",
	"aec_hf50_vt_nb_type0_rc_2mic_2.dat",
	"aec_hf50_vt_nb_type0_sp_2mic_2.dat",
	"aec_hf50_vt_nb_type1_rc_2mic_2.dat",
	"aec_hf50_vt_nb_type1_sp_2mic_2.dat",
	"aec_hf50_vt_wb_type0_rc_2mic_2.dat",
	"aec_hf50_vt_wb_type0_sp_2mic_2.dat",
	"aec_hf50_vt_wb_type1_rc_2mic_2.dat",
	"aec_hf50_vt_wb_type1_sp_2mic_2.dat",
	"aec_hf50_wb_svoice_2.dat",
	"aec_hf50_wb_svoicecar_2.dat",
};

/* volmap for Digital Volumes */
static const SINT16	volmap_digital[]	= {
	0xa000, 0xa100, 0xa200, 0xa300, 0xa400, 0xa500, 0xa600, 0xa700,
	0xa800, 0xa900, 0xaa00, 0xab00, 0xac00, 0xad00, 0xae00, 0xaf00,
	0xb000, 0xb100, 0xb200, 0xb300, 0xb400, 0xb500, 0xb600, 0xb700,
	0xb800, 0xb900, 0xba00, 0xbb00, 0xbc00, 0xbd00, 0xbe00, 0xbf00,
	0xc000, 0xc100, 0xc200, 0xc300, 0xc400, 0xc500, 0xc600, 0xc700,
	0xc800, 0xc900, 0xca00, 0xcb00, 0xcc00, 0xcd00, 0xce00, 0xcf00,
	0xd000, 0xd100, 0xd200, 0xd300, 0xd400, 0xd500, 0xd600, 0xd700,
	0xd800, 0xd900, 0xda00, 0xdb00, 0xdc00, 0xdd00, 0xde00, 0xdf00,
	0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700,
	0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00,
	0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0xf600, 0xf700,
	0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00, 0xff00,
	0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700,
	0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00,
	0x1000, 0x1100, 0x1200
};

/* volmap for LINE/MIC Input Volumes */
static const SINT16	volmap_ain[]	= {
	0xa000, 0xa000, 0xa000, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600,
	0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00,
	0xef00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0xf600,
	0xf700, 0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00,
	0xff00, 0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600,
	0x0700, 0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00,
	0x0f00, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1580,
	0x1600, 0x1680, 0x1700, 0x1780, 0x1800, 0x1a00, 0x1c00, 0x1e00
};

/* volmap for Analog Output Volumes */
static const SINT16	volmap_aout[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000
};

/* volmap for SP Volumes */
static const SINT16	volmap_sp[5][128]	= {
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
		0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
		0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
		0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
		0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
		0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
		0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
		0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdd00,
		0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500,
		0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00,
		0xee00, 0xef00, 0xf000, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300,
		0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700,
		0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00,
		0xfb40, 0xfb80, 0xfbc0, 0xfc00, 0xfc40, 0xfc80, 0xfcc0, 0xfd00,
		0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
		0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
		0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
		0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xde00,
		0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600,
		0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00,
		0xef00, 0xf000, 0xf100, 0xf200, 0xf280, 0xf300, 0xf380, 0xf400,
		0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800,
		0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00, 0xfb80, 0xfc00,
		0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdf00,
		0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700,
		0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00,
		0xf000, 0xf100, 0xf200, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500,
		0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900,
		0xf980, 0xfa00, 0xfa80, 0xfb00, 0xfb80, 0xfc00, 0xfc80, 0xfd00,
		0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
		0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
		0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
		0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xe000,
		0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800,
		0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00, 0xf000,
		0xf100, 0xf200, 0xf300, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
		0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
		0xfa80, 0xfb00, 0xfb80, 0xfc00, 0xfc80, 0xfd00, 0xfd80, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	}
};

/* volmap for LineOut Volumes */
static const SINT16	volmap_lineout[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
	0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
};

/* volmap for LineOut2 Volumes */
static const SINT16	volmap_lineout2[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xda00,
	0xdc00, 0xdc00, 0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200,
	0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00,
	0xeb00, 0xec00, 0xed00, 0xee00, 0xee00, 0xef00, 0xef00, 0xf000,
	0xf080, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300, 0xf380, 0xf400,
	0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800,
	0xf800, 0xf880, 0xf880, 0xf900, 0xf900, 0xf980, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
};

/* volmap for HP Output Volumes */
static const SINT16	volmap_hp_es1[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdd00,
	0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500,
	0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00,
	0xee00, 0xef00, 0xf000, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300,
	0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700,
	0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00,
	0xfb40, 0xfb80, 0xfbc0, 0xfc00, 0xfc40, 0xfc80, 0xfcc0, 0xfd00,
	0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
	0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
	0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
	0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
};
static const SINT16	volmap_hp[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
	0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
	0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400
};

/* volmap for Master Volumes */
static const SINT16	volmap_master[]	= {
	0xb500, 0xb600, 0xb700, 0xb800, 0xb900, 0xba00, 0xbb00, 0xbc00,
	0xbd00, 0xbe00, 0xbf00, 0xc000, 0xc100, 0xc200, 0xc300, 0xc400,
	0xc500, 0xc600, 0xc700, 0xc800, 0xc900, 0xca00, 0xcb00, 0xcc00,
	0xcd00, 0xce00, 0xcf00, 0xd000, 0xd100, 0xd200, 0xd300, 0xd400,
	0xd500, 0xd600, 0xd700, 0xd800, 0xd900, 0xda00, 0xdb00, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xdf00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400,
	0xf500, 0xf600, 0xf700, 0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00,
	0xfd00, 0xfe00, 0xff00, 0x0000
};

/* volmap for Voice Volumes */
static const SINT16	volmap_voice[]	= {
	0xb500, 0xb600, 0xb700, 0xb800, 0xb900, 0xba00, 0xbb00, 0xbc00,
	0xbd00, 0xbe00, 0xbf00, 0xc000, 0xc100, 0xc200, 0xc300, 0xc400,
	0xc500, 0xc600, 0xc700, 0xc800, 0xc900, 0xca00, 0xcb00, 0xcc00,
	0xcd00, 0xce00, 0xcf00, 0xd000, 0xd100, 0xd200, 0xd300, 0xd400,
	0xd500, 0xd600, 0xd700, 0xd800, 0xd900, 0xda00, 0xdb00, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xdf00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400,
	0xf500, 0xf600, 0xf700, 0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00,
	0xfd00, 0xfe00, 0xff00, 0x0000
};

/* volmap for AnalogIn Ana Volumes */
static const SINT16	volmap_aplay_a[]	= {
	0xa000, 0xa000, 0xa000, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600,
	0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00,
	0xef00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0xf600,
	0xf700, 0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00,
	0xff00, 0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600,
	0x0700, 0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00,
	0x0f00, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1580,
	0x1600, 0x1680, 0x1700, 0x1780, 0x1800, 0x1a00, 0x1c00, 0x1e00
};

/* volmap for Adif(ES) Volumes */
static const SINT16	volmap_adif[]	= {
	0xa000, 0xa300, 0xa400, 0xa500, 0xa600, 0xa700, 0xa800, 0xa900,
	0xaa00, 0xab00, 0xac00, 0xad00, 0xae00, 0xaf00, 0xb000, 0xb100,
	0xb200, 0xb300, 0xb400, 0xb500, 0xb600, 0xb700, 0xb800, 0xb900,
	0xba00, 0xbb00, 0xbc00, 0xbd00, 0xbe00, 0xbf00, 0xc000, 0xc100,
	0xc200, 0xc300, 0xc400, 0xc500, 0xc600, 0xc700, 0xc800, 0xc900,
	0xca00, 0xcb00, 0xcc00, 0xcd00, 0xce00, 0xcf00, 0xd000, 0xd100,
	0xd200, 0xd300, 0xd400, 0xd500, 0xd600, 0xd700, 0xd800, 0xd900,
	0xda00, 0xdb00, 0xdc00, 0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100,
	0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800, 0xe900,
	0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00, 0xf000, 0xf100,
	0xf200, 0xf300, 0xf400, 0xf500, 0xf600, 0xf700, 0xf800, 0xf900,
	0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00, 0xff00, 0x0000, 0x0100,
	0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800, 0x0900,
	0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00, 0x1000, 0x1100,
	0x1200, 0x1200, 0x1200
};

struct mc_asoc_vreg_info {
	size_t	offset;
	const SINT16	*volmap;
	UINT8	channels;
};
static struct mc_asoc_vreg_info	vreg_map[MC_ASOC_N_VOL_REG]	= {
	{offsetof(struct MCDRV_VOL_INFO, aswD_MusicIn),
	 volmap_digital,
	  MUSICIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_ExtIn),
	 volmap_digital,
	  EXTIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_VoiceIn),
	 volmap_digital,
	  VOICEIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_RefIn),
	 volmap_digital,
	  REFIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif0In),
	 volmap_digital,
	  ADIF0IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif1In),
	 volmap_digital,
	  ADIF1IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif2In),
	 volmap_digital,
	  ADIF2IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_MusicOut),
	 volmap_digital,
	  MUSICOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_ExtOut),
	 volmap_digital,
	  EXTOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_VoiceOut),
	 volmap_digital,
	  VOICEOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_RefOut),
	 volmap_digital,
	  REFOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Dac0Out),
	 volmap_digital,
	  DAC0OUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Dac1Out),
	 volmap_digital,
	  DAC1OUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_DpathDa),
	 volmap_digital,
	  DPATH_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_DpathAd),
	 volmap_digital,
	  DPATH_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineIn1),
	 volmap_ain,
	  LINEIN1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic1),
	 volmap_ain,
	  MIC1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic2),
	 volmap_ain,
	  MIC2_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic3),
	 volmap_ain,
	  MIC3_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic4),
	 volmap_ain,
	  MIC4_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Hp),
	 volmap_hp,
	  HP_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Sp),
	 volmap_sp[0],
	  SP_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Rc),
	 volmap_aout,
	  RC_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineOut1),
	 volmap_lineout,
	  LINEOUT1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineOut2),
	 volmap_lineout2,
	  LINEOUT2_VOL_CHANNELS},

	{(size_t)-1,	NULL,		0},

	{(size_t)-1,	volmap_master,	MUSICIN_VOL_CHANNELS},
	{(size_t)-1,	volmap_voice,	VOICEIN_VOL_CHANNELS},
	{(size_t)-1,	volmap_aplay_a,	LINEIN1_VOL_CHANNELS},
	{(size_t)-1,	volmap_digital,	ADIF0IN_VOL_CHANNELS},
};

static const DECLARE_TLV_DB_SCALE(mc_asoc_tlv_digital, -9600, 100, 1);
static unsigned int	mc_asoc_tlv_ain[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x02,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x03,	0x36,	TLV_DB_SCALE_ITEM(-3000, 100, 0),
	0x37,	0x3B,	TLV_DB_SCALE_ITEM(2150, 50, 0),
	0x3C,	0x3F,	TLV_DB_SCALE_ITEM(2400, 200, 0),
};
static unsigned int	mc_asoc_tlv_aout[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x6F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_sp[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x6F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_lout[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x77,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_hp[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2F,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x30,	0x43,	TLV_DB_SCALE_ITEM(-3500, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x7F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static const DECLARE_TLV_DB_SCALE(mc_asoc_tlv_ext, -7500, 100, 1);

/* SP Gain */
static unsigned int	mc_asoc_tlv_sp_gain[]	= {
	TLV_DB_RANGE_HEAD(1),
	0x00,	0x04,	TLV_DB_SCALE_ITEM(1200, 100, 0)
};

/* Audio Mode */
static const char	* const audio_mode_play_param_text[] = {
	"off", "audio", "incall", "audio+incall", "incommunication", "karaoke",
	"incall2", "audio+incall2", "incommunication2",
	"incall3", "audio+incall3", "incall4", "audio+incall4",
	"audiocp"
};
static const struct soc_enum	audio_mode_play_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_AUDIO_MODE_PLAY, 0,
		ARRAY_SIZE(audio_mode_play_param_text),
		audio_mode_play_param_text);

static const char	* const audio_mode_cap_param_text[] = {
	"off", "audio", "incall", "audio+incall", "incommunication", "audioex",
	"audiovr", "audiolb"
};
static const struct soc_enum	audio_mode_cap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_AUDIO_MODE_CAP, 0,
		ARRAY_SIZE(audio_mode_cap_param_text),
		audio_mode_cap_param_text);

/* Output Path */
static const char	* const output_path_param_text[] = {
	"SP", "RC", "HP", "HS", "LO1", "LO2", "BT",
	"SP+RC", "SP+HP", "SP+LO1", "SP+LO2", "SP+BT",
	"LO1+RC", "LO1+HP", "LO1+BT", "LO2+RC", "LO2+HP", "LO2+BT",
	"LO1+LO2", "LO2+LO1"
};
static const struct soc_enum	output_path_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_OUTPUT_PATH, 0,
		ARRAY_SIZE(output_path_param_text), output_path_param_text);

/* Input Path */
static const char	* const input_path_param_text[] = {
	"MainMIC", "SubMIC", "2MIC", "Headset", "Bluetooth",
	"VoiceCall", "VoiceUplink", "VoiceDownlink", "Linein1"
};
static const struct soc_enum	input_path_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INPUT_PATH, 0,
		ARRAY_SIZE(input_path_param_text), input_path_param_text);

/* Incall Mic */
static const char	* const incall_mic_param_text[] = {
	"MainMIC", "SubMIC", "2MIC"
};
static const struct soc_enum	incall_mic_sp_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INCALL_MIC_SP, 0,
		ARRAY_SIZE(incall_mic_param_text), incall_mic_param_text);
static const struct soc_enum	incall_mic_rc_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INCALL_MIC_RC, 0,
		ARRAY_SIZE(incall_mic_param_text), incall_mic_param_text);
static const struct soc_enum	incall_mic_hp_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INCALL_MIC_HP, 0,
		ARRAY_SIZE(incall_mic_param_text), incall_mic_param_text);
static const struct soc_enum	incall_mic_lo1_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INCALL_MIC_LO1, 0,
		ARRAY_SIZE(incall_mic_param_text), incall_mic_param_text);
static const struct soc_enum	incall_mic_lo2_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_INCALL_MIC_LO2, 0,
		ARRAY_SIZE(incall_mic_param_text), incall_mic_param_text);

/* Playback Path */
static const char	* const playback_path_sw_param_text[] = {
	"OFF", "ON"
};
static const struct soc_enum	mainmic_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MAINMIC_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);
static const struct soc_enum	submic_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_SUBMIC_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);
static const struct soc_enum	msmic_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_2MIC_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);
static const struct soc_enum	hsmic_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_HSMIC_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);
static const struct soc_enum	btmic_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_BTMIC_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);
static const struct soc_enum	lin1_playback_path_sw_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_LIN1_PLAYBACK_PATH, 0,
		ARRAY_SIZE(playback_path_sw_param_text),
		playback_path_sw_param_text);

/* DTMF Control */
static const char	* const dtmf_control_param_text[] = {
	"OFF", "ON"
};
static const struct soc_enum	dtmf_control_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DTMF_CONTROL, 0,
		ARRAY_SIZE(dtmf_control_param_text), dtmf_control_param_text);

/* DTMF Output */
static const char	* const dtmf_output_param_text[] = {
	"SP", "NORMAL"
};
static const struct soc_enum	dtmf_output_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DTMF_OUTPUT, 0,
		ARRAY_SIZE(dtmf_output_param_text), dtmf_output_param_text);

/* Switch Clock */
static const char	* const switch_clock_param_text[] = {
	"CLKA", "CLKB"
};
static const struct soc_enum	switch_clock_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_SWITCH_CLOCK, 0,
		ARRAY_SIZE(switch_clock_param_text), switch_clock_param_text);

/* Ext MasterSlave */
static const char	* const ext_masterslave_param_text[] = {
	"Slave", "Master"
};
static const struct soc_enum	ext_masterslave_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_MASTERSLAVE, 0,
		ARRAY_SIZE(ext_masterslave_param_text),
		ext_masterslave_param_text);

/* Ext Rate */
static const char	* const ext_rate_param_text[] = {
	"48kHz", "44.1kHz", "32kHz", "", "24kHz", "22.05kHz", "16kHz", "",
	"12kHz", "11.025kHz", "8kHz"
};
static const struct soc_enum	ext_rate_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_RATE, 0,
		ARRAY_SIZE(ext_rate_param_text), ext_rate_param_text);

/* Ext Bitclock Rate */
static const char	* const ext_bck_rate_param_text[] = {
	"64fs", "48fs", "32fs", "", "512fs", "256fs", "192fs", "128fs",
	"96fs", "24fs", "16fs", "8fs", "", "", "", "Slave"
};
static const struct soc_enum	ext_bck_rate_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_BITCLOCK_RATE, 0,
		ARRAY_SIZE(ext_bck_rate_param_text), ext_bck_rate_param_text);

/* Ext Interface */
static const char	* const ext_interface_param_text[] = {
	"DA", "PCM"
};
static const struct soc_enum	ext_interface_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INTERFACE, 0,
		ARRAY_SIZE(ext_interface_param_text), ext_interface_param_text);

/* Ext Bitclock Invert */
static const char	* const ext_bck_invert_param_text[] = {
	"Normal", "Invert"
};
static const struct soc_enum	ext_bck_invert_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_BITCLOCK_INVERT, 0,
		ARRAY_SIZE(ext_bck_invert_param_text),
		ext_bck_invert_param_text);

/* Ext DA Bit Width */
static const char	* const ext_bit_width_param_text[] = {
	"16bit", "20bit", "24bit"
};
static const struct soc_enum	ext_input_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_DA_BIT_WIDTH, 0,
		ARRAY_SIZE(ext_bit_width_param_text), ext_bit_width_param_text);
static const struct soc_enum	ext_output_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_DA_BIT_WIDTH, 0,
		ARRAY_SIZE(ext_bit_width_param_text), ext_bit_width_param_text);

/* Ext DA Format */
static const char	* const ext_da_format_param_text[] = {
	"HeadAlign", "I2S", "TailAlign"
};
static const struct soc_enum	ext_input_da_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_DA_FORMAT, 0,
		ARRAY_SIZE(ext_da_format_param_text), ext_da_format_param_text);
static const struct soc_enum	ext_output_da_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_DA_FORMAT, 0,
		ARRAY_SIZE(ext_da_format_param_text), ext_da_format_param_text);

/* Ext Pcm MonoStereo */
static const char	* const ext_pcm_mono_param_text[] = {
	"Stereo", "Mono"
};
static const struct soc_enum	ext_input_pcm_mono_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_PCM_MONOSTEREO, 0,
		ARRAY_SIZE(ext_pcm_mono_param_text), ext_pcm_mono_param_text);
static const struct soc_enum	ext_output_pcm_mono_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_PCM_MONOSTEREO, 0,
		ARRAY_SIZE(ext_pcm_mono_param_text), ext_pcm_mono_param_text);

/* Ext Pcm Bit Order */
static const char	* const ext_pcm_bit_order_param_text[] = {
	"MSB", "LSB"
};
static const struct soc_enum	ext_input_pcm_bit_order_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_PCM_BIT_ORDER, 0,
		ARRAY_SIZE(ext_pcm_bit_order_param_text),
		ext_pcm_bit_order_param_text);
static const struct soc_enum	ext_output_pcm_bit_order_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_PCM_BIT_ORDER, 0,
		ARRAY_SIZE(ext_pcm_bit_order_param_text),
		ext_pcm_bit_order_param_text);

/* Ext Pcm Format */
static const char	* const ext_pcm_format_param_text[] = {
	"Linear", "Alaw", "Mulaw"
};
static const struct soc_enum	ext_input_pcm_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_PCM_FORMAT, 0,
		ARRAY_SIZE(ext_pcm_format_param_text),
		ext_pcm_format_param_text);
static const struct soc_enum	ext_output_pcm_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_PCM_FORMAT, 0,
		ARRAY_SIZE(ext_pcm_format_param_text),
		ext_pcm_format_param_text);

/* Ext PCM Bit Width */
static const char	* const ext_pcm_bit_width_param_text[] = {
	"8bit", "16bit", "24bit"
};
static const struct soc_enum	ext_input_pcm_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_INPUT_PCM_BIT_WIDTH, 0,
		ARRAY_SIZE(ext_pcm_bit_width_param_text),
		ext_pcm_bit_width_param_text);
static const struct soc_enum	ext_output_pcm_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUTPUT_PCM_BIT_WIDTH, 0,
		ARRAY_SIZE(ext_pcm_bit_width_param_text),
		ext_pcm_bit_width_param_text);

/* Voice MasterSlave */
static const char	* const voice_masterslave_param_text[] = {
	"Slave", "Master"
};
static const struct soc_enum	voice_masterslave_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_MASTERSLAVE, 0,
		ARRAY_SIZE(voice_masterslave_param_text),
		voice_masterslave_param_text);

/* Voice Rate */
static const char	* const voice_rate_param_text[] = {
	"48kHz", "44.1kHz", "32kHz", "", "24kHz", "22.05kHz", "16kHz", "",
	"12kHz", "11.025kHz", "8kHz", "", "192kHz", "96kHz"
};
static const struct soc_enum	voice_rate_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_RATE, 0,
		ARRAY_SIZE(voice_rate_param_text), voice_rate_param_text);

/* Voice Bitclock Rate */
static const char	* const voice_bck_rate_param_text[]  = {
	"64fs", "48fs", "32fs", "", "512fs", "256fs", "192fs", "128fs",
	"96fs", "24fs", "16fs", "8fs", "", "", "", "Slave"
};
static const struct soc_enum	voice_bck_rate_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_BITCLOCK_RATE, 0,
		ARRAY_SIZE(voice_bck_rate_param_text),
			voice_bck_rate_param_text);

/* Voice Interface */
static const char	* const voice_interface_param_text[] = {
	"DA", "PCM"
};
static const struct soc_enum	voice_interface_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INTERFACE, 0,
		ARRAY_SIZE(voice_interface_param_text),
			voice_interface_param_text);

/* Voice Bitclock Invert */
static const char	* const voice_bck_invert_param_text[] = {
	"Normal", "Invert"
};
static const struct soc_enum	voice_bck_invert_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_BITCLOCK_INVERT, 0,
		ARRAY_SIZE(voice_bck_invert_param_text),
		voice_bck_invert_param_text);

/* Voice DA Bit Width */
static const char	* const voice_input_bit_width_param_text[] = {
	"16bit", "20bit", "24bit"
};
static const struct soc_enum	voice_input_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_DA_BIT_WIDTH, 0,
		ARRAY_SIZE(voice_input_bit_width_param_text),
		voice_input_bit_width_param_text);
static const char	* const voice_output_bit_width_param_text[] = {
	"16bit", "20bit", "24bit", "32bit"
};
static const struct soc_enum	voice_output_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_DA_BIT_WIDTH, 0,
		ARRAY_SIZE(voice_output_bit_width_param_text),
		voice_output_bit_width_param_text);

/* Voice DA Format */
static const char	* const voice_da_format_param_text[] = {
	"HeadAlign", "I2S", "TailAlign"
};
static const struct soc_enum	voice_input_da_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_DA_FORMAT, 0,
		ARRAY_SIZE(voice_da_format_param_text),
		voice_da_format_param_text);
static const struct soc_enum	voice_output_da_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_DA_FORMAT, 0,
		ARRAY_SIZE(voice_da_format_param_text),
		voice_da_format_param_text);

/* Voice Pcm MonoStereo */
static const char	* const voice_pcm_mono_param_text[] = {
	"Stereo", "Mono"
};
static const struct soc_enum	voice_input_pcm_mono_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_PCM_MONOSTEREO, 0,
		ARRAY_SIZE(voice_pcm_mono_param_text),
		voice_pcm_mono_param_text);
static const struct soc_enum	voice_output_pcm_mono_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_PCM_MONOSTEREO, 0,
		ARRAY_SIZE(voice_pcm_mono_param_text),
		voice_pcm_mono_param_text);

/* Voice Pcm Bit Order */
static const char	* const voice_pcm_bit_order_param_text[] = {
	"MSB", "LSB"
};
static const struct soc_enum	voice_input_pcm_bit_order_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_PCM_BIT_ORDER, 0,
		ARRAY_SIZE(voice_pcm_bit_order_param_text),
		voice_pcm_bit_order_param_text);
static const struct soc_enum	voice_output_pcm_bit_order_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_PCM_BIT_ORDER, 0,
		ARRAY_SIZE(voice_pcm_bit_order_param_text),
		voice_pcm_bit_order_param_text);

/* Voice Pcm Format */
static const char	* const voice_pcm_format_param_text[] = {
	"Linear", "Alaw", "Mulaw"
};
static const struct soc_enum	voice_input_pcm_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_PCM_FORMAT, 0,
		ARRAY_SIZE(voice_pcm_format_param_text),
		voice_pcm_format_param_text);
static const struct soc_enum	voice_output_pcm_format_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_PCM_FORMAT, 0,
		ARRAY_SIZE(voice_pcm_format_param_text),
		voice_pcm_format_param_text);

/* Voice PCM Bit Width */
static const char	* const voice_pcm_bit_width_param_text[] = {
	"8bit", "16bit", "24bit"
};
static const struct soc_enum	voice_input_pcm_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_INPUT_PCM_BIT_WIDTH, 0,
		ARRAY_SIZE(voice_pcm_bit_width_param_text),
		voice_pcm_bit_width_param_text);
static const struct soc_enum	voice_output_pcm_bit_width_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUTPUT_PCM_BIT_WIDTH, 0,
		ARRAY_SIZE(voice_pcm_bit_width_param_text),
		voice_pcm_bit_width_param_text);

/* Music Physical Port */
static const char	* const phy_port_param_text[] = {
	"DIO0", "DIO1", "DIO2", "NONE", "SLIM0", "SLIM1", "SLIM2"
};
static const struct soc_enum	music_phy_port_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_PHYSICAL_PORT, 0,
		ARRAY_SIZE(phy_port_param_text), phy_port_param_text);

/* Ext Physical Port */
static const struct soc_enum	ext_phy_port_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_PHYSICAL_PORT, 0,
		ARRAY_SIZE(phy_port_param_text), phy_port_param_text);

/* Voice Physical Port */
static const struct soc_enum	voice_phy_port_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_PHYSICAL_PORT, 0,
		ARRAY_SIZE(phy_port_param_text), phy_port_param_text);

/* Hifi Physical Port */
static const struct soc_enum	hifi_phy_port_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_HIFI_PHYSICAL_PORT, 0,
		ARRAY_SIZE(phy_port_param_text), phy_port_param_text);

/* Adif0 Swap */
static const char	* const swap_param_text[] = {
	"Normal", "Swap", "Mute", "Center", "Mix", "MonoMix", "BothL", "BothR"
};
static const struct soc_enum	adif0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Adif1 Swap */
static const struct soc_enum	adif1_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF1_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Adif2 Swap */
static const struct soc_enum	adif2_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF2_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Dac0 Swap */
static const struct soc_enum	dac0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DAC0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Dac1 Swap */
static const struct soc_enum	dac1_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DAC1_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Music Out0 Swap */
static const struct soc_enum	music_out0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Music In0 Swap */
static const char	* const swap2_param_text[] = {
	"Normal", "Both1", "Both0", "Swap"
};
static const struct soc_enum	music_in0_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN0_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music In1 Swap */
static const struct soc_enum	music_in1_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN1_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music In2 Swap */
static const struct soc_enum	music_in2_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN2_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Ext In Swap */
static const struct soc_enum	ext_in_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_IN_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Voice In Swap */
static const struct soc_enum	voice_in_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_IN_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music Out1 Swap */
static const struct soc_enum	music_out1_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT1_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music Out2 Swap */
static const struct soc_enum	music_out2_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT2_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Ext Out Swap */
static const struct soc_enum	ext_out_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUT_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Voice Out Swap */
static const struct soc_enum	voice_out_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUT_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Adif Source */
static const char	* const adif_src_param_text[] = {
	"ymu831_path_cfg.h", "ADC0L", "ADC0R", "ADC1",
	"PDM0L", "PDM0R", "PDM1L", "PDM1R", "DAC0REF", "DAC1REF"
};
static const struct soc_enum	adif_src[]	= {
	SOC_ENUM_DOUBLE(MC_ASOC_ADIF0_SOURCE, 0, 8, 8, adif_src_param_text),
	SOC_ENUM_DOUBLE(MC_ASOC_ADIF1_SOURCE, 0, 8, 8, adif_src_param_text),
	SOC_ENUM_DOUBLE(MC_ASOC_ADIF2_SOURCE, 0, 8, 10, adif_src_param_text)
};
static const char	* const clear_dsp_prm_param_text[] = {
	"OFF", "ON"
};
static const struct soc_enum	clear_dsp_prm_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_CLEAR_DSP_PARAM, 0,
		ARRAY_SIZE(clear_dsp_prm_param_text), clear_dsp_prm_param_text);

/* Parameter Setting */
static const char	* const parameter_setting_param_text[] = {
	"DUMMY"
};
static const struct soc_enum	parameter_setting_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_PARAMETER_SETTING, 0,
		ARRAY_SIZE(parameter_setting_param_text),
		parameter_setting_param_text);

static const char	* const mic_param_text[] = {
	"NONE", "MIC1", "MIC2", "MIC3", "MIC4", "PDM0", "PDM1"
};

/*	Main Mic	*/
static const struct soc_enum	main_mic_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MAIN_MIC, 0, ARRAY_SIZE(mic_param_text),
		mic_param_text);
/*	Sub Mic	*/
static const struct soc_enum	sub_mic_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_SUB_MIC, 0, ARRAY_SIZE(mic_param_text),
		mic_param_text);
/*	Headset Mic	*/
static const struct soc_enum	hs_mic_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_HS_MIC, 0, ARRAY_SIZE(mic_param_text),
		mic_param_text);

#ifdef MC_ASOC_TEST
/*	MICx_BIAS	*/
static const char	* const mic_bias_param_text[] = {
	"OFF", "ALWAYS_ON", "SYNC_MIC"
};
static const struct soc_enum	mic1_bias_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MIC1_BIAS, 0, ARRAY_SIZE(mic_bias_param_text),
		mic_bias_param_text);
static const struct soc_enum	mic2_bias_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MIC2_BIAS, 0, ARRAY_SIZE(mic_bias_param_text),
		mic_bias_param_text);
static const struct soc_enum	mic3_bias_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MIC3_BIAS, 0, ARRAY_SIZE(mic_bias_param_text),
		mic_bias_param_text);
static const struct soc_enum	mic4_bias_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MIC4_BIAS, 0, ARRAY_SIZE(mic_bias_param_text),
		mic_bias_param_text);
#endif

static const struct snd_kcontrol_new	mc_asoc_snd_controls[]	= {
	SOC_DOUBLE_TLV("Music Input Volume",
		MC_ASOC_DVOL_MUSICIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Music Input Switch",
		MC_ASOC_DVOL_MUSICIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Ext Input Volume",
		MC_ASOC_DVOL_EXTIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ext Input Switch",
		MC_ASOC_DVOL_EXTIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Voice Input Volume",
		MC_ASOC_DVOL_VOICEIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Voice Input Switch",
		MC_ASOC_DVOL_VOICEIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Ref Input Volume",
		MC_ASOC_DVOL_REFIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ref Input Switch",
		MC_ASOC_DVOL_REFIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Adif0 Input Volume",
		MC_ASOC_DVOL_ADIF0IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif0 Input Switch",
		MC_ASOC_DVOL_ADIF0IN, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Adif1 Input Volume",
		MC_ASOC_DVOL_ADIF1IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif1 Input Switch",
		MC_ASOC_DVOL_ADIF1IN, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Adif2 Input Volume",
		MC_ASOC_DVOL_ADIF2IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif2 Input Switch",
		MC_ASOC_DVOL_ADIF2IN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Music Output Volume",
		MC_ASOC_DVOL_MUSICOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Music Output Switch",
		MC_ASOC_DVOL_MUSICOUT, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Ext Output Volume",
		MC_ASOC_DVOL_EXTOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ext Output Switch",
		MC_ASOC_DVOL_EXTOUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Voice Output Volume",
		MC_ASOC_DVOL_VOICEOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Voice Output Switch",
		MC_ASOC_DVOL_VOICEOUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Ref Output Volume",
		MC_ASOC_DVOL_REFOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ref Output Switch",
		MC_ASOC_DVOL_REFOUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dac0 Output Volume",
		MC_ASOC_DVOL_DAC0OUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dac0 Output Switch",
		MC_ASOC_DVOL_DAC0OUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dac1 Output Volume",
		MC_ASOC_DVOL_DAC1OUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dac1 Output Switch",
		MC_ASOC_DVOL_DAC1OUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dpath Da Volume",
		MC_ASOC_DVOL_DPATHDA, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dpath Da Switch",
		MC_ASOC_DVOL_DPATHDA, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dpath Ad Volume",
		MC_ASOC_DVOL_DPATHAD, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dpath Ad Switch",
		MC_ASOC_DVOL_DPATHAD, 7, 15, 1, 0),


	SOC_DOUBLE_TLV("LineIn1 Volume",
		MC_ASOC_AVOL_LINEIN1, 0, 8, 63, 0, mc_asoc_tlv_ain),
	SOC_DOUBLE("LineIn1 Switch",
		MC_ASOC_AVOL_LINEIN1, 7, 15, 1, 0),

	SOC_SINGLE_TLV("Mic1 Volume",
		MC_ASOC_AVOL_MIC1, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic1 Switch",
		MC_ASOC_AVOL_MIC1, 7, 1, 0),

	SOC_SINGLE_TLV("Mic2 Volume",
		MC_ASOC_AVOL_MIC2, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic2 Switch",
		MC_ASOC_AVOL_MIC2, 7, 1, 0),

	SOC_SINGLE_TLV("Mic3 Volume",
		MC_ASOC_AVOL_MIC3, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic3 Switch",
		MC_ASOC_AVOL_MIC3, 7, 1, 0),

	SOC_SINGLE_TLV("Mic4 Volume",
		MC_ASOC_AVOL_MIC4, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic4 Switch",
		MC_ASOC_AVOL_MIC4, 7, 1, 0),

	SOC_DOUBLE_TLV("Headphone Volume",
		MC_ASOC_AVOL_HP, 0, 8, 127, 0, mc_asoc_tlv_hp),
	SOC_DOUBLE("Headphone Switch",
		MC_ASOC_AVOL_HP, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Speaker Volume",
		MC_ASOC_AVOL_SP, 0, 8, 127, 0, mc_asoc_tlv_sp),
	SOC_DOUBLE("Speaker Switch",
		MC_ASOC_AVOL_SP, 7, 15, 1, 0),

	SOC_SINGLE_TLV("Receiver Volume",
		MC_ASOC_AVOL_RC, 0, 111, 0, mc_asoc_tlv_aout),
	SOC_SINGLE("Receiver Switch",
		MC_ASOC_AVOL_RC, 7, 1, 0),

	SOC_DOUBLE_TLV("LineOut1 Volume",
		MC_ASOC_AVOL_LINEOUT1, 0, 8, 119, 0, mc_asoc_tlv_lout),
	SOC_DOUBLE("LineOut1 Switch",
		MC_ASOC_AVOL_LINEOUT1, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("LineOut2 Volume",
		MC_ASOC_AVOL_LINEOUT2, 0, 8, 119, 0, mc_asoc_tlv_lout),
	SOC_DOUBLE("LineOut2 Switch",
		MC_ASOC_AVOL_LINEOUT2, 7, 15, 1, 0),

	SOC_SINGLE_TLV("SP Gain",
		MC_ASOC_AVOL_SP_GAIN, 0, 4, 0, mc_asoc_tlv_sp_gain),

	SOC_DOUBLE_TLV("Master Playback Volume",
		MC_ASOC_DVOL_MASTER, 0, 8, 75, 0, mc_asoc_tlv_ext),
	SOC_DOUBLE("Master Playback Switch",
		MC_ASOC_DVOL_MASTER, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Voice Playback Volume",
		MC_ASOC_DVOL_VOICE, 0, 8, 75, 0, mc_asoc_tlv_ext),
	SOC_DOUBLE("Voice Playback Switch",
		MC_ASOC_DVOL_VOICE, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("AnalogIn Playback Analog Volume",
		MC_ASOC_DVOL_APLAY_A, 0, 8, 63, 0, mc_asoc_tlv_ain),
	SOC_DOUBLE("AnalogIn Playback Analog Switch",
		MC_ASOC_DVOL_APLAY_A, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("AnalogIn Playback Digital Volume",
		MC_ASOC_DVOL_APLAY_D, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("AnalogIn Playback Digital Switch",
		MC_ASOC_DVOL_APLAY_D, 7, 15, 1, 0),

	SOC_SINGLE("Voice Recording Switch",
			MC_ASOC_VOICE_RECORDING, 0, 1, 0),

	SOC_ENUM("Audio Mode Playback", audio_mode_play_param_enum),
	SOC_ENUM("Audio Mode Capture", audio_mode_cap_param_enum),
	SOC_ENUM("Output Path", output_path_param_enum),
	SOC_ENUM("Input Path", input_path_param_enum),
	SOC_ENUM("Incall Mic Speaker", incall_mic_sp_param_enum),
	SOC_ENUM("Incall Mic Receiver", incall_mic_rc_param_enum),
	SOC_ENUM("Incall Mic Headphone", incall_mic_hp_param_enum),
	SOC_ENUM("Incall Mic LineOut1", incall_mic_lo1_param_enum),
	SOC_ENUM("Incall Mic LineOut2", incall_mic_lo2_param_enum),
	SOC_ENUM("MainMIC Playback Path",
			mainmic_playback_path_sw_param_enum),
	SOC_ENUM("SubMIC Playback Path", submic_playback_path_sw_param_enum),
	SOC_ENUM("2MIC Playback Path", msmic_playback_path_sw_param_enum),
	SOC_ENUM("HeadsetMIC Playback Path",
			hsmic_playback_path_sw_param_enum),
	SOC_ENUM("BluetoothMIC Playback Path",
			btmic_playback_path_sw_param_enum),
	SOC_ENUM("LIN 1 Playback Path", lin1_playback_path_sw_param_enum),
	SOC_ENUM("DTMF Control", dtmf_control_param_enum),
	SOC_ENUM("DTMF Output", dtmf_output_param_enum),
	SOC_ENUM("Switch Clock", switch_clock_param_enum),
	SOC_ENUM("Ext MasterSlave", ext_masterslave_param_enum),
	SOC_ENUM("Ext Rate", ext_rate_param_enum),
	SOC_ENUM("Ext Bitclock Rate", ext_bck_rate_param_enum),
	SOC_ENUM("Ext Interface", ext_interface_param_enum),
	SOC_ENUM("Ext Bitclock Invert", ext_bck_invert_param_enum),
	SOC_ENUM("Ext Input DA Bit Width", ext_input_bit_width_param_enum),
	SOC_ENUM("Ext Output DA Bit Width",
			ext_output_bit_width_param_enum),
	SOC_ENUM("Ext Input DA Format", ext_input_da_format_param_enum),
	SOC_ENUM("Ext Output DA Format", ext_output_da_format_param_enum),
	SOC_ENUM("Ext Input Pcm MonoStereo", ext_input_pcm_mono_param_enum),
	SOC_ENUM("Ext Output Pcm MonoStereo", ext_output_pcm_mono_param_enum),
	SOC_ENUM("Ext Input Pcm Bit Order", ext_input_pcm_bit_order_param_enum),
	SOC_ENUM("Ext Output Pcm Bit Order",
			ext_output_pcm_bit_order_param_enum),
	SOC_ENUM("Ext Input Pcm Format", ext_input_pcm_format_param_enum),
	SOC_ENUM("Ext Output Pcm Format", ext_output_pcm_format_param_enum),
	SOC_ENUM("Ext Input PCM Bit Width", ext_input_pcm_bit_width_param_enum),
	SOC_ENUM("Ext Output PCM Bit Width",
			ext_output_pcm_bit_width_param_enum),
	SOC_ENUM("Voice MasterSlave", voice_masterslave_param_enum),
	SOC_ENUM("Voice Rate", voice_rate_param_enum),
	SOC_ENUM("Voice Bitclock Rate", voice_bck_rate_param_enum),
	SOC_ENUM("Voice Interface", voice_interface_param_enum),
	SOC_ENUM("Voice Bitclock Invert", voice_bck_invert_param_enum),
	SOC_ENUM("Voice Input DA Bit Width",
			voice_input_bit_width_param_enum),
	SOC_ENUM("Voice Output DA Bit Width",
			voice_output_bit_width_param_enum),
	SOC_ENUM("Voice Input DA Format", voice_input_da_format_param_enum),
	SOC_ENUM("Voice Output DA Format", voice_output_da_format_param_enum),
	SOC_ENUM("Voice Input Pcm MonoStereo", voice_input_pcm_mono_param_enum),
	SOC_ENUM("Voice Output Pcm MonoStereo",
			voice_output_pcm_mono_param_enum),
	SOC_ENUM("Voice Input Pcm Bit Order",
			voice_input_pcm_bit_order_param_enum),
	SOC_ENUM("Voice Output Pcm Bit Order",
			voice_output_pcm_bit_order_param_enum),
	SOC_ENUM("Voice Input Pcm Format", voice_input_pcm_format_param_enum),
	SOC_ENUM("Voice Output Pcm Format", voice_output_pcm_format_param_enum),
	SOC_ENUM("Voice Input PCM Bit Width",
			voice_input_pcm_bit_width_param_enum),
	SOC_ENUM("Voice Output PCM Bit Width",
			voice_output_pcm_bit_width_param_enum),
	SOC_ENUM("Music Physical Port", music_phy_port_param_enum),
	SOC_ENUM("Ext Physical Port", ext_phy_port_param_enum),
	SOC_ENUM("Voice Physical Port", voice_phy_port_param_enum),
	SOC_ENUM("Hifi Physical Port", hifi_phy_port_param_enum),
	SOC_ENUM("Adif0 Swap", adif0_swap_param_enum),
	SOC_ENUM("Adif1 Swap", adif1_swap_param_enum),
	SOC_ENUM("Adif2 Swap", adif2_swap_param_enum),
	SOC_ENUM("Dac0 Swap", dac0_swap_param_enum),
	SOC_ENUM("Dac1 Swap", dac1_swap_param_enum),
	SOC_ENUM("Music Out0 Swap", music_out0_swap_param_enum),
	SOC_ENUM("Music In0 Swap", music_in0_param_enum),
	SOC_ENUM("Music In1 Swap", music_in1_param_enum),
	SOC_ENUM("Music In2 Swap", music_in2_param_enum),
	SOC_ENUM("Ext In Swap", ext_in_param_enum),
	SOC_ENUM("Voice In Swap", voice_in_param_enum),
	SOC_ENUM("Music Out1 Swap", music_out1_param_enum),
	SOC_ENUM("Music Out2 Swap", music_out2_param_enum),
	SOC_ENUM("Ext Out Swap", ext_out_param_enum),
	SOC_ENUM("Voice Out Swap", voice_out_param_enum),

	SOC_ENUM("ADIF0 Source", adif_src[0]),
	SOC_ENUM("ADIF1 Source", adif_src[1]),
	SOC_ENUM("ADIF2 Source", adif_src[2]),

	SOC_SINGLE("Dsp Parameter", MC_ASOC_DSP_PARAM, 0,
					ARRAY_SIZE(firmware_name)-1, 0),
	SOC_SINGLE("Dsp Parameter Option", MC_ASOC_DSP_PARAM_OPT, 0,
					YMC_DSP_VOICECALL_BASE_COMMON, 0),
	SOC_ENUM("Clear Dsp Parameter", clear_dsp_prm_param_enum),

	SOC_SINGLE("Playback Scenario", MC_ASOC_PLAYBACK_SCENARIO, 0,
					500, 0),
	SOC_SINGLE("Capture Scenario", MC_ASOC_CAPTURE_SCENARIO, 0,
					500, 0),

	SOC_ENUM("Parameter Setting", parameter_setting_param_enum)
	, SOC_ENUM("Main Mic", main_mic_param_enum)
	, SOC_ENUM("Sub Mic", sub_mic_param_enum)
	, SOC_ENUM("Headset Mic", hs_mic_param_enum)
#ifdef MC_ASOC_TEST
	, SOC_ENUM("MIC1 BIAS", mic1_bias_param_enum)
	, SOC_ENUM("MIC2 BIAS", mic2_bias_param_enum)
	, SOC_ENUM("MIC3 BIAS", mic3_bias_param_enum)
	, SOC_ENUM("MIC4 BIAS", mic4_bias_param_enum)
#endif
};

struct snd_soc_codec		*mc_asoc_codec;
static struct spi_device	*mc_asoc_spi;

static UINT8	mc_asoc_ver_id		= 1;
static UINT8	mc_asoc_hold		= YMC_NOTITY_HOLD_OFF;
static UINT8	mc_asoc_suspended;
static UINT8	mc_asoc_hpimpclass	= (UINT8)-1;
static UINT8	mc_asoc_jack_status;
static UINT8	mc_asoc_irq_func;

static struct MCDRV_VOL_INFO	mc_asoc_vol_info_mute;

static UINT8	mc_asoc_main_mic	= MAIN_MIC;
static UINT8	mc_asoc_sub_mic		= SUB_MIC;
static UINT8	mc_asoc_hs_mic		= HEADSET_MIC;
static UINT8	mc_asoc_mic1_bias	= MIC1_BIAS;
static UINT8	mc_asoc_mic2_bias	= MIC2_BIAS;
static UINT8	mc_asoc_mic3_bias	= MIC3_BIAS;
static UINT8	mc_asoc_mic4_bias	= MIC4_BIAS;
static UINT8	mc_asoc_MBSEL4		= 0x80;

static UINT8	mc_asoc_audio_play_port	= DIO_MUSIC;
static UINT8	mc_asoc_audio_cap_port	= DIO_MUSIC;
static UINT8	mc_asoc_voice_port	= DIO_EXT;
static UINT8	mc_asoc_port_rate	= MCDRV_FS_48000;

static struct mutex	mc_asoc_mutex;
static struct mutex	hsdet_mutex;

static struct mc_asoc_data *mc_asoc_get_mc_asoc(
			const struct snd_soc_codec *codec);
static int	map_drv_error(int err);
static int	set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level);
/*
 * Function
 */
UINT8 mc_asoc_get_bus_select(void)
{
	return BUS_SELECT;
}

void mc_asoc_set_enable_clock_func(
	int (*pcbfunc)(struct snd_soc_codec *, int, bool))
{
	struct mc_asoc_data	*mc_asoc	= NULL;

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	mc_asoc->penableclkfn	= pcbfunc;
}

void mc_asoc_enable_clock(int enable)
{
#ifdef FEATURE_MCLK_CONTROL_BY_YMU831
	/* To do */
#endif
}

static void mc_asoc_lock(char *fn)
{
	mutex_lock(&mc_asoc_mutex);
}

static void mc_asoc_unlock(char *fn)
{
	mutex_unlock(&mc_asoc_mutex);
}

static struct mc_asoc_data *mc_asoc_get_mc_asoc(
	const struct snd_soc_codec *codec)
{
	struct mc_asoc_priv	*priv;

	if (codec == NULL)
		return NULL;

	priv	= snd_soc_codec_get_drvdata((struct snd_soc_codec *)codec);
	if (priv != NULL)
		return &priv->data;

	return NULL;
}

/* deliver i2c access to machdep */
static int map_drv_error(int err)
{
	switch (err) {
	case MCDRV_SUCCESS:
		return 0;
	case MCDRV_ERROR_ARGUMENT:
		return -EINVAL;
	case MCDRV_ERROR_STATE:
		return -EBUSY;
	case MCDRV_ERROR_TIMEOUT:
		return -EIO;
	default:
		/* internal error */
		return -EIO;
	}
}


static int read_cache(
	struct snd_soc_codec *codec,
	unsigned int reg)
{
	int		ret;
	unsigned int	val;

	ret	= snd_soc_cache_read(codec, reg, &val);
	if (ret != 0) {
		dev_err(codec->dev, "Cache read to %x failed: %d\n", reg, ret);
		return -EIO;
	}
	return val;
}

static int write_cache(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value)
{
	return snd_soc_cache_write(codec, reg, value);
}

#define	DSP_MEM_STATIC
#ifdef DSP_MEM_STATIC
#define	DSP_MEM_SIZE	(200000)
static int	dsp_mem_pt;
static UINT8	dsp_mem[DSP_MEM_SIZE];
#else
#include "linux/vmalloc.h"
#endif

static UINT8 *get_dsp_mem(int size)
{
#ifdef DSP_MEM_STATIC
	UINT8	*p	= NULL;
	if ((dsp_mem_pt+size) < DSP_MEM_SIZE) {
		p	= dsp_mem + dsp_mem_pt;
		dsp_mem_pt	+= size;
		dbg_info("dsp_mem_pt:%d\n", dsp_mem_pt);
	} else {
		pr_info("mem alloc failed!\n");
	}
	return p;
#else
	return vmalloc(size);
#endif
}

#include <linux/firmware.h>
static int load_file(const char *fn, char **fp)
{
	int	ret;
	struct device	*dev	= mc_asoc_codec->dev;
	const struct firmware	*fw;
	char	filename[512];

	TRACE_FUNC();

	strcpy(filename, MC_ASOC_HWDEP_ID);
	strcat(filename, "/");
	strcat(filename, fn);
	ret	= request_firmware(&fw, filename, dev);
	if (ret != 0) {
		pr_err("request_firmware failed(errno %d) for %s", ret, fn);
		return ret;
	}

	*fp	= get_dsp_mem(fw->size);
	if (*fp == NULL) {
		printk(KERN_INFO "Out of memory loading '%s'.\n", fn);
		release_firmware(fw);
		return -EIO;
	}
	memcpy(*fp, fw->data, fw->size);
	ret	= fw->size;

	release_firmware(fw);
	return ret;
}

static int get_mic_block_on(UINT8 mic)
{
	switch (mic) {
	case	MIC_1:
		return MCDRV_ASRC_MIC1_ON;
	case	MIC_2:
		return MCDRV_ASRC_MIC2_ON;
	case	MIC_3:
		return MCDRV_ASRC_MIC3_ON;
	case	MIC_4:
		return MCDRV_ASRC_MIC4_ON;
	default:
		return -1;
	}
}

static int get_main_mic_block_on(void)
{
	return get_mic_block_on(mc_asoc_main_mic);
}
static int get_sub_mic_block_on(void)
{
	return get_mic_block_on(mc_asoc_sub_mic);
}
static int get_hs_mic_block_on(void)
{
	return get_mic_block_on(mc_asoc_hs_mic);
}

static int get_unused_mic_block_on(void)
{
	int	ret	= MCDRV_ASRC_MIC1_ON
			| MCDRV_ASRC_MIC2_ON
			| MCDRV_ASRC_MIC3_ON
			| MCDRV_ASRC_MIC4_ON;
	if ((mc_asoc_main_mic == MIC_1)
	|| (mc_asoc_sub_mic == MIC_1)
	|| (mc_asoc_hs_mic == MIC_1))
		ret	&= ~MCDRV_ASRC_MIC1_ON;
	if ((mc_asoc_main_mic == MIC_2)
	|| (mc_asoc_sub_mic == MIC_2)
	|| (mc_asoc_hs_mic == MIC_2))
		ret	&= ~MCDRV_ASRC_MIC2_ON;
	if ((mc_asoc_main_mic == MIC_3)
	|| (mc_asoc_sub_mic == MIC_3)
	|| (mc_asoc_hs_mic == MIC_3))
		ret	&= ~MCDRV_ASRC_MIC3_ON;
	if ((mc_asoc_main_mic == MIC_4)
	|| (mc_asoc_sub_mic == MIC_4)
	|| (mc_asoc_hs_mic == MIC_4))
		ret	&= ~MCDRV_ASRC_MIC4_ON;
	return ret;
}


static UINT8 get_incall_mic(
	struct snd_soc_codec *codec,
	int	output_path
)
{
	switch (output_path) {
	case	MC_ASOC_OUTPUT_PATH_SP:
		return read_cache(codec, MC_ASOC_INCALL_MIC_SP);
		break;
	case	MC_ASOC_OUTPUT_PATH_RC:
	case	MC_ASOC_OUTPUT_PATH_SP_RC:
	case	MC_ASOC_OUTPUT_PATH_LO1_RC:
	case	MC_ASOC_OUTPUT_PATH_LO2_RC:
		return read_cache(codec, MC_ASOC_INCALL_MIC_RC);
		break;
	case	MC_ASOC_OUTPUT_PATH_HP:
	case	MC_ASOC_OUTPUT_PATH_SP_HP:
	case	MC_ASOC_OUTPUT_PATH_LO1_HP:
	case	MC_ASOC_OUTPUT_PATH_LO2_HP:
		return read_cache(codec, MC_ASOC_INCALL_MIC_HP);
		break;
	case	MC_ASOC_OUTPUT_PATH_LO1:
	case	MC_ASOC_OUTPUT_PATH_SP_LO1:
	case	MC_ASOC_OUTPUT_PATH_LO2_LO1:
		return read_cache(codec, MC_ASOC_INCALL_MIC_LO1);
		break;
	case	MC_ASOC_OUTPUT_PATH_LO2:
	case	MC_ASOC_OUTPUT_PATH_SP_LO2:
	case	MC_ASOC_OUTPUT_PATH_LO1_LO2:
		return read_cache(codec, MC_ASOC_INCALL_MIC_LO2);
		break;
	case	MC_ASOC_OUTPUT_PATH_HS:
	case	MC_ASOC_OUTPUT_PATH_BT:
	case	MC_ASOC_OUTPUT_PATH_SP_BT:
	case	MC_ASOC_OUTPUT_PATH_LO1_BT:
	case	MC_ASOC_OUTPUT_PATH_LO2_BT:
		return MC_ASOC_INCALL_MIC_MAINMIC;
	default:
		break;
	}
	return -EIO;
}

struct mc_asoc_mixer_path_ctl_info {
	int	audio_mode_play;
	int	audio_mode_cap;
	int	output_path;
	int	input_path;
	int	incall_mic;
	int	mainmic_play;
	int	submic_play;
	int	msmic_play;
	int	hsmic_play;
	int	btmic_play;
	int	lin1_play;
	int	dtmf_control;
	int	dtmf_output;
};

static int get_mixer_path_ctl_info(
	struct snd_soc_codec *codec,
	struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info
)
{
	mixer_ctl_info->audio_mode_play =
		read_cache(codec, MC_ASOC_AUDIO_MODE_PLAY);
	if (mixer_ctl_info->audio_mode_play < 0)
		return -EIO;

	mixer_ctl_info->audio_mode_cap =
		read_cache(codec, MC_ASOC_AUDIO_MODE_CAP);
	if (mixer_ctl_info->audio_mode_cap < 0)
		return -EIO;

	mixer_ctl_info->output_path =
		read_cache(codec, MC_ASOC_OUTPUT_PATH);
	if (mixer_ctl_info->output_path < 0)
		return -EIO;

	mixer_ctl_info->input_path =
		read_cache(codec, MC_ASOC_INPUT_PATH);
	if (mixer_ctl_info->input_path < 0)
		return -EIO;

	mixer_ctl_info->incall_mic =
		get_incall_mic(codec, mixer_ctl_info->output_path);
	if (mixer_ctl_info->incall_mic < 0)
		return -EIO;

	mixer_ctl_info->dtmf_control =
		read_cache(codec, MC_ASOC_DTMF_CONTROL);
	if (mixer_ctl_info->dtmf_control < 0)
		return -EIO;

	mixer_ctl_info->dtmf_output =
		read_cache(codec, MC_ASOC_DTMF_OUTPUT);
	if (mixer_ctl_info->dtmf_output < 0)
		return -EIO;

	mixer_ctl_info->mainmic_play =
		read_cache(codec, MC_ASOC_MAINMIC_PLAYBACK_PATH);
	if (mixer_ctl_info->mainmic_play < 0)
		return -EIO;

	mixer_ctl_info->submic_play =
		read_cache(codec, MC_ASOC_SUBMIC_PLAYBACK_PATH);
	if (mixer_ctl_info->submic_play < 0)
		return -EIO;

	mixer_ctl_info->msmic_play =
		read_cache(codec, MC_ASOC_2MIC_PLAYBACK_PATH);
	if (mixer_ctl_info->msmic_play < 0)
		return -EIO;

	mixer_ctl_info->hsmic_play =
		read_cache(codec, MC_ASOC_HSMIC_PLAYBACK_PATH);
	if (mixer_ctl_info->hsmic_play < 0)
		return -EIO;

	mixer_ctl_info->btmic_play =
		read_cache(codec, MC_ASOC_BTMIC_PLAYBACK_PATH);
	if (mixer_ctl_info->btmic_play < 0)
		return -EIO;

	mixer_ctl_info->lin1_play =
		read_cache(codec, MC_ASOC_LIN1_PLAYBACK_PATH);
	if (mixer_ctl_info->lin1_play < 0)
		return -EIO;

	return 0;
}

int ymu831_get_codec_suspended(struct snd_soc_codec *codec)
{
	struct mc_asoc_data     *mc_asoc        = NULL;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
	int check_codec_suspend = 0;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL) {
		pr_err("%s: Can not mc_asoc", __func__);
		return -1;
	}

	if (get_mixer_path_ctl_info(codec, &mixer_ctl_info) < 0) {
		pr_err("%s: get_mixer_path_ctl_info failed", __func__);
		return -EIO;
	}

	if ((mixer_ctl_info.audio_mode_play == 0)
	&& (mixer_ctl_info.audio_mode_cap == 0)
	&& (mixer_ctl_info.mainmic_play == 0)
	&& (mixer_ctl_info.submic_play == 0)
	&& (mixer_ctl_info.msmic_play == 0)
	&& (mixer_ctl_info.hsmic_play == 0)
	&& (mixer_ctl_info.btmic_play == 0)
	&& (mixer_ctl_info.lin1_play == 0)
	&& (mixer_ctl_info.dtmf_control == 0))
		check_codec_suspend = 1;
	else
		check_codec_suspend = 0;

	pr_info("%s: %d", __func__, check_codec_suspend);

	return check_codec_suspend;
}

static int get_path_preset_idx(
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info
)
{
	if ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM)
	&& ((mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM) ||
	    (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT)
			return 25;
		else if ((mixer_ctl_info->output_path
			== MC_ASOC_OUTPUT_PATH_SP_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO1_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO2_BT)
		)
			return 26;
		else
			return 24;
	}
	if ((mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM)
	&& ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM) ||
	    (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_OFF))) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT)
			return 25;
		else if ((mixer_ctl_info->output_path
			== MC_ASOC_OUTPUT_PATH_SP_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO1_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO2_BT)
		)
			return 26;
		else
			return 24;
	}

	if ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM2)
	&& ((mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM) ||
	    (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT)
			return 63;
		else if ((mixer_ctl_info->output_path
			== MC_ASOC_OUTPUT_PATH_SP_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO1_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO2_BT)
		)
			return 64;
		else
			return 62;
	}

	if (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL) {
		if (mixer_ctl_info->audio_mode_cap
			== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 13;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 14;
			else
				return 12;
		}
		if (mixer_ctl_info->audio_mode_cap
			== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 19;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 20;
			else
				return 18;
		}
	}

	if (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL2) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 51;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 52;
			else
				return 50;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 57;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 58;
			else
				return 56;
		}
	}

	if (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL3) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 66;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 67;
			else
				return 65;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 72;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 73;
			else
				return 71;
		}
	}

	if (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL4) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 78;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 79;
			else
				return 77;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 84;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			 || (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 85;
			else
				return 83;
		}
	}

	if (mixer_ctl_info->audio_mode_play
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
		if (mixer_ctl_info->audio_mode_cap
			== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 16;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 17;
			else
				return 15;
		}
		if (mixer_ctl_info->audio_mode_cap
			== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 22;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 23;
			else
				return 21;
		}
	}

	if (mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL2) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 54;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 55;
			else
				return 53;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 60;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 61;
			else
				return 59;
		}
	}

	if (mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL3) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 69;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 70;
			else
				return 68;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 75;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 76;
			else
				return 74;
		}
	}

	if (mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL4) {
		if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 81;
			else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 82;
			else
				return 80;
		}
		if (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 87;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 88;
			else
				return 86;
		}
	}

	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCALL))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
	|| ((mixer_ctl_info->audio_mode_play
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
	) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			if ((mc_asoc_port_rate == MCDRV_FS_96000)
			|| (mc_asoc_port_rate == MCDRV_FS_192000))
				return -1;
			return 2;
		} else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
			if ((mc_asoc_port_rate == MCDRV_FS_96000)
			|| (mc_asoc_port_rate == MCDRV_FS_192000))
				return -1;
			return 3;
		} else {
			if ((mc_asoc_port_rate == MCDRV_FS_96000)
			|| (mc_asoc_port_rate == MCDRV_FS_192000))
				return 27;
			return 1;
		}
	}
	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_OFF)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_OFF)
	 && (mixer_ctl_info->audio_mode_cap
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM)
	 && (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM2)
	 && (mixer_ctl_info->audio_mode_cap
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL))) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				return 5;
			} else {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return 28;
				return 4;
			}
		} else {
			if ((mc_asoc_port_rate == MCDRV_FS_96000)
			|| (mc_asoc_port_rate == MCDRV_FS_192000))
				return 28;
			return 4;
		}
	}
	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL))
	|| ((mixer_ctl_info->audio_mode_play
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO))
	) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					return 9;
				else
					return 8;
			}
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					return 11;
				else
					return 10;
			}
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				return 7;
			} else {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return 29;
				return 6;
			}
		} else {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				return 8;
			}
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return -1;
				return 10;
			} else {
				if ((mc_asoc_port_rate == MCDRV_FS_96000)
				|| (mc_asoc_port_rate == MCDRV_FS_192000))
					return 29;
				return 6;
			}
		}
	}

	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_OFF)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 31;
			else
				return 30;
		} else
			return 30;
	}
	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL2)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL3)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	|| ((mixer_ctl_info->audio_mode_play
					== MC_ASOC_AUDIO_MODE_AUDIO_INCALL4)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOEX))
	) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT) {
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					return 35;
				else
					return 34;
			}
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					return 37;
				else
					return 36;
			}
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 33;
			else
				return 32;
		} else {
			if (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_BT)
				return 34;
			if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT))
				return 36;
			else
				return 32;
		}
	}

	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_OFF)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOVR))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOVR))
	|| ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_INCOMM)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOVR))) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 39;
			else
				return 38;
		} else
			return 38;
	}
	if (((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOVR))
	|| ((mixer_ctl_info->audio_mode_play
		== MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	 && (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOVR))) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 43;
			else
				return 42;
		}
		if ((mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_SP_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO1_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO2_BT)
		) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 45;
			else
				return 44;
		}
		if (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_BT)
			return 41;
		else
			return 40;
	}

	if (mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_KARAOKE) {
		if (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 47;
			else
				return 46;
		} else if (mixer_ctl_info->audio_mode_cap
						== MC_ASOC_AUDIO_MODE_AUDIO) {
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
				return 49;
			else
				return 48;
		}
	}
	if ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIO)
	&& (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIOLB)) {
		if ((mc_asoc_port_rate == MCDRV_FS_96000)
		|| (mc_asoc_port_rate == MCDRV_FS_192000)) {
			;
			return -1;
		}
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			return 90;
		} else if ((mixer_ctl_info->output_path
			== MC_ASOC_OUTPUT_PATH_SP_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO1_BT)
		|| (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_LO2_BT)
		) {
			return 91;
		} else {
			return 89;
		}
	}

	if ((mixer_ctl_info->audio_mode_play == MC_ASOC_AUDIO_MODE_AUDIOCP)
	&& (mixer_ctl_info->audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF)) {
		;
		return 92;
	}

	return 0;
}

static int is_incall(
	int	preset_idx
)
{
	if (((preset_idx >= 12) && (preset_idx <= 23))
	|| ((preset_idx >= 50) && (preset_idx <= 61))
	|| ((preset_idx >= 65) && (preset_idx <= 88))
	)
		return 1;

	return 0;
}

static int is_incall_BT(
	int	preset_idx
)
{
	if ((preset_idx == 13)
	|| (preset_idx == 14)
	|| (preset_idx == 16)
	|| (preset_idx == 17)
	|| (preset_idx == 19)
	|| (preset_idx == 20)
	|| (preset_idx == 22)
	|| (preset_idx == 23)
	|| (preset_idx == 51)
	|| (preset_idx == 52)
	|| (preset_idx == 54)
	|| (preset_idx == 55)
	|| (preset_idx == 57)
	|| (preset_idx == 58)
	|| (preset_idx == 60)
	|| (preset_idx == 61)
	|| (preset_idx == 66)
	|| (preset_idx == 67)
	|| (preset_idx == 69)
	|| (preset_idx == 70)
	|| (preset_idx == 72)
	|| (preset_idx == 73)
	|| (preset_idx == 75)
	|| (preset_idx == 76)
	|| (preset_idx == 78)
	|| (preset_idx == 79)
	|| (preset_idx == 81)
	|| (preset_idx == 82)
	|| (preset_idx == 84)
	|| (preset_idx == 85)
	|| (preset_idx == 87)
	|| (preset_idx == 88)
	)
		return 1;

	return 0;
}

static int is_incommunication(
	int	preset_idx
)
{
	if (((preset_idx >= 24) && (preset_idx <= 26))
	|| ((preset_idx >= 62) && (preset_idx <= 64)))
		return 1;

	return 0;
}

static void set_vol_mute_flg(size_t offset, UINT8 lr, UINT8 mute)
{
	SINT16	*vp	= (SINT16 *)((void *)&mc_asoc_vol_info_mute+offset);

	if (offset == (size_t)-1)
		return;

	if (mute == 1)
		*(vp+lr)	= 0xA000;
	else
		*(vp+lr)	= 0;
}

static UINT8 get_vol_mute_flg(size_t offset, UINT8 lr)
{
	SINT16	*vp;

	if (offset == (size_t)-1)
		return 1;	/*	mute	*/

	vp	= (SINT16 *)((void *)&mc_asoc_vol_info_mute+offset);
	return (*(vp+lr) != 0);
}

static int get_master_vol(
	struct snd_soc_codec *codec,
	SINT16	*db,
	int	reg,
	int	i
)
{
	int	cache;
	unsigned int	v;
	int	sw, vol;
	SINT32	sum;

/*	TRACE_FUNC();*/

	cache	= read_cache(codec, MC_ASOC_DVOL_MASTER);
	if (cache < 0)
		return -EIO;
	v	= (cache >> (i*8)) & 0xff;
	sw	= (v & 0x80);
	vol	= sw ? (v & 0x7f) : 0;
	if (vol == 0)
		*db	= vreg_map[reg].volmap[0];
	else {
		sum	= *db + vreg_map[MC_ASOC_DVOL_MASTER].volmap[vol];
		if (sum < vreg_map[reg].volmap[0])
			sum	= vreg_map[reg].volmap[0];
		*db	= sum;
	}
	/*dbg_info("db=%d\n", *db);*/
	return 0;
}

static int get_voice_vol(
	struct snd_soc_codec *codec,
	SINT16	*db,
	int	reg,
	int	i
)
{
	int	cache;
	unsigned int	v;
	int	sw, vol;
	SINT32	sum;

/*	TRACE_FUNC();*/

	cache	= read_cache(codec, MC_ASOC_DVOL_VOICE);
	if (cache < 0)
		return -EIO;
	v	= (cache >> (i*8)) & 0xff;
	sw	= (v & 0x80);
	vol	= sw ? (v & 0x7f) : 0;
	if (vol == 0)
		*db	= vreg_map[reg].volmap[0];
	else {
		sum	= *db + vreg_map[MC_ASOC_DVOL_VOICE].volmap[vol];
		if (sum < vreg_map[reg].volmap[0])
			sum	= vreg_map[reg].volmap[0];
		*db	= sum;
	}
	return 0;
}

static int get_aplay_vol(
	struct snd_soc_codec *codec,
	SINT16	*db,
	int	reg,
	int	i,
	int	aplay_reg,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	int	cache;
	unsigned int	v;
	int	sw, vol;
	SINT16	aplay_db;

/*	TRACE_FUNC();*/

	if ((preset_idx >= 46)
	&& (preset_idx <= 49))
		return 0;

	if ((is_incall(preset_idx) != 0)
	|| (is_incommunication(preset_idx) != 0))
		return 0;

	if ((preset_idx < 12)
	|| (preset_idx == 28)
	|| (preset_idx > 29))
		;
	else
		return 0;

	cache	= read_cache(codec, aplay_reg);
	if (cache < 0)
		return -EIO;
	v	= (cache >> (i*8)) & 0xff;
	sw	= (v & 0x80);
	vol	= sw ? (v & 0x7f) : 0;
	aplay_db	= vreg_map[reg].volmap[vol];

	if (reg == MC_ASOC_DVOL_ADIF1IN) {
		if (mixer_ctl_info->lin1_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_LIN1)) {
				*db	= aplay_db;
				return 0;
			}
		}
		if (mixer_ctl_info->mainmic_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_MAINMIC)) {
				*db	= aplay_db;
				return 0;
			}
		}
		if (mixer_ctl_info->submic_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_SUBMIC)) {
				*db	= aplay_db;
				return 0;
			}
		}
		if (mixer_ctl_info->msmic_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_2MIC)) {
				*db	= aplay_db;
				return 0;
			}
		}
		if (mixer_ctl_info->hsmic_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS)) {
				*db	= aplay_db;
				return 0;
			}
		}
		return 0;
	}

	if (reg == MC_ASOC_AVOL_LINEIN1) {
		if (mixer_ctl_info->lin1_play == 1) {
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_LIN1))
				*db	= aplay_db;
		}
		return 0;
	}

	if (reg == MC_ASOC_AVOL_MIC1) {
		if (mc_asoc_main_mic == MIC_1) {
			if (mixer_ctl_info->mainmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_MAINMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if (mc_asoc_sub_mic == MIC_1) {
			if (mixer_ctl_info->submic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_SUBMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if ((mc_asoc_hs_mic == MIC_1)
		&& (mixer_ctl_info->hsmic_play == 1))
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS))
				*db	= aplay_db;

		return 0;
	}

	if (reg == MC_ASOC_AVOL_MIC2) {
		if (mc_asoc_main_mic == MIC_2) {
			if (mixer_ctl_info->mainmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_MAINMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if (mc_asoc_sub_mic == MIC_2) {
			if (mixer_ctl_info->submic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_SUBMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if ((mc_asoc_hs_mic == MIC_2)
		&& (mixer_ctl_info->hsmic_play == 1))
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS))
				*db	= aplay_db;

		return 0;
	}

	if (reg == MC_ASOC_AVOL_MIC3) {
		if (mc_asoc_main_mic == MIC_3) {
			if (mixer_ctl_info->mainmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_MAINMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if (mc_asoc_sub_mic == MIC_3) {
			if (mixer_ctl_info->submic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_SUBMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if ((mc_asoc_hs_mic == MIC_3)
		&& (mixer_ctl_info->hsmic_play == 1))
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS))
				*db	= aplay_db;

		return 0;
	}

	if (reg == MC_ASOC_AVOL_MIC4) {
		if (mc_asoc_main_mic == MIC_4) {
			if (mixer_ctl_info->mainmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_MAINMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if (mc_asoc_sub_mic == MIC_4) {
			if (mixer_ctl_info->submic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_SUBMIC)) {
					*db	= aplay_db;
					return 0;
				}
			if (mixer_ctl_info->msmic_play == 1)
				if ((preset_idx < 4)
				|| (preset_idx >= 38)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
				|| (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_2MIC)) {
					*db	= aplay_db;
					return 0;
				}
		}
		if ((mc_asoc_hs_mic == MIC_4)
		&& (mixer_ctl_info->hsmic_play == 1))
			if ((preset_idx < 4)
			|| (preset_idx >= 38)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_BT)
			|| (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS))
				*db	= aplay_db;

		return 0;
	}
	return 0;
}

static int set_vol_info(
	struct snd_soc_codec *codec,
	struct MCDRV_VOL_INFO	*vol_info,
	int	reg,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	SINT16	*vp;
	int	i;
	int	cache;

/*	TRACE_FUNC();*/

	if (codec == NULL)
		return -EIO;

	if (reg >= MC_ASOC_AVOL_SP_GAIN)
		return -EIO;

	if (vreg_map[reg].offset != (size_t)-1) {
		vp	= (SINT16 *)((void *)vol_info +
				vreg_map[reg].offset);
		cache	= read_cache(codec, reg);
		if (cache < 0)
			return -EIO;

		for (i = 0; i < vreg_map[reg].channels; i++, vp++) {
			unsigned int	v;
			int	sw, vol;
			SINT16	db;

			v	= (cache >> (i*8)) & 0xff;
			sw	= (v & 0x80);

			if (is_incall(preset_idx) != 0) {
				if (reg == MC_ASOC_DVOL_VOICEOUT) {
					if (((mc_asoc_voice_port == DIO_VOICE)
					  || (mc_asoc_voice_port == DIO_EXT))
					&& (sw != 0)) {
						;
						sw	= read_cache(codec,
						      MC_ASOC_VOICE_RECORDING);
					}
				} else if (reg == MC_ASOC_DVOL_DAC0OUT) {
					if ((mc_asoc_voice_port == LIN1_LOUT1)
					&& (sw != 0))
						sw	= read_cache(codec,
						MC_ASOC_VOICE_RECORDING);
				} else if (reg == MC_ASOC_DVOL_DAC1OUT) {
					if ((mc_asoc_voice_port == LIN1_LOUT2)
					&& (sw != 0))
						sw	= read_cache(codec,
						MC_ASOC_VOICE_RECORDING);
				}
			} else if (is_incommunication(preset_idx) != 0) {
				if (reg == MC_ASOC_DVOL_VOICEOUT) {
					if (sw != 0) {
						sw	= read_cache(codec,
						      MC_ASOC_VOICE_RECORDING);
					}
				}
			}

			vol	= sw ? (v & 0x7f) : 0;

			if (get_vol_mute_flg(
				vreg_map[reg].offset, i) != 0)
				db	= vreg_map[reg].volmap[0];
			else
				db	= vreg_map[reg].volmap[vol];

			if (reg == MC_ASOC_DVOL_MUSICIN) {
				if ((mc_asoc_audio_play_port != DIO_MUSIC)
				|| (vol == 0))
					db	= vreg_map[reg].volmap[0];
				else if (get_master_vol(codec, &db, reg, i)
					!= 0)
					return -EIO;
			} else if (reg == MC_ASOC_DVOL_VOICEIN) {
				if (is_incall(preset_idx) == 0)
					db	= vreg_map[reg].volmap[vol];
				else if ((mc_asoc_voice_port == LIN1_LOUT1)
				|| (mc_asoc_voice_port == LIN1_LOUT2)
				|| (vol == 0))
					db	= vreg_map[reg].volmap[0];
				else if ((mc_asoc_voice_port == DIO_EXT)
					&& (is_incall_BT(preset_idx) == 0))
					db	= vreg_map[reg].volmap[vol];
				else if (get_voice_vol(codec, &db, reg, i)
					!= 0)
					return -EIO;
			} else if (reg == MC_ASOC_DVOL_EXTIN) {
				if (is_incall(preset_idx) == 0)
					db	= vreg_map[reg].volmap[vol];
				else if ((mc_asoc_voice_port == DIO_VOICE)
					|| (is_incall_BT(preset_idx) != 0))
					db	= vreg_map[reg].volmap[vol];
				else if (get_voice_vol(codec, &db, reg, i)
					!= 0)
					return -EIO;
			} else if (reg == MC_ASOC_DVOL_ADIF1IN) {
				if (get_aplay_vol(codec, &db, reg, i,
						MC_ASOC_DVOL_APLAY_D,
						mixer_ctl_info,
						preset_idx) != 0)
					return -EIO;
				if (mc_asoc_audio_play_port == LIN1)
					if (get_master_vol(
						codec, &db, reg, i) != 0)
						return -EIO;
			} else if (reg == MC_ASOC_AVOL_LINEIN1) {
				if (is_incall(preset_idx) != 0) {
					if (((mc_asoc_voice_port == LIN1_LOUT1)
					  || (mc_asoc_voice_port == LIN1_LOUT2))
					&& (get_voice_vol(codec, &db, reg, i)
							!= 0)) {
						;
						return -EIO;
					}
				} else {
					if (get_aplay_vol(
						codec, &db, reg, i,
						MC_ASOC_DVOL_APLAY_A,
						mixer_ctl_info,
						preset_idx) != 0)
						return -EIO;
				}
			} else if ((reg == MC_ASOC_AVOL_MIC1)
				|| (reg == MC_ASOC_AVOL_MIC2)
				|| (reg == MC_ASOC_AVOL_MIC3)
				|| (reg == MC_ASOC_AVOL_MIC4)) {
				if (get_aplay_vol(codec, &db, reg, i,
					MC_ASOC_DVOL_APLAY_A,
					mixer_ctl_info,
					preset_idx) != 0)
					return -EIO;
			} else if (reg == MC_ASOC_AVOL_HP) {
				if ((mc_asoc_hpimpclass != (UINT8)-1)
				&& (db > vreg_map[reg].volmap[0])) {
					SINT16	db_max	=
		vreg_map[MC_ASOC_AVOL_HP].volmap[ARRAY_SIZE(volmap_hp)-1];
					db	+=
					(aswHpVolImpTable[mc_asoc_hpimpclass]
						<<8);
					if (db <
					  vreg_map[MC_ASOC_AVOL_HP].volmap[0])
						db	=
					vreg_map[MC_ASOC_AVOL_HP].volmap[0];
					else if (db > db_max)
						db	= db_max;
				}
			} else if (reg == MC_ASOC_DVOL_DAC0OUT) {
				if ((mc_asoc_hpimpclass != (UINT8)-1)
				&& (db > vreg_map[reg].volmap[0])) {
					SINT16	db_max	=
				volmap_digital[ARRAY_SIZE(volmap_digital)-1];
					db	+=
					(aswDac0VolImpTable[mc_asoc_hpimpclass]
						<<8);
					if (db < volmap_digital[0])
						db	= volmap_digital[0];
					else if (db > db_max)
						db	= db_max;
				}
			}
			*vp	= db | MCDRV_VOL_UPDATE;
		}
	}
	return 0;
}
static int set_volume(
	struct snd_soc_codec *codec,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	struct MCDRV_VOL_INFO	vol_info;
	int	err;
	int	reg;

	/*TRACE_FUNC();*/

	if (codec == NULL)
		return -EIO;

	memset(&vol_info, 0, sizeof(struct MCDRV_VOL_INFO));

	for (reg = MC_ASOC_DVOL_MUSICIN; reg < MC_ASOC_AVOL_SP_GAIN; reg++) {
		err	= set_vol_info(codec, &vol_info, reg, mixer_ctl_info,
				preset_idx);
		if (err < 0)
			return err;
	}

	err	= _McDrv_Ctrl(MCDRV_SET_VOLUME, &vol_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in MCDRV_SET_VOLUME\n", err);
		return -EIO;
	}

	return 0;
}

static void mask_AnaOut_src(
	struct MCDRV_PATH_INFO	*path_info,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	UINT8	bCh;

	/*TRACE_FUNC();*/

	if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_LO1)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_LO2)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_BT))
		for (bCh = 0; bCh < SP_PATH_CHANNELS; bCh++)
			path_info->asSp[bCh].dSrcOnOff	= 0x002AAAAA;

	if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_RC))
		for (bCh = 0; bCh < RC_PATH_CHANNELS; bCh++)
				path_info->asRc[bCh].dSrcOnOff	= 0x002AAAAA;

	if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_HS)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_HP))
		for (bCh = 0; bCh < HP_PATH_CHANNELS; bCh++)
			path_info->asHp[bCh].dSrcOnOff	= 0x002AAAAA;

	if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_LO1)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_BT)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_LO2)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_LO1)) {
		if (preset_idx < 12) {
			if (mc_asoc_audio_cap_port == LOUT1) {
				if ((preset_idx <= 3)
				|| (preset_idx == 6)
				|| (preset_idx == 7))
					for (bCh = 0;
						bCh < LOUT1_PATH_CHANNELS;
						bCh++) {
						;
					      path_info->asLout1[bCh].dSrcOnOff
						= 0x002AAAAA;
					}
			} else
				for (bCh = 0; bCh < LOUT1_PATH_CHANNELS; bCh++)
					path_info->asLout1[bCh].dSrcOnOff
						= 0x002AAAAA;
		} else if (is_incall(preset_idx) != 0) {
			/*	incall	*/
			if (mc_asoc_voice_port == LIN1_LOUT1) {
				;
			} else if (mc_asoc_audio_cap_port == LOUT1) {
				if ((preset_idx != 18)
				&& (preset_idx != 21)
				&& (preset_idx != 56)
				&& (preset_idx != 59)
				&& (preset_idx != 71)
				&& (preset_idx != 74)
				&& (preset_idx != 83)
				&& (preset_idx != 86)
				)
					for (bCh = 0;
						bCh < LOUT1_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asLout1[bCh].dSrcOnOff
							= 0x002AAAAA;
					}
			} else {
				for (bCh = 0; bCh < LOUT1_PATH_CHANNELS; bCh++)
					path_info->asLout1[bCh].dSrcOnOff
						= 0x002AAAAA;
			}
		} else if ((preset_idx == 24)
			|| (preset_idx >= 26)) {
			for (bCh = 0; bCh < LOUT1_PATH_CHANNELS; bCh++)
				path_info->asLout1[bCh].dSrcOnOff
					= 0x002AAAAA;
		} else if (preset_idx == 25) {
			;
		}
	}

	if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_LO2)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_RC)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_HP)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_BT)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_LO2)
	&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_LO1)) {
		if (preset_idx < 12) {
			if (mc_asoc_audio_cap_port == LOUT2) {
				if ((preset_idx <= 3)
				|| (preset_idx == 6)
				|| (preset_idx == 7))
					for (bCh = 0;
						bCh < LOUT2_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asLout2[bCh].dSrcOnOff
							= 0x002AAAAA;
					}
			} else
				for (bCh = 0; bCh < LOUT2_PATH_CHANNELS; bCh++)
					path_info->asLout2[bCh].dSrcOnOff
						= 0x002AAAAA;
		} else if (is_incall(preset_idx) != 0) {
			if (mc_asoc_voice_port == LIN1_LOUT2)
				;
			else if (mc_asoc_audio_cap_port == LOUT2) {
				if ((preset_idx != 18)
				&& (preset_idx != 21)
				&& (preset_idx != 56)
				&& (preset_idx != 59)
				&& (preset_idx != 71)
				&& (preset_idx != 74)
				&& (preset_idx != 83)
				&& (preset_idx != 86)
				)
					for (bCh = 0;
						bCh < LOUT2_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asLout2[bCh].dSrcOnOff
						= 0x002AAAAA;
					}
			} else {
				for (bCh = 0; bCh < LOUT2_PATH_CHANNELS; bCh++)
					path_info->asLout2[bCh].dSrcOnOff
						= 0x002AAAAA;
			}
		} else if ((preset_idx == 24)
			|| (preset_idx >= 26)) {
			for (bCh = 0; bCh < LOUT2_PATH_CHANNELS; bCh++)
				path_info->asLout2[bCh].dSrcOnOff	=
					0x002AAAAA;
		} else if (preset_idx == 25) {
			;
		}
	}
}

static void mask_BTOut_src(
	struct MCDRV_PATH_INFO	*path_info,
	int	output_path
)
{
	UINT8	bCh;

	/*TRACE_FUNC();*/

	if ((output_path != MC_ASOC_OUTPUT_PATH_BT)
	&& (output_path != MC_ASOC_OUTPUT_PATH_SP_BT)
	&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_BT)
	&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_BT))
		for (bCh = 0; bCh < EXTOUT_PATH_CHANNELS; bCh++)
			path_info->asExtOut[bCh].dSrcOnOff
				= 0x00AAAAAA;
}

static void mask_ADC_src(
	struct MCDRV_PATH_INFO		*path_info,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	int	main_mic_block_on	= get_main_mic_block_on();
	int	sub_mic_block_on	= get_sub_mic_block_on();
	int	hs_mic_block_on		= get_hs_mic_block_on();
	int	unused_mic_block_on	= get_unused_mic_block_on();
	UINT8	bCh;

	/*TRACE_FUNC();*/

	if ((is_incall(preset_idx) == 0)
	&& (is_incommunication(preset_idx) == 0)) {
		/*	!incall	*/
		if ((preset_idx == 4)
		|| (preset_idx == 6)
		|| (preset_idx == 8)
		|| (preset_idx == 10)
		|| (preset_idx == 28)
		|| (preset_idx == 29)
		|| (preset_idx == 30)
		|| (preset_idx == 32)
		|| (preset_idx == 34)
		|| (preset_idx == 36)
		|| (preset_idx == 46)
		|| (preset_idx == 48)) {
			/*	in capture	*/
			if ((mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_MAINMIC)
			&& (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_2MIC)) {
				if (main_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~main_mic_block_on;
					}
			}
			if ((mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_SUBMIC)
			&&  (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_2MIC)) {
				if (sub_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~sub_mic_block_on;
					}
			}
			if (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_HS) {
				if (hs_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~hs_mic_block_on;
					}
			}
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_2MIC) {
				path_info->asAdc0[0].dSrcOnOff
					&= ~sub_mic_block_on;
				path_info->asAdc0[1].dSrcOnOff
					&= ~main_mic_block_on;
			}
			if (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_LIN1) {
				path_info->asAdc0[0].dSrcOnOff
					&= ~MCDRV_ASRC_LINEIN1_L_ON;
				path_info->asAdc0[1].dSrcOnOff
					&= ~MCDRV_ASRC_LINEIN1_R_ON;
			}
		} else {
			if ((mixer_ctl_info->mainmic_play != 1)
			&& (mixer_ctl_info->msmic_play != 1)) {
				if (main_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~main_mic_block_on;
					}
			}
			if ((mixer_ctl_info->submic_play != 1)
			&& (mixer_ctl_info->msmic_play != 1)) {
				if (sub_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~sub_mic_block_on;
					}
			}
			if (mixer_ctl_info->hsmic_play != 1) {
				if (hs_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~hs_mic_block_on;
					}
			}
			if (mixer_ctl_info->lin1_play != 1) {
				for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
				bCh++) {
					path_info->asAdc0[bCh].dSrcOnOff
						&= ~MCDRV_ASRC_LINEIN1_L_ON;
					path_info->asAdc0[bCh].dSrcOnOff
						&= ~MCDRV_ASRC_LINEIN1_M_ON;
					path_info->asAdc0[bCh].dSrcOnOff
						&= ~MCDRV_ASRC_LINEIN1_R_ON;
				}
			}
		}
	} else {
		/*	incall or incommunication	*/
		if ((mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_BT)
		&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP_BT)
		&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO1_BT)
		&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_LO2_BT)
		) {
			if (mixer_ctl_info->output_path
				!= MC_ASOC_OUTPUT_PATH_HS) {
				if (hs_mic_block_on != -1)
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~hs_mic_block_on;
					}

				if ((mixer_ctl_info->incall_mic
					!= MC_ASOC_INCALL_MIC_MAINMIC)
				&& (mixer_ctl_info->incall_mic
					!= MC_ASOC_INCALL_MIC_2MIC)
				&& (main_mic_block_on != -1)) {
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~main_mic_block_on;
					}
				}
				if ((mixer_ctl_info->incall_mic
					!= MC_ASOC_INCALL_MIC_SUBMIC)
				&& (mixer_ctl_info->incall_mic
					!= MC_ASOC_INCALL_MIC_2MIC)
				&& (sub_mic_block_on != -1)) {
					for (bCh = 0;
						bCh < ADC0_PATH_CHANNELS;
						bCh++) {
							;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~sub_mic_block_on;
					}
				}
				if (mixer_ctl_info->incall_mic
					== MC_ASOC_INCALL_MIC_2MIC) {
					path_info->asAdc0[0].dSrcOnOff
						&= ~sub_mic_block_on;
					path_info->asAdc0[1].dSrcOnOff
						&= ~main_mic_block_on;
				}
			} else {
				if (main_mic_block_on != -1) {
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~main_mic_block_on;
					}
				}
				if (sub_mic_block_on != -1) {
					for (bCh = 0; bCh < ADC0_PATH_CHANNELS;
						bCh++) {
						;
					    path_info->asAdc0[bCh].dSrcOnOff
							&= ~sub_mic_block_on;
					}
				}
			}
		}
	}
	for (bCh = 0; bCh < ADC0_PATH_CHANNELS; bCh++)
		path_info->asAdc0[bCh].dSrcOnOff &= ~unused_mic_block_on;
}

static void mask_ADC1_src(
	struct MCDRV_PATH_INFO		*path_info,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	int	main_mic_block_on	= get_main_mic_block_on();
	int	sub_mic_block_on	= get_sub_mic_block_on();
	int	hs_mic_block_on		= get_hs_mic_block_on();
	int	unused_mic_block_on	= get_unused_mic_block_on();

	/*TRACE_FUNC();*/

	/*	!incall	*/
	if ((preset_idx == 38)
	|| (preset_idx == 40)
	|| (preset_idx == 42)
	|| (preset_idx == 44)) {
		/*	in capture	*/
		if ((mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_MAINMIC)
		&& (mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_2MIC)) {
			if (main_mic_block_on != -1)
				path_info->asAdc1[0].dSrcOnOff
						&= ~main_mic_block_on;
		}
		if ((mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_SUBMIC)
		&&  (mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_2MIC)) {
			if (sub_mic_block_on != -1)
				path_info->asAdc1[0].dSrcOnOff
						&= ~sub_mic_block_on;
		}
		if (mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_HS) {
			if (hs_mic_block_on != -1)
				path_info->asAdc1[0].dSrcOnOff
						&= ~hs_mic_block_on;
		}
		if (mixer_ctl_info->input_path != MC_ASOC_INPUT_PATH_LIN1) {
			;
			path_info->asAdc1[0].dSrcOnOff
				&= ~MCDRV_ASRC_LINEIN1_M_ON;
		}
	}
	path_info->asAdc1[0].dSrcOnOff &= ~unused_mic_block_on;
}

static void mask_DacRef(
	struct MCDRV_PATH_INFO	*path_info,
	int	output_path
)
{
	UINT8	bCh;

	/*TRACE_FUNC();*/

	switch (output_path) {
	case	MC_ASOC_OUTPUT_PATH_SP:
	case	MC_ASOC_OUTPUT_PATH_LO2:
	case	MC_ASOC_OUTPUT_PATH_SP_LO2:
	case	MC_ASOC_OUTPUT_PATH_SP_BT:
	case	MC_ASOC_OUTPUT_PATH_LO2_BT:
	case	MC_ASOC_OUTPUT_PATH_LO1_LO2:
		for (bCh = 0; bCh < ADIF2_PATH_CHANNELS; bCh++)
			path_info->asAdif2[bCh].dSrcOnOff
				&= ~MCDRV_D2SRC_DAC0REF_ON;
		break;
	case	MC_ASOC_OUTPUT_PATH_RC:
	case	MC_ASOC_OUTPUT_PATH_HP:
	case	MC_ASOC_OUTPUT_PATH_HS:
	case	MC_ASOC_OUTPUT_PATH_LO1:
	case	MC_ASOC_OUTPUT_PATH_LO1_RC:
	case	MC_ASOC_OUTPUT_PATH_LO1_HP:
	case	MC_ASOC_OUTPUT_PATH_LO1_BT:
	case	MC_ASOC_OUTPUT_PATH_SP_RC:
	case	MC_ASOC_OUTPUT_PATH_SP_HP:
	case	MC_ASOC_OUTPUT_PATH_SP_LO1:
	case	MC_ASOC_OUTPUT_PATH_LO2_RC:
	case	MC_ASOC_OUTPUT_PATH_LO2_HP:
	case	MC_ASOC_OUTPUT_PATH_LO2_LO1:
		for (bCh = 0; bCh < ADIF2_PATH_CHANNELS; bCh++)
			path_info->asAdif2[bCh].dSrcOnOff
				&= ~MCDRV_D2SRC_DAC1REF_ON;
		break;
	default:
		break;
	}
}

static void add_path_info(
	struct MCDRV_PATH_INFO	*dst_path_info,
	struct MCDRV_PATH_INFO	*src_path_info
)
{
	UINT8	bCh;

	/*TRACE_FUNC();*/

	for (bCh = 0; bCh < MUSICOUT_PATH_CHANNELS; bCh++)
		dst_path_info->asMusicOut[bCh].dSrcOnOff
			|= src_path_info->asMusicOut[bCh].dSrcOnOff;

	for (bCh = 0; bCh < EXTOUT_PATH_CHANNELS; bCh++)
		dst_path_info->asExtOut[bCh].dSrcOnOff
			|= src_path_info->asExtOut[bCh].dSrcOnOff;

	for (bCh = 0; bCh < HIFIOUT_PATH_CHANNELS; bCh++)
		dst_path_info->asHifiOut[bCh].dSrcOnOff
			|= src_path_info->asHifiOut[bCh].dSrcOnOff;

	for (bCh = 0; bCh < VBOXMIXIN_PATH_CHANNELS; bCh++)
		dst_path_info->asVboxMixIn[bCh].dSrcOnOff
			|= src_path_info->asVboxMixIn[bCh].dSrcOnOff;

	for (bCh = 0; bCh < AE_PATH_CHANNELS; bCh++) {
		dst_path_info->asAe0[bCh].dSrcOnOff
			|= src_path_info->asAe0[bCh].dSrcOnOff;
		dst_path_info->asAe1[bCh].dSrcOnOff
			|= src_path_info->asAe1[bCh].dSrcOnOff;
		dst_path_info->asAe2[bCh].dSrcOnOff
			|= src_path_info->asAe2[bCh].dSrcOnOff;
		dst_path_info->asAe3[bCh].dSrcOnOff
			|= src_path_info->asAe3[bCh].dSrcOnOff;
	}

	for (bCh = 0; bCh < DAC0_PATH_CHANNELS; bCh++)
		dst_path_info->asDac0[bCh].dSrcOnOff
			|= src_path_info->asDac0[bCh].dSrcOnOff;

	for (bCh = 0; bCh < DAC1_PATH_CHANNELS; bCh++)
		dst_path_info->asDac1[bCh].dSrcOnOff
			|= src_path_info->asDac1[bCh].dSrcOnOff;

	for (bCh = 0; bCh < VOICEOUT_PATH_CHANNELS; bCh++)
		dst_path_info->asVoiceOut[bCh].dSrcOnOff
			|= src_path_info->asVoiceOut[bCh].dSrcOnOff;

	for (bCh = 0; bCh < VBOXIOIN_PATH_CHANNELS; bCh++)
		dst_path_info->asVboxIoIn[bCh].dSrcOnOff
			|= src_path_info->asVboxIoIn[bCh].dSrcOnOff;

	for (bCh = 0; bCh < VBOXHOSTIN_PATH_CHANNELS; bCh++)
		dst_path_info->asVboxHostIn[bCh].dSrcOnOff
			|= src_path_info->asVboxHostIn[bCh].dSrcOnOff;

	for (bCh = 0; bCh < HOSTOUT_PATH_CHANNELS; bCh++)
		dst_path_info->asHostOut[bCh].dSrcOnOff
			|= src_path_info->asHostOut[bCh].dSrcOnOff;

	for (bCh = 0; bCh < ADIF0_PATH_CHANNELS; bCh++)
		dst_path_info->asAdif0[bCh].dSrcOnOff
			|= src_path_info->asAdif0[bCh].dSrcOnOff;

	for (bCh = 0; bCh < ADIF1_PATH_CHANNELS; bCh++)
		dst_path_info->asAdif1[bCh].dSrcOnOff
			|= src_path_info->asAdif1[bCh].dSrcOnOff;

	for (bCh = 0; bCh < ADIF2_PATH_CHANNELS; bCh++)
		dst_path_info->asAdif2[bCh].dSrcOnOff
			|= src_path_info->asAdif2[bCh].dSrcOnOff;

	for (bCh = 0; bCh < ADC0_PATH_CHANNELS; bCh++)
		dst_path_info->asAdc0[bCh].dSrcOnOff
			|= src_path_info->asAdc0[bCh].dSrcOnOff;

	for (bCh = 0; bCh < ADC1_PATH_CHANNELS; bCh++)
		dst_path_info->asAdc1[bCh].dSrcOnOff
			|= src_path_info->asAdc1[bCh].dSrcOnOff;

	for (bCh = 0; bCh < HP_PATH_CHANNELS; bCh++)
		dst_path_info->asHp[bCh].dSrcOnOff
			|= src_path_info->asHp[bCh].dSrcOnOff;

	for (bCh = 0; bCh < SP_PATH_CHANNELS; bCh++)
		dst_path_info->asSp[bCh].dSrcOnOff
			|= src_path_info->asSp[bCh].dSrcOnOff;

	for (bCh = 0; bCh < RC_PATH_CHANNELS; bCh++)
		dst_path_info->asRc[bCh].dSrcOnOff
			|= src_path_info->asRc[bCh].dSrcOnOff;

	for (bCh = 0; bCh < LOUT1_PATH_CHANNELS; bCh++)
		dst_path_info->asLout1[bCh].dSrcOnOff
			|= src_path_info->asLout1[bCh].dSrcOnOff;

	for (bCh = 0; bCh < LOUT2_PATH_CHANNELS; bCh++)
		dst_path_info->asLout2[bCh].dSrcOnOff
			|= src_path_info->asLout2[bCh].dSrcOnOff;

	for (bCh = 0; bCh < BIAS_PATH_CHANNELS; bCh++)
		dst_path_info->asBias[bCh].dSrcOnOff
			|= src_path_info->asBias[bCh].dSrcOnOff;
}

static void exchange_ADCtoPDM(
	struct MCDRV_PATH_INFO	*path_info,
	UINT32	dPdmLOn,
	UINT32	dPdmROn
)
{
	UINT32	dAdcOn	= MCDRV_D2SRC_ADC0_L_ON
			 |MCDRV_D2SRC_ADC0_R_ON;
	UINT32	dAdcOff	= MCDRV_D2SRC_ADC0_L_OFF
			 |MCDRV_D2SRC_ADC0_R_OFF;

	/*TRACE_FUNC();*/

	if (dPdmLOn != 0) {
		path_info->asAdif1[0].dSrcOnOff	&= ~dAdcOn;
		path_info->asAdif1[0].dSrcOnOff	|= dAdcOff;
		path_info->asAdif1[0].dSrcOnOff	|= dPdmLOn;
	}
	if (dPdmROn != 0) {
		path_info->asAdif1[1].dSrcOnOff	&= ~dAdcOn;
		path_info->asAdif1[1].dSrcOnOff	|= dAdcOff;
		path_info->asAdif1[1].dSrcOnOff	|= dPdmROn;
	}
}

static void exchange_ADC1toPDM(
	struct MCDRV_PATH_INFO	*path_info,
	UINT32	dPdmLOn,
	UINT32	dPdmROn
)
{
	UINT32	dAdcOn	= MCDRV_D2SRC_ADC1_ON;
	UINT32	dAdcOff	= MCDRV_D2SRC_ADC1_OFF;

	/*TRACE_FUNC();*/

	if (dPdmLOn != 0) {
		path_info->asAdif0[0].dSrcOnOff	&= ~dAdcOn;
		path_info->asAdif0[0].dSrcOnOff	|= dAdcOff;
		path_info->asAdif0[0].dSrcOnOff	|= dPdmLOn;
	}
	if (dPdmROn != 0) {
		path_info->asAdif0[1].dSrcOnOff	&= ~dAdcOn;
		path_info->asAdif0[1].dSrcOnOff	|= dAdcOff;
		path_info->asAdif0[1].dSrcOnOff	|= dPdmROn;
	}
}

static void set_ain_play_path(
	struct MCDRV_PATH_INFO	*path_info,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx,
	int	ignore_input_path
)
{
	int	idx	= AnalogPathMapping[preset_idx];

	/*TRACE_FUNC();*/

	if (idx >= ARRAY_SIZE(AnalogInputPath)) {
		dbg_info("\n********\nAnalogPathMapping err\n********\n");
		return;
	}

	if (mixer_ctl_info->mainmic_play == 1) {
		if ((ignore_input_path != 0)
		|| (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_MAINMIC)
		) {
			add_path_info(path_info,
			(struct MCDRV_PATH_INFO *)&AnalogInputPath[idx]);
			if (mc_asoc_main_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM0_L_ON, MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_main_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM1_L_ON, MCDRV_D2SRC_PDM1_R_ON);
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
			return;
		}
	}
	if (mixer_ctl_info->submic_play == 1) {
		if ((ignore_input_path != 0)
		|| (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_SUBMIC)) {
			add_path_info(path_info,
			(struct MCDRV_PATH_INFO *)&AnalogInputPath[idx]);
			if (mc_asoc_sub_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM0_L_ON, MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_sub_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM1_L_ON, MCDRV_D2SRC_PDM1_R_ON);
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
			return;
		}
	}
	if (mixer_ctl_info->hsmic_play == 1) {
		if ((ignore_input_path != 0)
		|| (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_HS)) {
			add_path_info(path_info,
			(struct MCDRV_PATH_INFO *)&AnalogInputPath[idx]);
			if (mc_asoc_hs_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM0_L_ON, MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_hs_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM1_L_ON, MCDRV_D2SRC_PDM1_R_ON);
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
			return;
		}
	}
	if (mixer_ctl_info->msmic_play == 1) {
		if ((ignore_input_path != 0)
		|| (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_2MIC)) {
			add_path_info(path_info,
			(struct MCDRV_PATH_INFO *)&AnalogInputPath[idx]);
			if (mc_asoc_main_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM0_L_ON, 0);
			else if (mc_asoc_main_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
				MCDRV_D2SRC_PDM1_L_ON, 0);
			if (mc_asoc_sub_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
				0, MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_sub_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
				0, MCDRV_D2SRC_PDM1_R_ON);
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
			return;
		}
	}
	if (mixer_ctl_info->lin1_play == 1) {
		if ((ignore_input_path != 0)
		|| (mixer_ctl_info->input_path == MC_ASOC_INPUT_PATH_LIN1)) {
			add_path_info(path_info,
				(struct MCDRV_PATH_INFO *)&AnalogInputPath[idx]);
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
			return;
		}
	}
}

static void set_BIAS(
	struct MCDRV_PATH_INFO	*path_info
)
{
	int	i;
	int	mic_bias[BIAS_PATH_CHANNELS]	= {
		mc_asoc_mic1_bias,
		mc_asoc_mic2_bias,
		mc_asoc_mic3_bias,
		mc_asoc_mic4_bias};
	UINT32	dOn[BIAS_PATH_CHANNELS]		= {
		MCDRV_ASRC_MIC1_ON,
		MCDRV_ASRC_MIC2_ON,
		MCDRV_ASRC_MIC3_ON,
		MCDRV_ASRC_MIC4_ON};
	UINT8	bCh;
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	err;
	struct MCDRV_HSDET_INFO	stHSDetInfo;
	struct MCDRV_REG_INFO	reg_info;

	for (i = 0; i < BIAS_PATH_CHANNELS; i++) {
		switch (mic_bias[i]) {
		case	BIAS_ON_ALWAYS:
			path_info->asBias[i].dSrcOnOff	|= dOn[i];
			break;
		case	BIAS_OFF:
			path_info->asBias[i].dSrcOnOff	&= ~dOn[i];
			break;
		case	BIAS_SYNC_MIC:
			path_info->asBias[i].dSrcOnOff	&= ~dOn[i];
			for (bCh = 0; bCh < ADC0_PATH_CHANNELS; bCh++) {
				if ((path_info->asAdc0[bCh].dSrcOnOff & dOn[i])
					!= 0) {
					path_info->asBias[i].dSrcOnOff
						|= dOn[i];
					break;
				}
			}
			for (bCh = 0; bCh < ADC1_PATH_CHANNELS; bCh++) {
				if ((path_info->asAdc1[bCh].dSrcOnOff & dOn[i])
					!= 0) {
					path_info->asBias[i].dSrcOnOff
						|= dOn[i];
					break;
				}
			}
			break;
		default:
			break;
		}
	}

	if (mc_asoc_jack_status == SND_JACK_HEADPHONE) {
		err	= _McDrv_Ctrl(MCDRV_GET_HSDET,
					(void *)&stHSDetInfo, NULL, 0);
		if ((err == MCDRV_SUCCESS)
		&& (stHSDetInfo.bEnMicDet == MCDRV_MICDET_ENABLE)) {
			;
			path_info->asBias[3].dSrcOnOff	|=
						MCDRV_ASRC_MIC4_ON;
		}
	} else if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		;
		path_info->asBias[3].dSrcOnOff	|= MCDRV_ASRC_MIC4_ON;
	}

	if (((path_info->asHp[0].dSrcOnOff & MCDRV_ASRC_DAC0_L_ON) != 0)
	|| ((path_info->asHp[1].dSrcOnOff & MCDRV_ASRC_DAC0_R_ON) != 0)
	|| ((path_info->asAdc0[0].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0)
	|| ((path_info->asAdc0[1].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0)
	|| ((path_info->asAdc1[0].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0)) {
		reg_info.bRegType = MCDRV_REGTYPE_ANA;
		reg_info.bAddress = 13;
		err	= _McDrv_Ctrl(MCDRV_READ_REG, (void *)&reg_info, NULL,
					0);
		if (err == MCDRV_SUCCESS) {
			reg_info.bData &= 0x3F;
			reg_info.bData |= 0x80;
			err	= _McDrv_Ctrl(MCDRV_WRITE_REG,
						(void *)&reg_info, NULL, 0);
		}
	} else {
		reg_info.bRegType = MCDRV_REGTYPE_ANA;
		reg_info.bAddress = 13;
		err	= _McDrv_Ctrl(MCDRV_READ_REG, (void *)&reg_info, NULL,
					0);
		if (err == MCDRV_SUCCESS) {
			reg_info.bData &= 0x3F;
			reg_info.bData	|= mc_asoc_MBSEL4;
			err	= _McDrv_Ctrl(MCDRV_WRITE_REG,
						(void *)&reg_info, NULL, 0);
		}
	}
	if (err != MCDRV_SUCCESS) {
		;
		dbg_info("%d: Error in set_BIAS\n", err);
	}

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	if (mc_asoc->pdata != NULL) {
		if (mc_asoc->pdata->set_ext_micbias != NULL) {
			if ((path_info->asAdc0[0].dSrcOnOff&MCDRV_ASRC_MIC1_ON)
			|| (path_info->asAdc0[1].dSrcOnOff&MCDRV_ASRC_MIC1_ON)
			|| (path_info->asAdc1[0].dSrcOnOff&MCDRV_ASRC_MIC1_ON)
			) {
				;
				(*mc_asoc->pdata->set_ext_micbias)(1);
			} else {
				(*mc_asoc->pdata->set_ext_micbias)(0);
			}
		}
	}
}

static void get_path_info(
	struct MCDRV_PATH_INFO	*path_info,
	const struct mc_asoc_mixer_path_ctl_info	*mixer_ctl_info,
	int	preset_idx
)
{
	int	ain_play	= 0;
	UINT8	mute_DIT	= 0;
	int	idx;
	struct MCDRV_PATH_INFO	*preset_path_info;

	/*TRACE_FUNC();*/

	if ((mixer_ctl_info->mainmic_play == 1)
	|| (mixer_ctl_info->submic_play == 1)
	|| (mixer_ctl_info->msmic_play == 1)
	|| (mixer_ctl_info->hsmic_play == 1)
	|| (mixer_ctl_info->lin1_play == 1))
		ain_play	= 1;
	else
		ain_play	= 0;

	preset_path_info	=
		(struct MCDRV_PATH_INFO *)&stPresetPathInfo[preset_idx];

	if (mixer_ctl_info->dtmf_control == 1) {
		if ((mixer_ctl_info->dtmf_output == MC_ASOC_DTMF_OUTPUT_SP)
		&& (mixer_ctl_info->output_path != MC_ASOC_OUTPUT_PATH_SP)) {
			;
		} else {
			idx	= DtmfPathMapping[preset_idx];
			if (idx >= ARRAY_SIZE(DtmfPath)) {
				dbg_info("\n***\nDtmfPathMapping err\n***\n");
				return;
			}
			add_path_info(path_info,
				(struct MCDRV_PATH_INFO *)&DtmfPath[idx]);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
		}
	}

	set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_EXTOUT].offset, 0, 0);
	set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_EXTOUT].offset, 1, 0);
	set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_MUSICOUT].offset, 0, 0);
	set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_MUSICOUT].offset, 1, 0);

	if (is_incommunication(preset_idx) != 0) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			add_path_info(path_info, preset_path_info);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
		} else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
			add_path_info(path_info, preset_path_info);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
			mask_DacRef(path_info, mixer_ctl_info->output_path);
		} else {
			add_path_info(path_info, preset_path_info);

			if (mixer_ctl_info->incall_mic
				== MC_ASOC_INCALL_MIC_MAINMIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
				path_info->asVboxMixIn[1].dSrcOnOff
					= 0x00AAAAAA;
			} else if (mixer_ctl_info->incall_mic
				== MC_ASOC_INCALL_MIC_SUBMIC) {
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
				path_info->asVboxMixIn[1].dSrcOnOff
					= 0x00AAAAAA;
			} else {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON, 0);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON, 0);
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						0, MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						0, MCDRV_D2SRC_PDM1_R_ON);
			}
			mask_DacRef(path_info, mixer_ctl_info->output_path);

			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
		}
		return;
	}

	if ((preset_idx == 12)
	|| (preset_idx == 13)
	|| (preset_idx == 14)
	|| (preset_idx == 15)
	|| (preset_idx == 16)
	|| (preset_idx == 17)
	|| (preset_idx == 50)
	|| (preset_idx == 51)
	|| (preset_idx == 52)
	|| (preset_idx == 53)
	|| (preset_idx == 54)
	|| (preset_idx == 55)
	|| (preset_idx == 65)
	|| (preset_idx == 66)
	|| (preset_idx == 67)
	|| (preset_idx == 68)
	|| (preset_idx == 69)
	|| (preset_idx == 70)
	|| (preset_idx == 77)
	|| (preset_idx == 78)
	|| (preset_idx == 79)
	|| (preset_idx == 80)
	|| (preset_idx == 81)
	|| (preset_idx == 82)
	|| (preset_idx == 83)
	) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			add_path_info(path_info, preset_path_info);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
		} else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
			add_path_info(path_info, preset_path_info);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
			mask_DacRef(path_info, mixer_ctl_info->output_path);
		} else {
			add_path_info(path_info, preset_path_info);
				if (mixer_ctl_info->incall_mic
					== MC_ASOC_INCALL_MIC_MAINMIC) {
					if (mc_asoc_main_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_main_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							MCDRV_D2SRC_PDM1_R_ON);
					path_info->asVboxMixIn[1].dSrcOnOff
						= 0x00AAAAAA;
				} else if (mixer_ctl_info->incall_mic
					== MC_ASOC_INCALL_MIC_SUBMIC) {
					if (mc_asoc_sub_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_sub_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							MCDRV_D2SRC_PDM1_R_ON);
					path_info->asVboxMixIn[1].dSrcOnOff
						= 0x00AAAAAA;
				} else {
					if (mc_asoc_main_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							0);
					else if (mc_asoc_main_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							0);
					if (mc_asoc_sub_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							0,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_sub_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							0,
							MCDRV_D2SRC_PDM1_R_ON);
			}

			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			mask_DacRef(path_info, mixer_ctl_info->output_path);
		}
		return;
	}
	if ((preset_idx == 18)
	|| (preset_idx == 19)
	|| (preset_idx == 20)
	|| (preset_idx == 21)
	|| (preset_idx == 22)
	|| (preset_idx == 23)
	|| (preset_idx == 56)
	|| (preset_idx == 57)
	|| (preset_idx == 58)
	|| (preset_idx == 59)
	|| (preset_idx == 60)
	|| (preset_idx == 61)
	|| (preset_idx == 71)
	|| (preset_idx == 72)
	|| (preset_idx == 73)
	|| (preset_idx == 74)
	|| (preset_idx == 75)
	|| (preset_idx == 76)
	|| (preset_idx == 83)
	|| (preset_idx == 84)
	|| (preset_idx == 85)
	|| (preset_idx == 86)
	|| (preset_idx == 87)
	|| (preset_idx == 88)
	) {
		if (mixer_ctl_info->output_path == MC_ASOC_OUTPUT_PATH_BT) {
			add_path_info(path_info, preset_path_info);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
		} else if ((mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_SP_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO1_BT)
			|| (mixer_ctl_info->output_path
				== MC_ASOC_OUTPUT_PATH_LO2_BT)) {
			add_path_info(path_info, preset_path_info);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			path_info->asVboxMixIn[1].dSrcOnOff	= 0x00AAAAAA;
			mask_DacRef(path_info, mixer_ctl_info->output_path);
		} else {
			add_path_info(path_info, preset_path_info);
				if (mixer_ctl_info->incall_mic
					== MC_ASOC_INCALL_MIC_MAINMIC) {
					if (mc_asoc_main_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_main_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							MCDRV_D2SRC_PDM1_R_ON);
					path_info->asVboxMixIn[1].dSrcOnOff
						= 0x00AAAAAA;
				} else if (mixer_ctl_info->incall_mic
					== MC_ASOC_INCALL_MIC_SUBMIC) {
					if (mc_asoc_sub_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_sub_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							MCDRV_D2SRC_PDM1_R_ON);
					path_info->asVboxMixIn[1].dSrcOnOff
						= 0x00AAAAAA;
				} else {
					if (mc_asoc_main_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM0_L_ON,
							0);
					else if (mc_asoc_main_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							MCDRV_D2SRC_PDM1_L_ON,
							0);
					if (mc_asoc_sub_mic == MIC_PDM0)
						exchange_ADCtoPDM(path_info,
							0,
							MCDRV_D2SRC_PDM0_R_ON);
					else if (mc_asoc_sub_mic == MIC_PDM1)
						exchange_ADCtoPDM(path_info,
							0,
							MCDRV_D2SRC_PDM1_R_ON);
			}

			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
			mask_DacRef(path_info, mixer_ctl_info->output_path);
		}
		return;
	}

	if (mixer_ctl_info->btmic_play == 1) {
		idx	= BtPathMapping[preset_idx];
		if (BtPathMapping[preset_idx] < ARRAY_SIZE(BtInputPath)) {
			add_path_info(path_info,
				(struct MCDRV_PATH_INFO *)&BtInputPath[idx]);
			mask_BTOut_src(path_info, mixer_ctl_info->output_path);
		} else
			dbg_info("\n********\nBtPathMapping err\n********\n");
	}

	if ((preset_idx == 1)
	|| (preset_idx == 2)
	|| (preset_idx == 3)
	|| (preset_idx == 27)
	|| (preset_idx == 89)
	|| (preset_idx == 90)
	|| (preset_idx == 91)
	|| (preset_idx == 92)) {
		if (ain_play == 1)
			set_ain_play_path(path_info, mixer_ctl_info,
				preset_idx, 1);
		add_path_info(path_info, preset_path_info);
	} else if ((preset_idx == 4)
	|| (preset_idx == 5)
	|| (preset_idx == 28)
	|| (preset_idx == 30)
	|| (preset_idx == 31)) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (ain_play == 1) {
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					set_ain_play_path(path_info,
						mixer_ctl_info, preset_idx, 1);
				else
					set_ain_play_path(path_info,
						mixer_ctl_info, preset_idx, 0);
			}
			add_path_info(path_info, preset_path_info);
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_MAINMIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_SUBMIC) {
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_2MIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						0);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						0);
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						0,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						0,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS) {
				if (mc_asoc_hs_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_hs_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			}
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			if (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_2MIC)
				path_info->asVboxMixIn[1].dSrcOnOff
					= 0x00AAAAAA;
		} else {
			add_path_info(path_info, preset_path_info);
			mute_DIT	= 1;
		}
	} else if ((preset_idx == 6)
	|| (preset_idx == 7)
	|| (preset_idx == 8)
	|| (preset_idx == 9)
	|| (preset_idx == 10)
	|| (preset_idx == 11)
	|| (preset_idx == 29)
	|| (preset_idx == 32)
	|| (preset_idx == 33)
	|| (preset_idx == 34)
	|| (preset_idx == 35)
	|| (preset_idx == 36)
	|| (preset_idx == 37)) {
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			if (ain_play == 1) {
				if (mixer_ctl_info->input_path
					== MC_ASOC_INPUT_PATH_BT)
					set_ain_play_path(path_info,
						mixer_ctl_info, preset_idx, 1);
				else
					set_ain_play_path(path_info,
						mixer_ctl_info, preset_idx, 0);
			}
			add_path_info(path_info, preset_path_info);
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_MAINMIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_SUBMIC) {
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_2MIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						0);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						0);
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
						0,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
						0,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS) {
				if (mc_asoc_hs_mic == MIC_PDM0)
					exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_hs_mic == MIC_PDM1)
					exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					MCDRV_D2SRC_PDM1_R_ON);
			}
			mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
			if (mixer_ctl_info->input_path
				!= MC_ASOC_INPUT_PATH_2MIC)
				path_info->asVboxMixIn[1].dSrcOnOff
					= 0x00AAAAAA;
		} else {
			add_path_info(path_info, preset_path_info);
			mute_DIT	= 1;
		}
	} else if ((preset_idx == 38)
	|| (preset_idx == 39)
	|| (preset_idx == 40)
	|| (preset_idx == 41)
	|| (preset_idx == 42)
	|| (preset_idx == 43)
	|| (preset_idx == 44)
	|| (preset_idx == 45)) {
		if (ain_play == 1)
			set_ain_play_path(path_info,
				mixer_ctl_info, preset_idx, 1);
		if ((mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICECALL)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEUPLINK)
		&& (mixer_ctl_info->input_path
			!= MC_ASOC_INPUT_PATH_VOICEDOWNLINK)) {
			add_path_info(path_info, preset_path_info);
			if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_MAINMIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_SUBMIC) {
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON,
						MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON,
						MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_2MIC) {
				if (mc_asoc_main_mic == MIC_PDM0)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM0_L_ON, 0);
				if (mc_asoc_main_mic == MIC_PDM1)
					exchange_ADC1toPDM(path_info,
						MCDRV_D2SRC_PDM1_L_ON, 0);
				if (mc_asoc_sub_mic == MIC_PDM0)
					exchange_ADC1toPDM(path_info,
						0, MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_sub_mic == MIC_PDM1)
					exchange_ADC1toPDM(path_info,
						0, MCDRV_D2SRC_PDM1_R_ON);
			} else if (mixer_ctl_info->input_path
				== MC_ASOC_INPUT_PATH_HS) {
				if (mc_asoc_hs_mic == MIC_PDM0)
					exchange_ADC1toPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					MCDRV_D2SRC_PDM0_R_ON);
				else if (mc_asoc_hs_mic == MIC_PDM1)
					exchange_ADC1toPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					MCDRV_D2SRC_PDM1_R_ON);
			}
			mask_ADC1_src(path_info, mixer_ctl_info, preset_idx);
		} else {
			add_path_info(path_info, preset_path_info);
			mute_DIT	= 1;
		}
	} else if ((preset_idx == 46)
		|| (preset_idx == 47)
		|| (preset_idx == 48)
		|| (preset_idx == 49)) {
		add_path_info(path_info, preset_path_info);
		if (mixer_ctl_info->input_path
			== MC_ASOC_INPUT_PATH_MAINMIC) {
			if (mc_asoc_main_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_main_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					MCDRV_D2SRC_PDM1_R_ON);
		} else if (mixer_ctl_info->input_path
			== MC_ASOC_INPUT_PATH_SUBMIC) {
			if (mc_asoc_sub_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_sub_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					MCDRV_D2SRC_PDM1_R_ON);
		} else if (mixer_ctl_info->input_path
			== MC_ASOC_INPUT_PATH_2MIC) {
			if (mc_asoc_main_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					0);
			else if (mc_asoc_main_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					0);
			if (mc_asoc_sub_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
					0,
					MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_sub_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
					0,
					MCDRV_D2SRC_PDM1_R_ON);
		} else if (mixer_ctl_info->input_path
			== MC_ASOC_INPUT_PATH_HS) {
			if (mc_asoc_hs_mic == MIC_PDM0)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM0_L_ON,
					MCDRV_D2SRC_PDM0_R_ON);
			else if (mc_asoc_hs_mic == MIC_PDM1)
				exchange_ADCtoPDM(path_info,
					MCDRV_D2SRC_PDM1_L_ON,
					MCDRV_D2SRC_PDM1_R_ON);
		}
		mask_ADC_src(path_info, mixer_ctl_info, preset_idx);
	} else if (ain_play == 1)
		set_ain_play_path(path_info, mixer_ctl_info, preset_idx, 1);

	mask_AnaOut_src(path_info, mixer_ctl_info, preset_idx);
	if ((preset_idx < 4)
	|| CAPTURE_PORT != CAPTURE_PORT_EXT)
		mask_BTOut_src(path_info, mixer_ctl_info->output_path);

	if (CAPTURE_PORT == CAPTURE_PORT_EXT) {
		if (preset_idx >= 4) {
			path_info->asExtOut[0].dSrcOnOff
				= path_info->asMusicOut[0].dSrcOnOff;
			path_info->asMusicOut[0].dSrcOnOff	= 0x00AAAAAA;
			path_info->asExtOut[1].dSrcOnOff
				= path_info->asMusicOut[1].dSrcOnOff;
			path_info->asMusicOut[1].dSrcOnOff	= 0x00AAAAAA;
			if (mute_DIT != 0) {
				set_vol_mute_flg(
				vreg_map[MC_ASOC_DVOL_EXTOUT].offset,
				0, 1);
				set_vol_mute_flg(
				vreg_map[MC_ASOC_DVOL_EXTOUT].offset,
				1, 1);
			}
		}
	} else if (mute_DIT != 0) {
		set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_MUSICOUT].offset,
			0, 1);
		set_vol_mute_flg(vreg_map[MC_ASOC_DVOL_MUSICOUT].offset,
			1, 1);
	}

	return;
}

static void set_adif_src(
	UINT8	bSrc,
	UINT32	*dSrcOnOff
)
{
	switch (bSrc) {
	default:
	case	0:
		break;
	case	1:
		/*	ADC0L	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_ADC0_L_ON;
		break;
	case	2:
		/*	ADC0R	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_ADC0_R_ON;
		break;
	case	3:
		/*	ADC1	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_ADC1_ON;
		break;
	case	4:
		/*	PDM0L	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_PDM0_L_ON;
		break;
	case	5:
		/*	PDM0R	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_PDM0_R_ON;
		break;
	case	6:
		/*	PDM1L	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_PDM1_L_ON;
		break;
	case	7:
		/*	PDM1R	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_PDM1_R_ON;
		break;
	case	8:
		/*	DAC0REF	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_DAC0REF_ON;
		break;
	case	9:
		/*	DAC1REF	*/
		*dSrcOnOff	= 0x00AAAAAA | MCDRV_D2SRC_DAC1REF_ON;
		break;
	}
}

static int connect_path(
	struct snd_soc_codec *codec
)
{
	int	err;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
	struct MCDRV_PATH_INFO	path_info;
	int	preset_idx	= 0;
	int	cache;

	/* TRACE_FUNC(); */

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON) {
		dbg_info("hold=on\n");
		return 0;
	}

	if (get_mixer_path_ctl_info(codec, &mixer_ctl_info) < 0)
		return -EIO;

	preset_idx	= get_path_preset_idx(&mixer_ctl_info);
	dbg_info("preset_idx=%d\n", preset_idx);
	if ((preset_idx < 0) || (preset_idx > PRESET_PATH_N))
		return -EIO;

	memcpy(&path_info,
		&stPresetPathInfo[0],
		sizeof(struct MCDRV_PATH_INFO));
	get_path_info(&path_info, &mixer_ctl_info, preset_idx);
	set_BIAS(&path_info);

	cache	= read_cache(codec, MC_ASOC_ADIF0_SOURCE);
	if (cache < 0)
		return -EIO;
	if (((UINT8)cache != 0)
	&& ((UINT8)(cache>>8) != 0)) {
		set_adif_src((UINT8)cache, &path_info.asAdif0[0].dSrcOnOff);
		set_adif_src((UINT8)(cache>>8),
					&path_info.asAdif0[1].dSrcOnOff);
	}
	cache	= read_cache(codec, MC_ASOC_ADIF1_SOURCE);
	if (cache < 0)
		return -EIO;
	if (((UINT8)cache != 0)
	&& ((UINT8)(cache>>8) != 0)) {
		set_adif_src((UINT8)cache, &path_info.asAdif1[0].dSrcOnOff);
		set_adif_src((UINT8)(cache>>8),
					&path_info.asAdif1[1].dSrcOnOff);
	}
	cache	= read_cache(codec, MC_ASOC_ADIF2_SOURCE);
	if (cache < 0)
		return -EIO;
	if (((UINT8)cache != 0)
	&& ((UINT8)(cache>>8) != 0)) {
		set_adif_src((UINT8)cache, &path_info.asAdif2[0].dSrcOnOff);
		set_adif_src((UINT8)(cache>>8),
					&path_info.asAdif2[1].dSrcOnOff);
	}

	err	= set_volume(codec, &mixer_ctl_info, preset_idx);
	if (err < 0)
		return err;
	err	= _McDrv_Ctrl(MCDRV_SET_PATH, &path_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	return err;
}

/*
 * DAI (PCM interface)
 */
static int is_dio_modified(
	const struct MCDRV_DIO_PORT	*port,
	int	id,
	int	mode,
	UINT32	update
)
{
	int	err;
	struct MCDRV_DIO_INFO	cur_dio;

	err	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &cur_dio, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	if (((update & MCDRV_MUSIC_COM_UPDATE_FLAG) != 0)
	|| ((update & MCDRV_EXT_COM_UPDATE_FLAG) != 0)
	|| ((update & MCDRV_HIFI_COM_UPDATE_FLAG) != 0)) {
		if ((cur_dio.asPortInfo[id].sDioCommon.bMasterSlave
			!= port->sDioCommon.bMasterSlave)
		|| (cur_dio.asPortInfo[id].sDioCommon.bAutoFs
			!= port->sDioCommon.bAutoFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bFs
			!= port->sDioCommon.bFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bBckFs
			!= port->sDioCommon.bBckFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bInterface
			!= port->sDioCommon.bInterface)
		|| (cur_dio.asPortInfo[id].sDioCommon.bBckInvert
			!= port->sDioCommon.bBckInvert)
		|| (cur_dio.asPortInfo[id].sDioCommon.bSrcThru
			!= port->sDioCommon.bSrcThru))
			return 1;
		if (mode == MCDRV_DIO_PCM) {
			if ((cur_dio.asPortInfo[id].sDioCommon.bPcmHizTim
				!= port->sDioCommon.bPcmHizTim)
			|| (cur_dio.asPortInfo[id].sDioCommon.bPcmFrame
				!= port->sDioCommon.bPcmFrame)
			|| (cur_dio.asPortInfo[id].sDioCommon.bPcmHighPeriod
				!= port->sDioCommon.bPcmHighPeriod))
				return 1;
		}
	}

	if (((update & MCDRV_MUSIC_DIR_UPDATE_FLAG) != 0)
	|| ((update & MCDRV_HIFI_DIR_UPDATE_FLAG) != 0)) {
		if (mode == MCDRV_DIO_DA) {
			if ((cur_dio.asPortInfo[id].sDir.sDaFormat.bBitSel
				!= port->sDir.sDaFormat.bBitSel)
			|| (cur_dio.asPortInfo[id].sDir.sDaFormat.bMode
				!= port->sDir.sDaFormat.bMode))
				return 1;
		} else {
			if ((cur_dio.asPortInfo[id].sDir.sPcmFormat.bMono
				!= port->sDir.sPcmFormat.bMono)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bOrder
				!= port->sDir.sPcmFormat.bOrder)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bLaw
				!= port->sDir.sPcmFormat.bLaw)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bBitSel
				!= port->sDir.sPcmFormat.bBitSel))
				return 1;
		}
	}

	if (((update & MCDRV_MUSIC_DIT_UPDATE_FLAG) != 0)
	|| ((update & MCDRV_EXT_DIT_UPDATE_FLAG) != 0)
	|| ((update & MCDRV_HIFI_DIT_UPDATE_FLAG) != 0)) {
		if (mode == MCDRV_DIO_DA) {
			if ((cur_dio.asPortInfo[id].sDit.sDaFormat.bBitSel
				!= port->sDit.sDaFormat.bBitSel)
			|| (cur_dio.asPortInfo[id].sDit.sDaFormat.bMode
				!= port->sDit.sDaFormat.bMode))
				return 1;
		} else {
			if ((cur_dio.asPortInfo[id].sDit.sPcmFormat.bMono
				!= port->sDit.sPcmFormat.bMono)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bOrder
				!= port->sDit.sPcmFormat.bOrder)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bLaw
				!= port->sDit.sPcmFormat.bLaw)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bBitSel
				!= port->sDit.sPcmFormat.bBitSel))
				return 1;
		}
	}
	return 0;
}

static int setup_dai(
	struct snd_soc_codec	*codec,
	struct mc_asoc_data	*mc_asoc,
	int	id,
	int	mode,
	int	dir
)
{
	struct MCDRV_DIO_INFO	dio;
	struct MCDRV_DIO_PORT	*port	= &dio.asPortInfo[id];
	struct mc_asoc_port_params	*port_prm	= &mc_asoc->port;
	UINT32	update	= 0;
	int	bCh, err, modify;
	struct MCDRV_PATH_INFO	path_info, tmp_path_info;

	err	= _McDrv_Ctrl(MCDRV_GET_PATH, &path_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	memset(&dio, 0, sizeof(struct MCDRV_DIO_INFO));

	if (port_prm->stream == 0) {
		port->sDioCommon.bMasterSlave	= port_prm->master;
		port->sDioCommon.bAutoFs	= MCDRV_AUTOFS_OFF;
		port->sDioCommon.bFs		= port_prm->rate;
		port->sDioCommon.bBckFs		= port_prm->bckfs;
		port->sDioCommon.bInterface	= mode;
		port->sDioCommon.bBckInvert	= port_prm->inv;
		port->sDioCommon.bSrcThru	= port_prm->srcthru;
		if (mode == MCDRV_DIO_PCM)
			port->sDioCommon.bPcmFrame
				= port_prm->format;
		if (id == 0)
			update |= MCDRV_MUSIC_COM_UPDATE_FLAG;
		else if (id == 1)
			update |= MCDRV_EXT_COM_UPDATE_FLAG;
		else if (id == 3)
			update |= MCDRV_HIFI_COM_UPDATE_FLAG;
	}

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mode == MCDRV_DIO_DA) {
			port->sDir.sDaFormat.bBitSel	= port_prm->bits[dir];
			port->sDir.sDaFormat.bMode	= port_prm->format;
		}  else {
			port->sDir.sPcmFormat.bMono
				= port_prm->pcm_mono[dir];
			port->sDir.sPcmFormat.bOrder
				= port_prm->pcm_order[dir];
			port->sDir.sPcmFormat.bLaw
				= port_prm->pcm_law[dir];
			port->sDir.sPcmFormat.bBitSel
				= port_prm->bits[dir];
		}
		if (id == 0)
			update |= MCDRV_MUSIC_DIR_UPDATE_FLAG;
		else if (id == 3)
			update |= MCDRV_HIFI_DIR_UPDATE_FLAG;
	}

	if (dir == SNDRV_PCM_STREAM_CAPTURE) {
		if (mode == MCDRV_DIO_DA) {
			port->sDit.sDaFormat.bBitSel	= port_prm->bits[dir];
			port->sDit.sDaFormat.bMode	= port_prm->format;
		}  else {
			port->sDit.sPcmFormat.bMono
				= port_prm->pcm_mono[dir];
			port->sDit.sPcmFormat.bOrder
				= port_prm->pcm_order[dir];
			port->sDit.sPcmFormat.bLaw
				= port_prm->pcm_law[dir];
			port->sDit.sPcmFormat.bBitSel
				= port_prm->bits[dir];
		}
		if (id == 0)
			update |= MCDRV_MUSIC_DIT_UPDATE_FLAG;
		else if (id == 1)
			update |= MCDRV_EXT_DIT_UPDATE_FLAG;
		else if (id == 3)
			update |= MCDRV_HIFI_DIT_UPDATE_FLAG;
	}

	modify	= is_dio_modified(port, id, mode, update);
	if (modify < 0)
		return -EIO;
	if (modify == 0) {
		dbg_info("modify == 0\n");
		return 0;
	}

	memcpy(&tmp_path_info, &path_info, sizeof(struct MCDRV_PATH_INFO));
	if ((dir == SNDRV_PCM_STREAM_PLAYBACK)
	|| (port_prm->stream == 0)) {
		if (id == 0) {
			for (bCh = 0; bCh < MUSICOUT_PATH_CHANNELS; bCh++) {
				tmp_path_info.asMusicOut[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asMusicOut[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
			for (bCh = 0; bCh < EXTOUT_PATH_CHANNELS; bCh++) {
				tmp_path_info.asExtOut[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asExtOut[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
			for (bCh = 0; bCh < VBOXMIXIN_PATH_CHANNELS; bCh++) {
				tmp_path_info.asVboxMixIn[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asVboxMixIn[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
			for (bCh = 0; bCh < AE_PATH_CHANNELS; bCh++) {
				tmp_path_info.asAe0[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asAe0[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
				tmp_path_info.asAe1[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asAe1[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
				tmp_path_info.asAe2[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asAe2[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
				tmp_path_info.asAe3[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asAe3[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
			for (bCh = 0; bCh < DAC0_PATH_CHANNELS; bCh++) {
				tmp_path_info.asDac0[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asDac0[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
			for (bCh = 0; bCh < DAC1_PATH_CHANNELS; bCh++) {
				tmp_path_info.asDac1[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_MUSICIN_ON;
				tmp_path_info.asDac1[bCh].dSrcOnOff
					|= MCDRV_D1SRC_MUSICIN_OFF;
			}
		} else if (id == 3) {
			for (bCh = 0; bCh < DAC0_PATH_CHANNELS; bCh++) {
				tmp_path_info.asDac0[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_HIFIIN_ON;
				tmp_path_info.asDac0[bCh].dSrcOnOff
					|= MCDRV_D1SRC_HIFIIN_OFF;
			}
			for (bCh = 0; bCh < DAC1_PATH_CHANNELS; bCh++) {
				tmp_path_info.asDac1[bCh].dSrcOnOff
					&= ~MCDRV_D1SRC_HIFIIN_ON;
				tmp_path_info.asDac1[bCh].dSrcOnOff
					|= MCDRV_D1SRC_HIFIIN_OFF;
			}
		}
	}
	if ((dir == SNDRV_PCM_STREAM_CAPTURE)
	|| (port_prm->stream == 0)) {
		if (id == 0)
			for (bCh = 0; bCh < MUSICOUT_PATH_CHANNELS; bCh++)
				tmp_path_info.asMusicOut[bCh].dSrcOnOff
					= 0x00AAAAAA;
		else if (id == 1)
			for (bCh = 0; bCh < EXTOUT_PATH_CHANNELS; bCh++)
				tmp_path_info.asExtOut[bCh].dSrcOnOff
					= 0x00AAAAAA;
		else if (id == 3)
			for (bCh = 0; bCh < HIFIOUT_PATH_CHANNELS; bCh++)
				tmp_path_info.asHifiOut[bCh].dSrcOnOff
					= 0x00AAAAAA;
	}

	if (memcmp(&tmp_path_info, &path_info, sizeof(struct MCDRV_PATH_INFO))
		== 0)
		modify	= 0;
	else {
		err	= _McDrv_Ctrl(MCDRV_SET_PATH, &tmp_path_info, NULL, 0);
		if (err != MCDRV_SUCCESS)
			return map_drv_error(err);
	}

	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &dio, NULL, update);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	if (modify != 0) {
		err	= connect_path(codec);
		return err;
	}
	return 0;
}

static int mc_asoc_set_clkdiv(
	struct snd_soc_dai *dai,
	int div_id,
	int div)
{
	int	err	= 0;
	struct snd_soc_codec	*codec	= NULL;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;

	/*TRACE_FUNC();*/

	if ((dai == NULL)
	|| (dai->codec == NULL)
	|| (get_port_id(dai->id) != 0))
		return -EINVAL;

	codec	= dai->codec;
	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	mc_asoc_lock("mc_asoc_set_clkdiv");

	port	= &mc_asoc->port;

	switch (div_id) {
	case MC_ASOC_BCLK_MULT:
		switch (div) {
		case MC_ASOC_LRCK_X64:
			port->bckfs	= MCDRV_BCKFS_64;
			break;
		case MC_ASOC_LRCK_X48:
			port->bckfs	= MCDRV_BCKFS_48;
			break;
		case MC_ASOC_LRCK_X32:
			port->bckfs	= MCDRV_BCKFS_32;
			break;
		case MC_ASOC_LRCK_X512:
			port->bckfs	= MCDRV_BCKFS_512;
			break;
		case MC_ASOC_LRCK_X256:
			port->bckfs	= MCDRV_BCKFS_256;
			break;
		case MC_ASOC_LRCK_X192:
			port->bckfs	= MCDRV_BCKFS_192;
			break;
		case MC_ASOC_LRCK_X128:
			port->bckfs	= MCDRV_BCKFS_128;
			break;
		case MC_ASOC_LRCK_X96:
			port->bckfs	= MCDRV_BCKFS_96;
			break;
		case MC_ASOC_LRCK_X24:
			port->bckfs	= MCDRV_BCKFS_24;
			break;
		case MC_ASOC_LRCK_X16:
			port->bckfs	= MCDRV_BCKFS_16;
			break;
		default:
			err	= -EINVAL;
		}
		break;

	default:
		break;
	}

	mc_asoc_unlock("mc_asoc_set_clkdiv");
	return err;
}

static int mc_asoc_set_fmt(
	struct snd_soc_dai *dai,
	unsigned int fmt)
{
	int	err	= 0;
	struct snd_soc_codec	*codec	= NULL;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;

	/*TRACE_FUNC();*/

	if ((dai == NULL)
	|| (dai->codec == NULL)
	|| (get_port_id(dai->id) != 0))
		return -EINVAL;

	codec	= dai->codec;
	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	mc_asoc_lock("mc_asoc_set_fmt");

	port	= &mc_asoc->port;

	/* format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		port->format	= MCDRV_DAMODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		port->format	= MCDRV_DAMODE_TAILALIGN;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		port->format	= MCDRV_DAMODE_HEADALIGN;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		port->format	= MCDRV_PCM_SHORTFRAME;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		port->format	= MCDRV_PCM_LONGFRAME;
		break;
	default:
		err	= -EINVAL;
		goto exit;
		break;
	}

	/* master */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		port->master	= MCDRV_DIO_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		port->master	= MCDRV_DIO_SLAVE;
		break;
	default:
		err	= -EINVAL;
		goto exit;
		break;
	}

	/* inv */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		port->inv	= MCDRV_BCLK_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		port->inv	= MCDRV_BCLK_INVERT;
		break;
	default:
		err	= -EINVAL;
		break;
	}

exit:
	mc_asoc_unlock("mc_asoc_set_fmt");
	return err;
}

#ifdef DELAY_CONNECT_XXX
#define	DELAY_CFG_SLIM_SCH	(200)

static struct workqueue_struct	*workq_cfg_slim_sch;
static struct delayed_work	delayed_work_cfg_slim_sch;
static void work_cfg_slim_sch(struct work_struct *work)
{
	int	err;
	struct MCDRV_PATH_INFO	path_info;
	struct MCDRV_DIO_INFO	sDioInfo;
	struct MCDRV_DIOPATH_INFO	sDioPathInfo;
	int	port;
	int	rate[]	= {48000, 44100, 32000, -1, 24000, 22050, 16000, -1,
				12000, 11025, 8000, -1, 192000, 96000};

	TRACE_FUNC();

	err	= _McDrv_Ctrl(MCDRV_GET_PATH, &path_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		pr_info("get path info failed:%d\n", err);
		return;
	}

	_McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sDioInfo, NULL, 0);
	_McDrv_Ctrl(MCDRV_GET_DIGITALIO_PATH, &sDioPathInfo, NULL, 0);

	port	= sDioPathInfo.abPhysPort[0];
	if (port >= MCDRV_PHYSPORT_SLIM0) {
		if ((path_info.asMusicOut[0].dSrcOnOff != 0x00AAAAAA)
		|| (path_info.asMusicOut[1].dSrcOnOff != 0x00AAAAAA)) {
			cfg_slim_sch_tx((port - MCDRV_PHYSPORT_SLIM0)*2, 1,
				rate[sDioInfo.asPortInfo[0].sDioCommon.bFs]);
		}
		if (is_d1src_used(&path_info, MCDRV_D1SRC_MUSICIN_ON)) {
			cfg_slim_sch_rx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[0].sDioCommon.bFs]);
		}
	}

	port	= sDioPathInfo.abPhysPort[1];
	if (port >= MCDRV_PHYSPORT_SLIM0) {
		if ((path_info.asExtOut[0].dSrcOnOff != 0x00AAAAAA)
		|| (path_info.asExtOut[1].dSrcOnOff != 0x00AAAAAA)) {
			cfg_slim_sch_tx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[1].sDioCommon.bFs]);
		}
		if (is_d1src_used(&path_info, MCDRV_D1SRC_EXTIN_ON)) {
			cfg_slim_sch_rx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[1].sDioCommon.bFs]);
		}
	}

	port	= sDioPathInfo.abPhysPort[2];
	if (port >= MCDRV_PHYSPORT_SLIM0) {
		if (path_info.asVoiceOut[0].dSrcOnOff != 0x00AAAAAA) {
			cfg_slim_sch_tx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[2].sDioCommon.bFs]);
		}
		if ((path_info.asVboxIoIn[0].dSrcOnOff != 0x00AAAAAA)
		|| (path_info.asVboxHostIn[0].dSrcOnOff != 0x00AAAAAA)) {
			cfg_slim_sch_rx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[2].sDioCommon.bFs]);
		}
	}

	port	= sDioPathInfo.abPhysPort[3];
	if (port >= MCDRV_PHYSPORT_SLIM0) {
		if (path_info.asHifiOut[0].dSrcOnOff != 0x00AAAAAA) {
			cfg_slim_sch_tx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[3].sDioCommon.bFs]);
		}
		if (((path_info.asDac0[0].dSrcOnOff&MCDRV_D1SRC_HIFIIN_ON) != 0)
		|| ((path_info.asDac0[1].dSrcOnOff&MCDRV_D1SRC_HIFIIN_ON) != 0)
		|| ((path_info.asDac1[0].dSrcOnOff&MCDRV_D1SRC_HIFIIN_ON) != 0)
		|| ((path_info.asDac1[1].dSrcOnOff&MCDRV_D1SRC_HIFIIN_ON) != 0)
		) {
			cfg_slim_sch_rx((port - MCDRV_PHYSPORT_SLIM0)*2, 2,
				rate[sDioInfo.asPortInfo[3].sDioCommon.bFs]);
		}
	}
}
#endif

static int mc_asoc_hw_params(
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec	*codec	= NULL;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;
	int	dir	= substream->stream;
	int	rate;
	int	err	= 0;
	int	id;
	struct MCDRV_DIOPATH_INFO	sDioPathInfo;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
	int	preset_idx	= 0;

	/* TRACE_FUNC(); */

	if ((substream == NULL)
	|| (dai == NULL))
		return -EINVAL;

	id	= get_port_id(dai->id);
	if (id != 0) {
		dbg_info("dai->id=%d\n", id);
		return -EINVAL;
	}

	dbg_info("hw_params: [%d] name=%s, dir=%d, rate=%d, bits=%d, ch=%d\n",
		id,
		substream->name,
		dir,
		params_rate(params),
		params_format(params),
		params_channels(params));

	codec	= dai->codec;
	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if ((codec == NULL)
	|| (mc_asoc == NULL)) {
		dbg_info("mc_asoc=NULL\n");
		return -EINVAL;
	}
	port	= &mc_asoc->port;

	/* channels */
	switch (params_channels(params)) {
	case 1:
		port->pcm_mono[dir]	= MCDRV_PCM_MONO;
		port->channels	= MCDRV_MUSIC_2CH;
		break;
	case 2:
		port->channels	= MCDRV_MUSIC_2CH;
		port->pcm_mono[dir]	= MCDRV_PCM_STEREO;
		break;
	case 4:
		port->channels	= MCDRV_MUSIC_4CH;
		port->pcm_mono[dir]	= MCDRV_PCM_STEREO;
		break;
	case 6:
		port->channels	= MCDRV_MUSIC_6CH;
		port->pcm_mono[dir]	= MCDRV_PCM_STEREO;
		break;
	default:
		return -EINVAL;
	}

	/* format (bits) */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		port->bits[dir]	= MCDRV_BITSEL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		port->bits[dir]	= MCDRV_BITSEL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		port->bits[dir]	= MCDRV_BITSEL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		port->bits[dir]	= MCDRV_BITSEL_32;
		break;
	default:
		return -EINVAL;
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		port->pcm_order[dir]
			= stMusicPort_Default.sDir.sPcmFormat.bOrder;
		port->pcm_law[dir]
			= stMusicPort_Default.sDir.sPcmFormat.bLaw;
	} else {
		port->pcm_order[dir]
			= stMusicPort_Default.sDit.sPcmFormat.bOrder;
		port->pcm_law[dir]
			= stMusicPort_Default.sDit.sPcmFormat.bLaw;
	}

	/* rate */
	switch (params_rate(params)) {
	case 8000:
		rate	= MCDRV_FS_8000;
		break;
	case 11025:
		rate	= MCDRV_FS_11025;
		break;
	case 16000:
		rate	= MCDRV_FS_16000;
		break;
	case 22050:
		rate	= MCDRV_FS_22050;
		break;
	case 32000:
		rate	= MCDRV_FS_32000;
		break;
	case 44100:
		rate	= MCDRV_FS_44100;
		break;
	case 48000:
		rate	= MCDRV_FS_48000;
		break;
	case 96000:
		rate	= MCDRV_FS_96000;
		break;
	case 192000:
		rate	= MCDRV_FS_192000;
		break;
	default:
		return -EINVAL;
	}

	mc_asoc_lock("mc_asoc_hw_params");

	if (CAPTURE_PORT == CAPTURE_PORT_MUSIC)
		if ((port->stream & ~(1 << dir)) && (rate != port->rate)) {
			err	= -EBUSY;
			goto error;
		}

	port->rate	= rate;

	if (get_mixer_path_ctl_info(codec, &mixer_ctl_info) < 0) {
		err	= -EIO;
		goto error;
	}
	preset_idx	= get_path_preset_idx(&mixer_ctl_info);
	if ((rate == MCDRV_FS_96000)
	|| (rate == MCDRV_FS_192000)) {
		if ((is_incall(preset_idx) != 0)
		|| (is_incommunication(preset_idx) != 0)) {
			err	= -EINVAL;
			goto error;
		}
	}

	if ((rate == MCDRV_FS_96000)
	|| (rate == MCDRV_FS_192000)) {
		id	= 3;
	} else {
		sDioPathInfo.bMusicCh	= port->channels;
		err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO_PATH, &sDioPathInfo,
						NULL, MCDRV_MUSICNUM_UPDATE_FLAG);
		if (err != MCDRV_SUCCESS) {
			dev_err(codec->dev,
				"%d: Error in MCDRV_SET_DIGITALIO_PATH\n",
				err);
			goto error;
		}

		if ((dir == SNDRV_PCM_STREAM_CAPTURE)
		&& (CAPTURE_PORT == CAPTURE_PORT_EXT))
			id	= 1;
	}

	err	= setup_dai(codec, mc_asoc, id, MCDRV_DIO_DA, dir);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in setup_dai\n", err);
		err	= -EIO;
		goto error;
	}

	mc_asoc_port_rate	= rate;
	port->stream |= (1 << dir);

	if (preset_idx != get_path_preset_idx(&mixer_ctl_info)) {
		err	= connect_path(codec);
		if (err < 0) {
			;
			goto error;
		}
	}

error:
	mc_asoc_unlock("mc_asoc_hw_params");

#ifdef DELAY_CONNECT_XXX
	dbg_info("queue_delayed_work_cfg_slim_sch\n");
	queue_delayed_work(workq_cfg_slim_sch, &delayed_work_cfg_slim_sch,
					msecs_to_jiffies(DELAY_CFG_SLIM_SCH));
#endif

	return err;
}

static int mc_asoc_hw_free(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec	*codec	= NULL;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;
	int	dir	= substream->stream;
	int	err	= 0;

	/* TRACE_FUNC(); */

	if ((substream == NULL)
	|| (dai == NULL))
		return -EINVAL;

	codec	= dai->codec;
	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if ((codec == NULL)
	|| (mc_asoc == NULL)
	|| (get_port_id(dai->id) != 0))
		return -EINVAL;

	port	= &mc_asoc->port;

	mc_asoc_lock("mc_asoc_hw_free");

	if (!(port->stream & (1 << dir))) {
		err	= 0;
		goto error;
	}

	port->stream &= ~(1 << dir);

error:
	mc_asoc_unlock("mc_asoc_hw_free");

#ifdef DELAY_CONNECT_XXX
	cancel_delayed_work(&delayed_work_cfg_slim_sch);
	dbg_info("cancel_delayed_work_cfg_slim_sch\n");
#endif

	return err;
}

static struct snd_soc_dai_ops	mc_asoc_dai_ops[]	= {
	{
		.set_clkdiv	= mc_asoc_set_clkdiv,
		.set_fmt	= mc_asoc_set_fmt,
		.hw_params	= mc_asoc_hw_params,
		.hw_free	= mc_asoc_hw_free,
	},
};

struct snd_soc_dai_driver	mc_asoc_dai[]	= {
	{
		.name	= MC_ASOC_NAME "-da0",
		.id	= 1,
		.playback	= {
			.stream_name	= "Playback",
			.channels_min	= 1,
			.channels_max	= 6,
			.rates		= MC_ASOC_RATE,
			.formats	= MC_ASOC_FORMATS,
		},
		.capture	= {
			.stream_name	= "Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= MC_ASOC_RATE,
			.formats	= MC_ASOC_FORMATS,
		},
		.ops	= &mc_asoc_dai_ops[0]
	},
};

/*
 * Control interface
 */
/*
 * Virtual register
 *
 * 16bit software registers are implemented for volumes and mute
 * switches (as an exception, no mute switches for MIC and HP gain).
 * Register contents are stored in codec's register cache.
 *
 *	15	14			8	7			0
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *	|swR|	volume-R		|swL|	volume-L		|
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 */

static int write_reg_vol(
	struct snd_soc_codec *codec,
	  unsigned int reg,
	  unsigned int value)
{
	struct MCDRV_VOL_INFO	vol_info;
	int	err;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
	int	preset_idx	= 0;

	err	= write_cache(codec, reg, value);
	if (err != 0)
		dev_err(codec->dev, "Cache write to %x failed: %d\n", reg,
			err);

	if (get_mixer_path_ctl_info(codec, &mixer_ctl_info) < 0)
		return -EIO;

	preset_idx	= get_path_preset_idx(&mixer_ctl_info);
	if ((preset_idx < 0) || (preset_idx > PRESET_PATH_N))
		return -EIO;

	memset(&vol_info, 0, sizeof(struct MCDRV_VOL_INFO));
	switch (reg) {
	case	MC_ASOC_AVOL_SP_GAIN:
		if (mc_asoc_ver_id == 0) {
			vreg_map[MC_ASOC_AVOL_SP].volmap
				= volmap_sp[value];
			reg	= MC_ASOC_AVOL_SP;
		} else {
			return 0;
		}
		break;
	case	MC_ASOC_DVOL_MASTER:
		if (mc_asoc_audio_play_port == LIN1)
			reg	= MC_ASOC_DVOL_ADIF1IN;
		else
			reg	= MC_ASOC_DVOL_MUSICIN;
		break;
	case	MC_ASOC_DVOL_VOICE:
		if (is_incall(preset_idx) != 0) {
			reg	= MC_ASOC_DVOL_VOICEIN;
			if ((mc_asoc_voice_port == LIN1_LOUT1)
			|| (mc_asoc_voice_port == LIN1_LOUT2))
				reg	= MC_ASOC_AVOL_LINEIN1;
			else if ((mc_asoc_voice_port == DIO_EXT)
				&& (is_incall_BT(preset_idx) == 0))
				reg	= MC_ASOC_DVOL_EXTIN;
		} else {
			return 0;
		}
		break;
	case	MC_ASOC_DVOL_APLAY_A:
		reg	= MC_ASOC_AVOL_MIC1;
		err	= set_vol_info(codec, &vol_info, reg, &mixer_ctl_info,
				preset_idx);
		if (err < 0)
			return err;
		reg	= MC_ASOC_AVOL_MIC2;
		err	= set_vol_info(codec, &vol_info, reg, &mixer_ctl_info,
				preset_idx);
		if (err < 0)
			return err;
		reg	= MC_ASOC_AVOL_MIC3;
		err	= set_vol_info(codec, &vol_info, reg, &mixer_ctl_info,
				preset_idx);
		if (err < 0)
			return err;
		reg	= MC_ASOC_AVOL_MIC4;
		err	= set_vol_info(codec, &vol_info, reg, &mixer_ctl_info,
				preset_idx);
		if (err < 0)
			return err;
		reg	= MC_ASOC_AVOL_LINEIN1;
		break;
	case	MC_ASOC_DVOL_APLAY_D:
		reg	= MC_ASOC_DVOL_ADIF1IN;
		break;
	case	MC_ASOC_VOICE_RECORDING:
		if (is_incall(preset_idx) != 0) {
			reg	= MC_ASOC_DVOL_VOICEOUT;
			if (mc_asoc_voice_port == LIN1_LOUT1)
				reg	= MC_ASOC_DVOL_DAC0OUT;
			else if (mc_asoc_voice_port == LIN1_LOUT2)
				reg	= MC_ASOC_DVOL_DAC1OUT;
		} else if (is_incommunication(preset_idx) != 0) {
			reg	= MC_ASOC_DVOL_VOICEOUT;
		} else {
			return 0;
		}
		break;
	default:
		break;
	}

	err	= set_vol_info(codec, &vol_info, reg, &mixer_ctl_info,
			preset_idx);
	if (err < 0)
		return err;
	err	= _McDrv_Ctrl(MCDRV_SET_VOLUME, &vol_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev,
			"%d: Error in MCDRV_SET_VOLUME\n", err);
		return -EIO;
	}
	return 0;
}

static void auto_powerdown(
	struct snd_soc_codec *codec
)
{
#if (AUTO_POWEROFF == AUTO_POWEROFF_ON)
	int	err	= 0;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
	UINT8	bAEC[]	= {
		0x41, 0x45, 0x43,
		0x05,
		0, 0, 0, 60,
		0x00,
		253,
		0,
		0,
		/*	D7:	*/
		0x44, 0x37,
		0, 0, 0, 50,
		/*	AudioEngine:16	*/
		0x02, 0x00, 0x00, 0x00,
		0, 0, 0, 8,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		/*	V-BOX:23	*/
		0x03, 0x00, 0x00, 0x00,
		0, 0, 0, 15,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		/*	E-DSP:11	*/
		0x07, 0x00, 0x00, 0x00,
		0, 0, 0, 3,
		0,
		0,
		0,
	};

	get_mixer_path_ctl_info(codec, &mixer_ctl_info);
	if ((mixer_ctl_info.audio_mode_play == 0)
	&& (mixer_ctl_info.audio_mode_cap == 0)
	&& (mixer_ctl_info.mainmic_play == 0)
	&& (mixer_ctl_info.submic_play == 0)
	&& (mixer_ctl_info.msmic_play == 0)
	&& (mixer_ctl_info.hsmic_play == 0)
	&& (mixer_ctl_info.btmic_play == 0)
	&& (mixer_ctl_info.lin1_play == 0)
	&& (mixer_ctl_info.dtmf_control == 0))
		err	= _McDrv_Ctrl(MCDRV_SET_DSP, bAEC, NULL, sizeof(bAEC));
		if (err != MCDRV_SUCCESS) {
			;
			dbg_info("%d: Error in MCDRV_SET_DSP\n", err);
		}
#endif
}

static int add_dsp_prm(
	struct mc_asoc_data	*mc_asoc,
	int	i,
	int	j,
	UINT8	*param,
	UINT32	dSize
)
{
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;

	dsp_prm	= &mc_asoc->param_store[i][j];
	if (dsp_prm->pabParam == NULL)
		dbg_info("param_store[%d][%d]->pabParam = %8p\n",
						i, j, dsp_prm->pabParam);
	else
		while (dsp_prm->pabParam != NULL) {
			dbg_info("pabParam = %8p\n", dsp_prm->pabParam);
			if (dsp_prm->next == NULL) {
				dsp_prm->next	= kzalloc(
					sizeof(struct mc_asoc_dsp_param),
					GFP_KERNEL);
				if (dsp_prm->next == NULL)
					return -ENOMEM;
				dsp_prm	= dsp_prm->next;
				dbg_info("next = %8p\n", dsp_prm);
				break;
			} else
				dsp_prm	= dsp_prm->next;
		}

	dbg_info("param = %8p\n", param);

	dsp_prm->pabParam	= param;
	dsp_prm->dSize		= dSize;
	return 0;
}

static void del_dsp_prm(
	struct mc_asoc_data	*mc_asoc
)
{
	int	i, j;
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;
	struct mc_asoc_dsp_param	*next_prm	= NULL;

	for (i = 0; i <= DSP_PRM_VC_2MIC; i++) {
		for (j = 0; j <= DSP_PRM_USER; j++) {
			if (mc_asoc->param_store[i][j].pabParam
				!= NULL) {
				dbg_info(
			"free(param_store[%d][%d].pabParam:%8p)\n",
				i, j,
				mc_asoc->param_store[i][j].pabParam);
#ifdef DSP_MEM_STATIC
#else
				vfree(
				mc_asoc->param_store[i][j].pabParam);
#endif
			}
			dsp_prm	= mc_asoc->param_store[i][j].next;
			while (dsp_prm != NULL) {
				dbg_info("free(pabParam:%8p)\n",
					dsp_prm->pabParam);
#ifdef DSP_MEM_STATIC
#else
				vfree(dsp_prm->pabParam);
#endif
				next_prm	= dsp_prm->next;
				dbg_info("free(dsp_prm:%8p)\n",
					dsp_prm);
				kfree(dsp_prm);
				dsp_prm	= next_prm;
			}
			mc_asoc->param_store[i][j].pabParam
				= NULL;
			mc_asoc->param_store[i][j].dSize
				= 0;
			mc_asoc->param_store[i][j].next
				= NULL;
		}
	}
#ifdef DSP_MEM_STATIC
	dsp_mem_pt	= 0;
	dbg_info("dsp_mem_pt:%d\n", dsp_mem_pt);
#endif
}

static int set_audio_mode_play(
	struct snd_soc_codec *codec,
	unsigned int value
)
{
	int	ret;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;

	/* TRACE_FUNC(); */

	dbg_info("audio_mode=%d\n", value);

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	port	= &mc_asoc->port;
	if (value > 1)
		if ((port->stream != 0)
		&& ((port->rate == MCDRV_FS_96000)
			|| (port->rate == MCDRV_FS_192000)))
			return -EINVAL;

	ret	= write_cache(codec, MC_ASOC_AUDIO_MODE_PLAY, value);
	if (ret < 0)
		return ret;

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	if (value == 0)
		del_dsp_prm(mc_asoc);

	ret	= connect_path(codec);
	if (value == 0)
		auto_powerdown(codec);
	return ret;
}

static int set_audio_mode_cap(
	struct snd_soc_codec *codec,
	unsigned int value
)
{
	int	ret;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_port_params	*port	= NULL;

	TRACE_FUNC();
	dbg_info("audio_mode=%d\n", value);

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	port	= &mc_asoc->port;
	if (value > 1)
		if ((port->stream != 0)
		&& ((port->rate == MCDRV_FS_96000)
			|| (port->rate == MCDRV_FS_192000)))
			return -EINVAL;

	ret	= write_cache(codec, MC_ASOC_AUDIO_MODE_CAP, value);
	if (ret < 0)
		return ret;

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	if (value == 0)
		del_dsp_prm(mc_asoc);

	ret	= connect_path(codec);
	if (value == 0)
		auto_powerdown(codec);
	return ret;
}

static int set_incall_mic(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value
)
{
	int	ret;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;

	TRACE_FUNC();

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	if ((value == MC_ASOC_INCALL_MIC_MAINMIC)
	|| (value == MC_ASOC_INCALL_MIC_SUBMIC))
		dsp_prm	= &mc_asoc->param_store[DSP_PRM_VC_1MIC][DSP_PRM_BASE];
	else
		dsp_prm	= &mc_asoc->param_store[DSP_PRM_VC_2MIC][DSP_PRM_BASE];

	while (dsp_prm != NULL) {
		if (dsp_prm->dSize > 0) {
			ret	= _McDrv_Ctrl(MCDRV_SET_DSP,
				dsp_prm->pabParam, NULL, dsp_prm->dSize);
			if (ret != 0)
				return map_drv_error(ret);
		}
		dsp_prm	= dsp_prm->next;
	}

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	ret	= connect_path(codec);
	return ret;
}

static int set_ain_playback(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value
)
{
	int	ret;
	int	audio_mode;
	int	audio_mode_cap;

	TRACE_FUNC();

	dbg_info("ain_playback=%d\n", value);

	audio_mode_cap	= read_cache(codec, MC_ASOC_AUDIO_MODE_CAP);
	if (audio_mode_cap < 0)
		return -EIO;
	audio_mode	= read_cache(codec, MC_ASOC_AUDIO_MODE_PLAY);
	if (audio_mode < 0)
		return -EIO;

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	if ((audio_mode == MC_ASOC_AUDIO_MODE_INCALL)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_INCALL2)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_INCALL3)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_INCALL4)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_AUDIO_INCALL)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_AUDIO_INCALL2)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_AUDIO_INCALL3)
	|| (audio_mode == MC_ASOC_AUDIO_MODE_AUDIO_INCALL4)
	) {
		if ((audio_mode_cap == MC_ASOC_AUDIO_MODE_INCALL)
		|| (audio_mode_cap == MC_ASOC_AUDIO_MODE_AUDIO_INCALL))
			return 0;
	}
	if (((audio_mode == MC_ASOC_AUDIO_MODE_INCOMM)
		|| (audio_mode == MC_ASOC_AUDIO_MODE_INCOMM2))
	&& ((audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM)
		|| (audio_mode_cap == MC_ASOC_AUDIO_MODE_OFF)))
		return 0;
	if ((audio_mode == MC_ASOC_AUDIO_MODE_OFF)
	&& (audio_mode_cap == MC_ASOC_AUDIO_MODE_INCOMM))
		return 0;

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	ret	= connect_path(codec);
	if (value == 0)
		auto_powerdown(codec);
	return ret;
}

static int set_dtmf_control(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value
)
{
	int	ret;

	TRACE_FUNC();

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	ret	= connect_path(codec);
	if (value == 0)
		auto_powerdown(codec);
	return ret;
}

static int set_dtmf_output(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value
)
{
	int		ret;

	TRACE_FUNC();

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	if (mc_asoc_hold == YMC_NOTITY_HOLD_ON)
		return 0;

	ret	= connect_path(codec);
	return ret;
}

static int set_switch_clock(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value
)
{
	int	ret;
	struct	MCDRV_CLOCKSW_INFO	sInfo;

	TRACE_FUNC();

	sInfo.bClkSrc	= (UINT8)value;
	ret	= _McDrv_Ctrl(MCDRV_SET_CLOCKSW, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_masterslave(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	sInfo.asPortInfo[bPort].sDioCommon.bMasterSlave	= (UINT8)value;
	dFlag	= (bPort == 1) ? MCDRV_EXT_COM_UPDATE_FLAG
				: MCDRV_VOICE_COM_UPDATE_FLAG;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_rate(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	sInfo.asPortInfo[bPort].sDioCommon.bFs	= value;
	dFlag	= (bPort == 1) ? MCDRV_EXT_COM_UPDATE_FLAG
				: MCDRV_VOICE_COM_UPDATE_FLAG;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_bitclock_rate(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	sInfo.asPortInfo[bPort].sDioCommon.bBckFs	= (UINT8)value;
	dFlag	= (bPort == 1) ? MCDRV_EXT_COM_UPDATE_FLAG
				: MCDRV_VOICE_COM_UPDATE_FLAG;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_interface(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	sInfo.asPortInfo[bPort].sDioCommon.bInterface	= (UINT8)value;
	dFlag	= (bPort == 1) ? MCDRV_EXT_COM_UPDATE_FLAG
				: MCDRV_VOICE_COM_UPDATE_FLAG;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_bitclock_invert(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	sInfo.asPortInfo[bPort].sDioCommon.bBckInvert	= (UINT8)value;
	dFlag	= (bPort == 1) ? MCDRV_EXT_COM_UPDATE_FLAG
				: MCDRV_VOICE_COM_UPDATE_FLAG;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_da_bit_width(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sDaFormat.bBitSel	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sDaFormat.bBitSel	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_da_format(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sDaFormat.bMode	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sDaFormat.bMode	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_pcm_monostereo(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sPcmFormat.bMono	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sPcmFormat.bMono	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_pcm_bit_order(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sPcmFormat.bOrder	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sPcmFormat.bOrder	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_pcm_format(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sPcmFormat.bLaw	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sPcmFormat.bLaw	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_pcm_bit_width(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort,
	UINT8	bInOut
)
{
	int	ret;
	struct MCDRV_DIO_INFO	sInfo;
	UINT32	dFlag;

	TRACE_FUNC();

	ret	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	if (bInOut == 0)
		sInfo.asPortInfo[bPort].sDir.sPcmFormat.bBitSel	= (UINT8)value;
	else
		sInfo.asPortInfo[bPort].sDit.sPcmFormat.bBitSel	= (UINT8)value;
	dFlag	= (bPort == 1) ?
		MCDRV_EXT_DIR_UPDATE_FLAG|MCDRV_EXT_DIT_UPDATE_FLAG
		: MCDRV_VOICE_DIR_UPDATE_FLAG|MCDRV_VOICE_DIT_UPDATE_FLAG;

	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_phys_port(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	UINT8	bPort
)
{
	int	ret;
	struct MCDRV_DIOPATH_INFO	sInfo;

	TRACE_FUNC();

	sInfo.abPhysPort[bPort]	= (UINT8)value;
	ret	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO_PATH, &sInfo, NULL,
		1<<bPort);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_swap(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value,
	size_t	offset,
	UINT32	dFlag
)
{
	int	ret;
	struct MCDRV_SWAP_INFO	sInfo;

	/* TRACE_FUNC(); */

	*(UINT8 *)((void *)&sInfo+offset)	= (UINT8)value;
	ret	= _McDrv_Ctrl(MCDRV_SET_SWAP, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_dsp(
	struct snd_soc_codec	*codec,
	UINT8	*param,
	unsigned long	size,
	unsigned long	option
)
{
	int	err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	output_path;
	int	input_path;
	int	incall_mic;
	UINT8	*param2;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	output_path	= read_cache(codec, MC_ASOC_OUTPUT_PATH);
	if (output_path < 0)
		return -EIO;
	input_path	= read_cache(codec, MC_ASOC_INPUT_PATH);
	if (input_path < 0)
		return -EIO;
	incall_mic	= get_incall_mic(codec, output_path);
	if (incall_mic < 0)
		return -EIO;

	dbg_info("option=%08lX\n", option);
	switch (option) {
	case	YMC_DSP_OUTPUT_BASE:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_INPUT_BASE:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_VOICECALL_BASE_COMMON:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_BASE, param, size);
		if (err != 0)
			break;

		param2	= get_dsp_mem(size);
		if (param2 == NULL) {
			err	= -ENOMEM;
			break;
		}
		memcpy(param2, param, size);
		param	= param2;
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_VOICECALL_BASE_1MIC:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_BASE, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_BASE_2MIC:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_BASE, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	case	YMC_DSP_OUTPUT_SP:
		if (output_path != MC_ASOC_OUTPUT_PATH_SP) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_OUTPUT_RC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_RC)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_OUTPUT_HP:
		if ((output_path != MC_ASOC_OUTPUT_PATH_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_HS)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_HP)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_OUTPUT_LO1:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_LO1)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_OUTPUT_LO2:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_LO2)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_OUTPUT_BT:
		if ((output_path != MC_ASOC_OUTPUT_PATH_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_BT)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_MAINMIC:
		if (input_path != MC_ASOC_INPUT_PATH_MAINMIC) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_SUBMIC:
		if (input_path != MC_ASOC_INPUT_PATH_SUBMIC) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_2MIC:
		if (input_path != MC_ASOC_INPUT_PATH_2MIC) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_HEADSET:
		if (input_path != MC_ASOC_INPUT_PATH_HS) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_BT:
		if (input_path != MC_ASOC_INPUT_PATH_BT) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_INPUT_LINEIN1:
		if (input_path != MC_ASOC_INPUT_PATH_LIN1) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_USER, param, size);
		break;
	case	YMC_DSP_VOICECALL_SP_1MIC:
		if (output_path != MC_ASOC_OUTPUT_PATH_SP) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
			DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_RC_1MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_RC)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_HP_1MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_HP)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_LO1_1MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_LO1)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_LO2_1MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_LO2)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_HEADSET:
		if (output_path != MC_ASOC_OUTPUT_PATH_HS) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_BT:
		if ((output_path != MC_ASOC_OUTPUT_PATH_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_BT)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_BT)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if ((incall_mic != MC_ASOC_INCALL_MIC_MAINMIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_SUBMIC))
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_SP_2MIC:
		if (output_path != MC_ASOC_OUTPUT_PATH_SP) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_RC_2MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_RC)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_RC)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_HP_2MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_HS)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_HP)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_HP)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_LO1_2MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO1)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO2_LO1)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	case	YMC_DSP_VOICECALL_LO2_2MIC:
		if ((output_path != MC_ASOC_OUTPUT_PATH_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_SP_LO2)
		&& (output_path != MC_ASOC_OUTPUT_PATH_LO1_LO2)) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			goto exit;
		}
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_USER, param, size);
		if (err != 0)
			break;
		if (incall_mic != MC_ASOC_INCALL_MIC_2MIC)
			goto exit;
		break;
	default:
		err	= -EINVAL;
		break;
	}
	if (err != 0) {
		;
		goto error;
	}
	err	= map_drv_error(_McDrv_Ctrl(MCDRV_SET_DSP, param, NULL, size));
exit:
	return err;
error:
#ifdef DSP_MEM_STATIC
#else
	vfree(param);
#endif
	return err;
}

static int mc_asoc_write_reg(
	struct snd_soc_codec *codec,
	unsigned int reg,
	unsigned int value)
{
	int	err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;

	/* TRACE_FUNC(); */

	/* dbg_info("reg=%d, value=%04Xh\n", reg, value); */

	if (codec == NULL)
		return -EINVAL;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	mc_asoc_lock("mc_asoc_write_reg");

	if (reg <= MC_ASOC_N_VOL_REG) {
		switch (reg) {
		case	MC_ASOC_DVOL_MUSICIN:
		case	MC_ASOC_DVOL_EXTIN:
		case	MC_ASOC_DVOL_VOICEIN:
		case	MC_ASOC_DVOL_REFIN:
		case	MC_ASOC_DVOL_ADIF0IN:
		case	MC_ASOC_DVOL_ADIF1IN:
		case	MC_ASOC_DVOL_ADIF2IN:
		case	MC_ASOC_DVOL_MUSICOUT:
		case	MC_ASOC_DVOL_EXTOUT:
		case	MC_ASOC_DVOL_VOICEOUT:
		case	MC_ASOC_DVOL_REFOUT:
		case	MC_ASOC_DVOL_DAC0OUT:
		case	MC_ASOC_DVOL_DAC1OUT:
		case	MC_ASOC_DVOL_DPATHDA:
		case	MC_ASOC_DVOL_DPATHAD:
		case	MC_ASOC_DVOL_APLAY_D:
			if ((((value>>8)&0x7F) > 114)
			|| ((value&0x7F) > 114)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_LINEIN1:
		case	MC_ASOC_AVOL_MIC1:
		case	MC_ASOC_AVOL_MIC2:
		case	MC_ASOC_AVOL_MIC3:
		case	MC_ASOC_AVOL_MIC4:
		case	MC_ASOC_DVOL_APLAY_A:
			if ((((value>>8)&0x7F) > 63)
			|| ((value&0x7F) > 63)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_HP:
			if ((((value>>8)&0x7F) > 127)
			|| ((value&0x7F) > 127)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_SP:
			if ((((value>>8)&0x7F) > 127)
			|| ((value&0x7F) > 127)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_RC:
			if ((((value>>8)&0x7F) > 111)
			|| ((value&0x7F) > 111)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_LINEOUT1:
		case	MC_ASOC_AVOL_LINEOUT2:
			if ((((value>>8)&0x7F) > 119)
			|| ((value&0x7F) > 119)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_SP_GAIN:
			if ((((value>>8)&0x7F) > 4)
			|| ((value&0x7F) > 4)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_DVOL_MASTER:
		case	MC_ASOC_DVOL_VOICE:
			if ((((value>>8)&0x7F) > 75)
			|| ((value&0x7F) > 75)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_VOICE_RECORDING:
			if ((value&0x7F) > 1) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		}
		if (err == 0) {
			;
			err	= write_reg_vol(codec, reg, value);
		}
	} else {
		switch (reg) {
		case	MC_ASOC_AUDIO_MODE_PLAY:
			err	= set_audio_mode_play(codec, value);
			break;
		case	MC_ASOC_AUDIO_MODE_CAP:
			err	= set_audio_mode_cap(codec, value);
			break;
		case	MC_ASOC_OUTPUT_PATH:
			err	= write_cache(codec, reg, value);
			break;
		case	MC_ASOC_INPUT_PATH:
			err	= write_cache(codec, reg, value);
			break;
		case	MC_ASOC_INCALL_MIC_SP:
		case	MC_ASOC_INCALL_MIC_RC:
		case	MC_ASOC_INCALL_MIC_HP:
		case	MC_ASOC_INCALL_MIC_LO1:
		case	MC_ASOC_INCALL_MIC_LO2:
			err	= set_incall_mic(codec, reg, value);
			break;
		case	MC_ASOC_MAINMIC_PLAYBACK_PATH:
		case	MC_ASOC_SUBMIC_PLAYBACK_PATH:
		case	MC_ASOC_2MIC_PLAYBACK_PATH:
		case	MC_ASOC_HSMIC_PLAYBACK_PATH:
		case	MC_ASOC_BTMIC_PLAYBACK_PATH:
		case	MC_ASOC_LIN1_PLAYBACK_PATH:
			err	= set_ain_playback(codec, reg, value);
			break;
		case	MC_ASOC_PARAMETER_SETTING:
			break;
		case	MC_ASOC_DTMF_CONTROL:
			err	= set_dtmf_control(codec, reg, value);
			break;
		case	MC_ASOC_DTMF_OUTPUT:
			err	= set_dtmf_output(codec, reg, value);
			break;
		case	MC_ASOC_SWITCH_CLOCK:
			err	= set_switch_clock(codec, reg, value);
			break;
		case	MC_ASOC_EXT_MASTERSLAVE:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_masterslave(codec, reg, value,
						PORT_EXT);
			break;
		case	MC_ASOC_EXT_RATE:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_rate(codec, reg, value,
						PORT_EXT);
			break;
		case	MC_ASOC_EXT_BITCLOCK_RATE:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_bitclock_rate(codec, reg, value,
						PORT_EXT);
			break;
		case	MC_ASOC_EXT_INTERFACE:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_interface(codec, reg, value,
						PORT_EXT);
			break;
		case	MC_ASOC_EXT_BITCLOCK_INVERT:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_bitclock_invert(codec, reg,
						value, PORT_EXT);
			break;
		case	MC_ASOC_EXT_INPUT_DA_BIT_WIDTH:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_da_bit_width(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_INPUT_DA_FORMAT:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_da_format(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_INPUT_PCM_MONOSTEREO:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_monostereo(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_INPUT_PCM_BIT_ORDER:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_bit_order(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_INPUT_PCM_FORMAT:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_format(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_INPUT_PCM_BIT_WIDTH:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_bit_width(codec, reg, value,
						PORT_EXT, 0);
			break;
		case	MC_ASOC_EXT_OUTPUT_DA_BIT_WIDTH:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_da_bit_width(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_EXT_OUTPUT_DA_FORMAT:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_da_format(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_EXT_OUTPUT_PCM_MONOSTEREO:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_monostereo(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_EXT_OUTPUT_PCM_BIT_ORDER:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_bit_order(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_EXT_OUTPUT_PCM_FORMAT:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_format(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_EXT_OUTPUT_PCM_BIT_WIDTH:
			if (CAPTURE_PORT != CAPTURE_PORT_EXT)
				err	= set_pcm_bit_width(codec, reg, value,
						PORT_EXT, 1);
			break;
		case	MC_ASOC_VOICE_MASTERSLAVE:
			err	= set_masterslave(codec, reg, value,
				PORT_VOICE);
			break;
		case	MC_ASOC_VOICE_RATE:
			err	= set_rate(codec, reg, value, PORT_VOICE);
			break;
		case	MC_ASOC_VOICE_BITCLOCK_RATE:
			err	= set_bitclock_rate(codec, reg, value,
				PORT_VOICE);
			break;
		case	MC_ASOC_VOICE_INTERFACE:
			err	= set_interface(codec, reg, value, PORT_VOICE);
			break;
		case	MC_ASOC_VOICE_BITCLOCK_INVERT:
			err	= set_bitclock_invert(codec, reg, value,
				PORT_VOICE);
			break;
		case	MC_ASOC_VOICE_INPUT_DA_BIT_WIDTH:
			err	= set_da_bit_width(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_INPUT_DA_FORMAT:
			err	= set_da_format(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_INPUT_PCM_MONOSTEREO:
			err	= set_pcm_monostereo(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_INPUT_PCM_BIT_ORDER:
			err	= set_pcm_bit_order(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_INPUT_PCM_FORMAT:
			err	= set_pcm_format(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_INPUT_PCM_BIT_WIDTH:
			err	= set_pcm_bit_width(codec, reg, value,
				PORT_VOICE, 0);
			break;
		case	MC_ASOC_VOICE_OUTPUT_DA_BIT_WIDTH:
			err	= set_da_bit_width(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_VOICE_OUTPUT_DA_FORMAT:
			err	= set_da_format(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_VOICE_OUTPUT_PCM_MONOSTEREO:
			err	= set_pcm_monostereo(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_VOICE_OUTPUT_PCM_BIT_ORDER:
			err	= set_pcm_bit_order(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_VOICE_OUTPUT_PCM_FORMAT:
			err	= set_pcm_format(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_VOICE_OUTPUT_PCM_BIT_WIDTH:
			err	= set_pcm_bit_width(codec, reg, value,
				PORT_VOICE, 1);
			break;
		case	MC_ASOC_MUSIC_PHYSICAL_PORT:
			err	= set_phys_port(codec, reg, value, PORT_MUSIC);
			break;
		case	MC_ASOC_EXT_PHYSICAL_PORT:
			err	= set_phys_port(codec, reg, value, PORT_EXT);
			break;
		case	MC_ASOC_VOICE_PHYSICAL_PORT:
			err	= set_phys_port(codec, reg, value, PORT_VOICE);
			break;
		case	MC_ASOC_HIFI_PHYSICAL_PORT:
			err	= set_phys_port(codec, reg, value, PORT_HIFI);
			break;
		case	MC_ASOC_ADIF0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif0),
				MCDRV_SWAP_ADIF0_UPDATE_FLAG);
			break;
		case	MC_ASOC_ADIF1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif1),
				MCDRV_SWAP_ADIF1_UPDATE_FLAG);
			break;
		case	MC_ASOC_ADIF2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif2),
				MCDRV_SWAP_ADIF2_UPDATE_FLAG);
			break;
		case	MC_ASOC_DAC0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bDac0),
				MCDRV_SWAP_DAC0_UPDATE_FLAG);
			break;
		case	MC_ASOC_DAC1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bDac1),
				MCDRV_SWAP_DAC1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut0),
				MCDRV_SWAP_MUSICOUT0_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn0),
				MCDRV_SWAP_MUSICIN0_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn1),
				MCDRV_SWAP_MUSICIN1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn2),
				MCDRV_SWAP_MUSICIN2_UPDATE_FLAG);
			break;
		case	MC_ASOC_EXT_IN_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bExtIn),
				MCDRV_SWAP_EXTIN_UPDATE_FLAG);
			break;
		case	MC_ASOC_VOICE_IN_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bVoiceIn),
				MCDRV_SWAP_VOICEIN_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut1),
				MCDRV_SWAP_MUSICOUT1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut2),
				MCDRV_SWAP_MUSICOUT2_UPDATE_FLAG);
			break;
		case	MC_ASOC_EXT_OUT_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bExtOut),
				MCDRV_SWAP_EXTOUT_UPDATE_FLAG);
			break;
		case	MC_ASOC_VOICE_OUT_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bVoiceOut),
				MCDRV_SWAP_VOICEOUT_UPDATE_FLAG);
			break;
		case	MC_ASOC_ADIF0_SOURCE:
		case	MC_ASOC_ADIF1_SOURCE:
		case	MC_ASOC_ADIF2_SOURCE:
			err	= write_cache(codec, reg, value);
			if (err < 0)
				break;
			err	= connect_path(codec);
			break;
		case	MC_ASOC_DSP_PARAM:
			{
			char	*param	= NULL;
			UINT32	size;
			UINT16	option;

			if (value >= ARRAY_SIZE(firmware_name)) {
				err	= -EINVAL;
				break;
			}
			size	= load_file(firmware_name[value], &param);
			dbg_info("file:%s\n", firmware_name[value]);
			dbg_info("size:%ld\n", size);
			dbg_info("param:%p", param);
			if (size < 0) {
				break;
			} else {
#if 0
				int	i;
				char	tmp[20], str[256];
				tmp[0]	= str[0]	= 0;
				strcat(str, "read data:\n");
				for (i = 0; i < size; i++) {
					sprintf(tmp, " %02X", param[i]);
					strcat(str, tmp);
					if ((i%16) == 15) {
						pr_info("%s", str);
						str[0]	= 0;
					}
				}
				strcat(str, "\n");
				pr_info("%s", str);
#endif
			}

			option	= read_cache(codec, MC_ASOC_DSP_PARAM_OPT);
			err	= set_dsp(codec, param, size, option);
			}
			break;
		case	MC_ASOC_DSP_PARAM_OPT:
			switch (value) {
			case	YMC_DSP_OUTPUT_BASE:
			case	YMC_DSP_OUTPUT_SP:
			case	YMC_DSP_OUTPUT_RC:
			case	YMC_DSP_OUTPUT_HP:
			case	YMC_DSP_OUTPUT_LO1:
			case	YMC_DSP_OUTPUT_LO2:
			case	YMC_DSP_OUTPUT_BT:
			case	YMC_DSP_INPUT_BASE:
			case	YMC_DSP_INPUT_MAINMIC:
			case	YMC_DSP_INPUT_SUBMIC:
			case	YMC_DSP_INPUT_2MIC:
			case	YMC_DSP_INPUT_HEADSET:
			case	YMC_DSP_INPUT_BT:
			case	YMC_DSP_INPUT_LINEIN1:
			case	YMC_DSP_INPUT_LINEIN2:
			case	YMC_DSP_VOICECALL_BASE_1MIC:
			case	YMC_DSP_VOICECALL_BASE_2MIC:
			case	YMC_DSP_VOICECALL_SP_1MIC:
			case	YMC_DSP_VOICECALL_SP_2MIC:
			case	YMC_DSP_VOICECALL_RC_1MIC:
			case	YMC_DSP_VOICECALL_RC_2MIC:
			case	YMC_DSP_VOICECALL_HP_1MIC:
			case	YMC_DSP_VOICECALL_HP_2MIC:
			case	YMC_DSP_VOICECALL_LO1_1MIC:
			case	YMC_DSP_VOICECALL_LO1_2MIC:
			case	YMC_DSP_VOICECALL_LO2_1MIC:
			case	YMC_DSP_VOICECALL_LO2_2MIC:
			case	YMC_DSP_VOICECALL_HEADSET:
			case	YMC_DSP_VOICECALL_BT:
			case	YMC_DSP_VOICECALL_BASE_COMMON:
				write_cache(codec, reg, value);
				break;
			default:
				err	= -EINVAL;
				break;
			}
			break;
		case	MC_ASOC_PLAYBACK_SCENARIO:
		case	MC_ASOC_CAPTURE_SCENARIO:
			write_cache(codec, reg, value);
			break;
		case	MC_ASOC_CLEAR_DSP_PARAM:
			del_dsp_prm(mc_asoc);
			break;
		case	MC_ASOC_MAIN_MIC:
			mc_asoc_main_mic	= value;
			write_cache(codec, reg, value);
			break;
		case	MC_ASOC_SUB_MIC:
			mc_asoc_sub_mic	= value;
			write_cache(codec, reg, value);
			break;
		case	MC_ASOC_HS_MIC:
			mc_asoc_hs_mic	= value;
			write_cache(codec, reg, value);
			break;
#ifdef MC_ASOC_TEST
		case	MC_ASOC_MIC1_BIAS:
			mc_asoc_mic1_bias	= value;
			write_cache(codec, reg, value);
			err	= connect_path(codec);
			break;
		case	MC_ASOC_MIC2_BIAS:
			mc_asoc_mic2_bias	= value;
			write_cache(codec, reg, value);
			err	= connect_path(codec);
			break;
		case	MC_ASOC_MIC3_BIAS:
			mc_asoc_mic3_bias	= value;
			write_cache(codec, reg, value);
			err	= connect_path(codec);
			break;
		case	MC_ASOC_MIC4_BIAS:
			mc_asoc_mic4_bias	= value;
			write_cache(codec, reg, value);
			err	= connect_path(codec);
			break;
#endif

		default:
			err	= -EINVAL;
			break;
		}
	}

	if (err < 0)
		dbg_info("err=%d\n", err);

	mc_asoc_unlock("mc_asoc_write_reg");
	return err;
}

static unsigned int mc_asoc_read_reg(
	struct snd_soc_codec *codec,
	unsigned int reg)
{
	int	ret;
/*
	TRACE_FUNC();

	dbg_info("reg=%d\n", reg);
*/
	mc_asoc_lock("mc_asoc_read_reg");
	if (codec == NULL) {
		ret	= -EINVAL;
		goto exit;
	}
	ret	= read_cache(codec, reg);
	if (ret < 0)
		ret	= -EIO;
exit:
	mc_asoc_unlock("mc_asoc_read_reg");
	return (unsigned int)ret;
}

/*
 * Same as snd_soc_add_controls supported in alsa-driver 1.0.19 or later.
 * This function is implimented for compatibility with linux 2.6.29.
 */
static int mc_asoc_add_controls(
	struct snd_soc_codec *codec,
	const struct snd_kcontrol_new *controls,
	int n)
{
	return snd_soc_add_codec_controls(codec, controls, n);
}

static const struct snd_soc_dapm_widget	mc_asoc_widgets[] = {
	/* DACs */
	SND_SOC_DAPM_DAC("DAC DUMMY", "DAC Playback", SND_SOC_NOPM, 0, 0),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC DUMMY", "ADC Capture", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_INPUT("INPUT DUMMY"),
	SND_SOC_DAPM_OUTPUT("OUTPUT DUMMY"),
};

static const struct snd_soc_dapm_widget	mc_asoc_widgets_Headset[] = {
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_INPUT("AMIC1"),
	/* Headset Control */
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route	mc_asoc_intercon[] = {
	{"OUTPUT DUMMY",	NULL,		"DAC DUMMY"},
	{"ADC DUMMY",		NULL,		"INPUT DUMMY"}
};

static const struct snd_soc_dapm_route	mc_asoc_intercon_Headset[] = {
	{"Headphone Jack",	NULL,		"HPOUTL"},
	{"Headphone Jack",	NULL,		"HPOUTR"},
	{"Mic Jack",		NULL,		"AMIC1"},
};

static int mc_asoc_add_widgets(struct snd_soc_codec *codec)
{
	int	err;

	if (codec == NULL)
		return -EINVAL;

	err	= snd_soc_dapm_new_controls(&codec->dapm,
				mc_asoc_widgets,
				ARRAY_SIZE(mc_asoc_widgets));
	if (err < 0)
		return err;

	err	= snd_soc_dapm_add_routes(&codec->dapm,
				mc_asoc_intercon,
				ARRAY_SIZE(mc_asoc_intercon));
	if (err < 0)
		return err;

	err	= snd_soc_dapm_new_controls(&codec->dapm,
				mc_asoc_widgets_Headset,
				ARRAY_SIZE(mc_asoc_widgets_Headset));
	if (err < 0)
		return err;

	err	= snd_soc_dapm_add_routes(&codec->dapm,
				mc_asoc_intercon_Headset,
				ARRAY_SIZE(mc_asoc_intercon_Headset));
	if (err < 0)
		return err;

	err	= snd_soc_dapm_new_widgets(&codec->dapm);
	if (err < 0)
		return err;
	return 0;
}

/*
 * Hwdep interface
 */
static int mc_asoc_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	/* Nothing to do */
	mc_asoc_lock("mc_asoc_hwdep_open");
	mc_asoc_unlock("mc_asoc_hwdep_open");
	return 0;
}

static int mc_asoc_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	/* Nothing to do */
	mc_asoc_lock("mc_asoc_hwdep_release");
	mc_asoc_unlock("mc_asoc_hwdep_release");
	return 0;
}

static int hwdep_ioctl_read_reg(struct MCDRV_REG_INFO *args)
{
	int	err;
	struct MCDRV_REG_INFO	reg_info;

	if (!access_ok(VERIFY_WRITE, args, sizeof(struct MCDRV_REG_INFO)))
		return -EFAULT;
	if (copy_from_user(&reg_info, args, sizeof(struct MCDRV_REG_INFO))
		!= 0)
		return -EFAULT;

	err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);
	else if (copy_to_user(args, &reg_info, sizeof(struct MCDRV_REG_INFO))
		!= 0)
		err	= -EFAULT;

	return 0;
}

static int hwdep_ioctl_write_reg(const struct MCDRV_REG_INFO *args)
{
	int	err;
	struct MCDRV_REG_INFO	reg_info;

	if (!access_ok(VERIFY_READ, args, sizeof(struct MCDRV_REG_INFO)))
		return -EFAULT;

	if (copy_from_user(&reg_info, args, sizeof(struct MCDRV_REG_INFO))
		!= 0)
		return -EFAULT;

	err	= _McDrv_Ctrl(MCDRV_WRITE_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);
	return 0;
}

static int hwdep_ioctl_get_dsp_data(struct ymc_dspdata_args *args)
{
	int	ret	= 0;
	struct ymc_dspdata_args	ymc_dspdata;
	UINT8	*param	= NULL;

	if (!access_ok(VERIFY_WRITE, args, sizeof(struct ymc_dspdata_args)))
		return -EFAULT;

	if (copy_from_user(&ymc_dspdata, args, sizeof(struct ymc_dspdata_args))
		!= 0)
		return -EFAULT;
	if (ymc_dspdata.bufsize == 0)
		return 0;

	if (MAX_YMS_CTRL_PARAM_SIZE < ymc_dspdata.bufsize)
		return -ENOMEM;

	param	= kzalloc(ymc_dspdata.bufsize, GFP_KERNEL);
	if (param == NULL)
		return -ENOMEM;

	ret	= _McDrv_Ctrl(MCDRV_GET_DSP_DATA, param, NULL,
				ymc_dspdata.bufsize);
	if (ret < MCDRV_SUCCESS) {
		ret	= map_drv_error(ret);
		goto EXIT;
	}

	ymc_dspdata.size	= ret;
	if (copy_to_user(args, &ymc_dspdata,
			sizeof(struct ymc_dspdata_args)) != 0) {
		ret	= -EFAULT;
		goto EXIT;
	}
	if (copy_to_user(args->buf, param, ymc_dspdata.size) != 0) {
		ret	= -EFAULT;
		goto EXIT;
	}
EXIT:
	if (param != NULL)
		kfree(param);
	return ret;
}

static int hwdep_ioctl_set_dsp_data(struct ymc_dspdata_args *args)
{
	int	ret	= 0;
	struct ymc_dspdata_args	ymc_dspdata;
	UINT8	*param	= NULL;

	if (!access_ok(VERIFY_WRITE, args, sizeof(struct ymc_dspdata_args)))
		return -EFAULT;

	if (copy_from_user(&ymc_dspdata, args, sizeof(struct ymc_dspdata_args))
		!= 0)
		return -EFAULT;

	if (ymc_dspdata.bufsize == 0)
		return 0;

	if (MAX_YMS_CTRL_PARAM_SIZE < ymc_dspdata.bufsize)
		return -ENOMEM;

	param	= kzalloc(ymc_dspdata.bufsize, GFP_KERNEL);
	if (param == NULL)
		return -ENOMEM;
	if (copy_from_user(param, ymc_dspdata.buf, ymc_dspdata.bufsize) != 0) {
		ret	= EFAULT;
		goto EXIT;
	}

	ret	= _McDrv_Ctrl(MCDRV_SET_DSP_DATA, param, NULL,
				ymc_dspdata.bufsize);
	if (ret < MCDRV_SUCCESS) {
		ret	= map_drv_error(ret);
		goto EXIT;
	}
	ymc_dspdata.size	= ret;
	if (copy_to_user(args, &ymc_dspdata,
			sizeof(struct ymc_dspdata_args)) != 0) {
		ret	= -EFAULT;
		goto EXIT;
	}
EXIT:
	if (param != NULL)
		kfree(param);
	return ret;
}

static int mc_asoc_hwdep_ioctl(
	struct snd_hwdep *hw,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int	err	= 0;
	UINT8	*param	= NULL;
	struct snd_soc_codec	*codec	= NULL;
	struct ymc_ctrl_args	ymc_ctrl_arg;
	UINT32	hold;

	if (hw != NULL)
		codec	= hw->private_data;

	mc_asoc_lock("mc_asoc_hwdep_ioctl");

	switch (cmd) {
	case YMC_IOCTL_SET_CTRL:
		if (!access_ok(VERIFY_READ,
			(struct ymc_ctrl_args *)arg,
			sizeof(struct ymc_ctrl_args))) {
			err	= -EFAULT;
			break;
		}
		if (copy_from_user(&ymc_ctrl_arg,
			(struct ymc_ctrl_args *)arg,
			sizeof(struct ymc_ctrl_args)) != 0) {
			err	= -EFAULT;
			break;
		}
		if (ymc_ctrl_arg.size == 0)
			break;
		if (MAX_YMS_CTRL_PARAM_SIZE < ymc_ctrl_arg.size) {
			err	= -ENOMEM;
			break;
		}

		param	= get_dsp_mem(ymc_ctrl_arg.size);
		if (param == NULL) {
			err	= -ENOMEM;
			break;
		}
		if (copy_from_user(param, ymc_ctrl_arg.param,
			ymc_ctrl_arg.size) != 0) {
#ifdef DSP_MEM_STATIC
#else
			vfree(param);
#endif
			err	= -EFAULT;
			break;
		}
		err	= set_dsp(codec, param, ymc_ctrl_arg.size,
							ymc_ctrl_arg.option);
		break;

	case YMC_IOCTL_READ_REG:
		err	= hwdep_ioctl_read_reg((struct MCDRV_REG_INFO *)arg);
		dbg_info("err=%d, RegType=%d, Addr=%d, Data=0x%02X\n",
				err,
				((struct MCDRV_REG_INFO *)arg)->bRegType,
				((struct MCDRV_REG_INFO *)arg)->bAddress,
				((struct MCDRV_REG_INFO *)arg)->bData);
		break;

	case YMC_IOCTL_WRITE_REG:
		err	= hwdep_ioctl_write_reg((struct MCDRV_REG_INFO *)arg);
		dbg_info("err=%d, RegType=%d, Addr=%d, Data=0x%02X\n",
				err,
				((struct MCDRV_REG_INFO *)arg)->bRegType,
				((struct MCDRV_REG_INFO *)arg)->bAddress,
				((struct MCDRV_REG_INFO *)arg)->bData);
		break;

	case YMC_IOCTL_NOTIFY_HOLD:
		if (!access_ok(VERIFY_READ, (UINT32 *)arg, sizeof(UINT32))) {
			err	= -EFAULT;
			break;
		}
		if (copy_from_user(&hold, (UINT32 *)arg, sizeof(UINT32)) != 0
		) {
			err	= -EFAULT;
			break;
		}
		dbg_info("hold=%ld\n", hold);
		switch (hold) {
		case	YMC_NOTITY_HOLD_OFF:
			mc_asoc_hold	= (UINT8)hold;
			err	= connect_path(codec);
			if (err == 0)
				auto_powerdown(codec);
			break;
		case	YMC_NOTITY_HOLD_ON:
			mc_asoc_hold	= (UINT8)hold;
			break;
		default:
			err	= -EINVAL;
			break;
		}
		break;

	case YMC_IOCTL_GET_DSP_DATA:
		err	= hwdep_ioctl_get_dsp_data(
				(struct ymc_dspdata_args *)arg);
		break;
	case YMC_IOCTL_SET_DSP_DATA:
		err	= hwdep_ioctl_set_dsp_data(
				(struct ymc_dspdata_args *)arg);
		break;
	default:
		err	= -EINVAL;
	}

	mc_asoc_unlock("mc_asoc_hwdep_ioctl");
	return err;
}

static int mc_asoc_add_hwdep(struct snd_soc_codec *codec)
{
	struct snd_hwdep	*hw;
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	err;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	err	= snd_hwdep_new((struct snd_card *)codec->card->snd_card,
				MC_ASOC_HWDEP_ID, 0, &hw);
	if (err < 0)
		return err;

	hw->iface		= SNDRV_HWDEP_IFACE_YAMAHA_YMU831;
	hw->private_data	= codec;
	hw->ops.open		= mc_asoc_hwdep_open;
	hw->ops.release		= mc_asoc_hwdep_release;
	hw->ops.ioctl		= mc_asoc_hwdep_ioctl;
	hw->exclusive		= 1;
	strcpy(hw->name, MC_ASOC_HWDEP_ID);
	mc_asoc->hwdep		= hw;

	return 0;
}

#ifdef CONFIG_SWITCH
#define	SW_DRV
#endif
static struct input_dev		*inp_dev;
#ifdef SW_DRV
static struct switch_dev	*h2w_sdev;
static ssize_t headset_print_name(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {
	case 0:
		return sprintf(buf, "No Device\n");
	case 1:
		return sprintf(buf, "Headset\n");
	case 2:
		return sprintf(buf, "Headphone\n");
	}
	return -EINVAL;
}
#endif	/*	SW_DRV	*/

static struct snd_soc_jack	hs_jack;
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin	= "Mic Jack",
		.mask	= SND_JACK_MICROPHONE,
	},
	{
		.pin	= "Headphone Jack",
		.mask	= SND_JACK_HEADPHONE,
	},
};

static struct workqueue_struct	*workq_mb4;
static struct delayed_work	delayed_work_mb4;

static void work_mb4(struct work_struct *work)
{
	TRACE_FUNC();

	mc_asoc_lock("work_mb4");
	mc_asoc_MBSEL4	= 0x00;
	connect_path(mc_asoc_codec);
	mc_asoc_unlock("work_mb4");
}

static struct workqueue_struct	*workq_mkdeten;
static struct delayed_work	delayed_work_mkdeten;
#ifdef MICOPEN_4POLE
static UINT8	mc_asoc_MICDET;
#endif

static void work_mkdeten(struct work_struct *work)
{
	int	err;
	UINT8	jack_status	= mc_asoc_jack_status;
	struct MCDRV_HSDET_INFO	stHSDetInfo;
#ifdef MICOPEN_4POLE
	struct MCDRV_REG_INFO	reg_info;
#endif

	TRACE_FUNC();

	if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		dbg_info("skip mkdeten\n");
		return;
	}

	mutex_lock(&hsdet_mutex);
	mc_asoc_lock("work_mkdeten");

#ifdef MICOPEN_4POLE
	reg_info.bRegType	= MCDRV_REGTYPE_CD;
	reg_info.bAddress	= MCI_MICDET;
	err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		reg_info.bData	= 1;
	if (((reg_info.bData&0x47) == 0)
	&& ((reg_info.bData&0x47) == mc_asoc_MICDET)
	&& ((mc_asoc_hpimpclass < MC_ASOC_IMPCLASS_THRESHOLD)
		|| (mc_asoc_hpimpclass == 5)
		|| (mc_asoc_hpimpclass == 6))) {
		dbg_info("MICDET\n");
		mc_asoc_jack_status	= SND_JACK_HEADSET;
#ifdef SW_DRV
		switch_set_state(h2w_sdev, 0);
		switch_set_state(h2w_sdev, 1);
#endif
		dbg_info("queue_delayed_work_mb4\n");
		queue_delayed_work(workq_mb4, &delayed_work_mb4,
						msecs_to_jiffies(MSDETMB4OFF));
	} else {
#endif
		err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo,
								NULL, 0);
		if (err == MCDRV_SUCCESS) {
			stHSDetInfo.bEnMicDet		= MCDRV_MICDET_DISABLE;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					(void *)&stHSDetInfo, NULL, 0x7C);
		}
		if (err < MCDRV_SUCCESS) {
			;
			dbg_info("%d: Error in work_mkdeten\n", err);
		}
		connect_path(mc_asoc_codec);
#ifdef MICOPEN_4POLE
	}
#endif

	mc_asoc_unlock("work_mkdeten");
	if (jack_status != mc_asoc_jack_status) {
		if (mc_asoc_jack_status != 0) {
			;
			snd_soc_jack_report(&hs_jack, 0, SND_JACK_HEADSET);
		}
		snd_soc_jack_report(&hs_jack,
			mc_asoc_jack_status, SND_JACK_HEADSET);
	}
	mutex_unlock(&hsdet_mutex);
}

static void hsdet_cb(UINT32 dFlags, struct MCDRV_HSDET_RES *psRes)
{
	struct MCDRV_HSDET_INFO	stHSDetInfo;
	int	err;
	UINT8	hpimpclass	= mc_asoc_hpimpclass;
	UINT8	bCurEnPlugDetDb;
	UINT8	bEnPlugDetDb;
	UINT8	bEnMicDet;
	UINT8	bEnDlyKeyOff;
	UINT8	bEnDlyKeyOn;
	UINT8	bEnKeyOff;
	UINT8	bEnKeyOn;
	UINT8	bKey0OnDlyTim;
	UINT8	bKey1OnDlyTim;
	UINT8	bKey2OnDlyTim;
	UINT8	bKey0OnDlyTim2;
	UINT8	bKey1OnDlyTim2;
	UINT8	bKey2OnDlyTim2;
	UINT8	bHsDetDbnc, bDbncNumPlug;
	UINT8	jack_status	= mc_asoc_jack_status;
	UINT32	jack_btn_on	= 0,
		jack_btn_off	= 0,
		jack_btn_stat	= 0;

	TRACE_FUNC();

	mutex_lock(&hsdet_mutex);
	mc_asoc_lock("hsdet_cb");

	dbg_info("dFlags=0x%08lX, bKeyCnt0=%d, bKeyCnt1=%d, bKeyCnt2=%d\n",
		dFlags,
		psRes->bKeyCnt0, psRes->bKeyCnt1,
		psRes->bKeyCnt2);

#ifdef HSDET_WHILE_SUSPEND
	if (mc_asoc_suspended == 0) {
		bEnPlugDetDb	= stHSDetInfo_Default.bEnPlugDetDb;
		bEnMicDet	= stHSDetInfo_Default.bEnMicDet;
		bEnDlyKeyOff	= stHSDetInfo_Default.bEnDlyKeyOff;
		bEnDlyKeyOn	= stHSDetInfo_Default.bEnDlyKeyOn;
		bEnKeyOff	= stHSDetInfo_Default.bEnKeyOff;
		bEnKeyOn	= stHSDetInfo_Default.bEnKeyOn;
		bKey0OnDlyTim	= stHSDetInfo_Default.bKey0OnDlyTim;
		bKey1OnDlyTim	= stHSDetInfo_Default.bKey1OnDlyTim;
		bKey2OnDlyTim	= stHSDetInfo_Default.bKey2OnDlyTim;
		bKey0OnDlyTim2	= stHSDetInfo_Default.bKey0OnDlyTim2;
		bKey1OnDlyTim2	= stHSDetInfo_Default.bKey1OnDlyTim2;
		bKey2OnDlyTim2	= stHSDetInfo_Default.bKey2OnDlyTim2;
		bHsDetDbnc	= stHSDetInfo_Default.bHsDetDbnc;
		bDbncNumPlug	= stHSDetInfo_Default.bDbncNumPlug;
	} else {
		bEnPlugDetDb	= stHSDetInfo_Suspend.bEnPlugDetDb;
		bEnMicDet	= stHSDetInfo_Suspend.bEnMicDet;
		bEnDlyKeyOff	= stHSDetInfo_Suspend.bEnDlyKeyOff;
		bEnDlyKeyOn	= stHSDetInfo_Suspend.bEnDlyKeyOn;
		bEnKeyOff	= stHSDetInfo_Suspend.bEnKeyOff;
		bEnKeyOn	= stHSDetInfo_Suspend.bEnKeyOn;
		bKey0OnDlyTim	= stHSDetInfo_Suspend.bKey0OnDlyTim;
		bKey1OnDlyTim	= stHSDetInfo_Suspend.bKey1OnDlyTim;
		bKey2OnDlyTim	= stHSDetInfo_Suspend.bKey2OnDlyTim;
		bKey0OnDlyTim2	= stHSDetInfo_Suspend.bKey0OnDlyTim2;
		bKey1OnDlyTim2	= stHSDetInfo_Suspend.bKey1OnDlyTim2;
		bKey2OnDlyTim2	= stHSDetInfo_Suspend.bKey2OnDlyTim2;
		bHsDetDbnc	= stHSDetInfo_Suspend.bHsDetDbnc;
		bDbncNumPlug	= stHSDetInfo_Suspend.bDbncNumPlug;
	}
#else
	bEnPlugDetDb	= stHSDetInfo_Default.bEnPlugDetDb;
	bEnMicDet	= stHSDetInfo_Default.bEnMicDet;
	bEnDlyKeyOff	= stHSDetInfo_Default.bEnDlyKeyOff;
	bEnDlyKeyOn	= stHSDetInfo_Default.bEnDlyKeyOn;
	bEnKeyOff	= stHSDetInfo_Default.bEnKeyOff;
	bEnKeyOn	= stHSDetInfo_Default.bEnKeyOn;
	bKey0OnDlyTim	= stHSDetInfo_Default.bKey0OnDlyTim;
	bKey1OnDlyTim	= stHSDetInfo_Default.bKey1OnDlyTim;
	bKey2OnDlyTim	= stHSDetInfo_Default.bKey2OnDlyTim;
	bKey0OnDlyTim2	= stHSDetInfo_Default.bKey0OnDlyTim2;
	bKey1OnDlyTim2	= stHSDetInfo_Default.bKey1OnDlyTim2;
	bKey2OnDlyTim2	= stHSDetInfo_Default.bKey2OnDlyTim2;
#endif
	err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo, NULL, 0);
	if (err < MCDRV_SUCCESS) {
		dbg_info("%d: Error in MCDRV_GET_HSDET\n", err);
		goto exit;
	}
	bCurEnPlugDetDb	= stHSDetInfo.bEnPlugDetDb;

	if (dFlags & MCDRV_HSDET_EVT_SENSEFIN_FLAG) {
		dbg_info("bPlugRev=%d, bHpImpClass=%d, wHpImp=%d\n",
			psRes->bPlugRev, psRes->bHpImpClass,
			psRes->wHpImp);
		mc_asoc_hpimpclass	= psRes->bHpImpClass;
	}

	if (dFlags & MCDRV_HSDET_EVT_PLUGUNDET_DB_FLAG) {
		if (bCurEnPlugDetDb & MCDRV_PLUGDETDB_UNDET_ENABLE) {
			dbg_info("PLUGUNDETDB\n");
			mc_asoc_jack_status	= 0;
#ifdef SW_DRV
			switch_set_state(h2w_sdev, 0);
#endif
			cancel_delayed_work(&delayed_work_mb4);
			dbg_info("cancel_delayed_work_mb4\n");
			cancel_delayed_work(&delayed_work_mkdeten);
			dbg_info("cancel_delayed_work_mkdeten\n");

			mc_asoc_MBSEL4	= 0x80;

			stHSDetInfo.bEnPlugDetDb	=
					bEnPlugDetDb & MCDRV_PLUGDETDB_DET_ENABLE;
			stHSDetInfo.bEnMicDet		= bEnMicDet;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bHsDetDbnc		= bHsDetDbnc;
			stHSDetInfo.bDbncNumPlug	= bDbncNumPlug;
			stHSDetInfo.cbfunc		= NULL;
			err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					(void *)&stHSDetInfo, NULL, 0x410000FE);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);

			stHSDetInfo.cbfunc	= hsdet_cb;
			err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					(void *)&stHSDetInfo, NULL, 0x40000000);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
			mc_asoc_hpimpclass	= (UINT8)-1;
		} else {
			connect_path(mc_asoc_codec);
		}
	}

	if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		if (dFlags & MCDRV_HSDET_EVT_KEYON0_FLAG) {
			dbg_info("KEYON_0\n");
			if ((bEnKeyOn & MCDRV_KEYEN_D_D_E) != 0) {
				jack_btn_on	= SND_JACK_BTN_0;
				jack_btn_stat	= SND_JACK_BTN_0;
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYON1_FLAG) {
			dbg_info("KEYON_1\n");
			if ((bEnKeyOn & MCDRV_KEYEN_D_E_D) != 0) {
				jack_btn_on	= SND_JACK_BTN_1;
				jack_btn_stat	= SND_JACK_BTN_1;
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYON2_FLAG) {
			dbg_info("KEYON_2\n");
			if ((bEnKeyOn & MCDRV_KEYEN_E_D_D) != 0) {
				jack_btn_on	= SND_JACK_BTN_2;
				jack_btn_stat	= SND_JACK_BTN_2;
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_KEYOFF0_FLAG) {
			dbg_info("KEYOFF_0\n");
			if ((bEnKeyOff & MCDRV_KEYEN_D_D_E) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_0;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_D_D_E) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey0OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 1) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~1;
					stHSDetInfo.bKey0OnDlyTim	=
					    bKey0OnDlyTim;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x2020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYOFF1_FLAG) {
			dbg_info("KEYOFF_1\n");
			if ((bEnKeyOff & MCDRV_KEYEN_D_E_D) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_1;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_D_E_D) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey1OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 2) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~2;
					stHSDetInfo.bKey1OnDlyTim	=
					    bKey1OnDlyTim;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x4020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYOFF2_FLAG) {
			dbg_info("KEYOFF_2\n");
			if ((bEnKeyOff & MCDRV_KEYEN_E_D_D) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_2;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_E_D_D) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey2OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 4) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~4;
					stHSDetInfo.bKey2OnDlyTim	=
					    bKey2OnDlyTim;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x8020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_DLYKEYON0_FLAG) {
			dbg_info("DLYKEYON_0\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_D_D_E) != 0) {
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON0, 1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON0, 0);
				input_sync(inp_dev);
				if ((mc_asoc_ver_id == 0)
				&& (bKey0OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 1;
					stHSDetInfo.bKey0OnDlyTim	= 0;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x2020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYON1_FLAG) {
			dbg_info("DLYKEYON_1\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_D_E_D) != 0) {
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON1, 1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON1, 0);
				input_sync(inp_dev);

				if ((mc_asoc_ver_id == 0)
				&& (bKey1OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 2;
					stHSDetInfo.bKey1OnDlyTim	= 0;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x4020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYON2_FLAG) {
			dbg_info("DLYKEYON_2\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_E_D_D) != 0) {
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON2, 1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
					MC_ASOC_EV_KEY_DELAYKEYON2, 0);
				input_sync(inp_dev);

				if ((mc_asoc_ver_id == 0)
				&& (bKey2OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 4;
					stHSDetInfo.bKey2OnDlyTim	= 0;
					err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					  (void *)&stHSDetInfo, NULL, 0x8020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF0_FLAG) {
			dbg_info("DLYKEYOFF_0\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_D_D_E) != 0) {
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff0[psRes->bKeyCnt0],
					1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff0[psRes->bKeyCnt0],
					0);
				input_sync(inp_dev);
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF1_FLAG) {
			dbg_info("DLYKEYOFF_1\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_D_E_D) != 0) {
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff1[psRes->bKeyCnt1],
				  1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff1[psRes->bKeyCnt1],
				  0);
				input_sync(inp_dev);
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF2_FLAG) {
			dbg_info("DLYKEYOFF_2\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_E_D_D) != 0) {
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff2[psRes->bKeyCnt2],
				  1);
				input_sync(inp_dev);
				input_report_key(inp_dev,
				  mc_asoc_ev_key_delaykeyoff2[psRes->bKeyCnt2],
				  0);
				input_sync(inp_dev);
			}
		}
	}

	if ((dFlags & MCDRV_HSDET_EVT_PLUGDET_DB_FLAG)
	&& (bCurEnPlugDetDb & MCDRV_PLUGDETDB_DET_ENABLE)) {
		dbg_info("PLUGDETDB\n");
		if ((dFlags & MCDRV_HSDET_EVT_MICDET_FLAG)
		&& (bEnMicDet & MCDRV_MICDET_ENABLE)) {
			if ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
			&& (mc_asoc_hpimpclass != 5)
			&& (mc_asoc_hpimpclass != 6)) {
				mc_asoc_jack_status	= SND_JACK_HEADPHONE;
#ifdef SW_DRV
				switch_set_state(h2w_sdev, 2);
#endif
				bEnMicDet	= MCDRV_MICDET_DISABLE;
				stHSDetInfo.bEnMicDet	= bEnMicDet;
				bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
				bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
				bEnKeyOff	= MCDRV_KEYEN_D_D_D;
				bEnKeyOn	= MCDRV_KEYEN_D_D_D;
			} else {
				dbg_info("MICDET\n");
				mc_asoc_jack_status	= SND_JACK_HEADSET;
#ifdef SW_DRV
				switch_set_state(h2w_sdev, 1);
#endif
				dbg_info("queue_delayed_work_mb4\n");
				queue_delayed_work(workq_mb4, &delayed_work_mb4,
						msecs_to_jiffies(MSDETMB4OFF));
			}
		} else {
#ifdef MICOPEN_4POLE
			struct MCDRV_REG_INFO	reg_info;
			reg_info.bRegType	= MCDRV_REGTYPE_CD;
			reg_info.bAddress	= MCI_MICDET;
			err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL,
									0);
			if (err != MCDRV_SUCCESS)
				reg_info.bData	= 1;
			mc_asoc_MICDET	= reg_info.bData & 0x47;
#endif
			mc_asoc_jack_status	= SND_JACK_HEADPHONE;
#ifdef SW_DRV
			switch_set_state(h2w_sdev, 2);
#endif
			dbg_info("queue_delayed_work_mkdeten\n");
			queue_delayed_work(workq_mkdeten,
						&delayed_work_mkdeten,
						msecs_to_jiffies(MSMKDETENOFF));
		}
		stHSDetInfo.bEnPlugDetDb	=
				bEnPlugDetDb & MCDRV_PLUGDETDB_UNDET_ENABLE;
		stHSDetInfo.bEnDlyKeyOff	= bEnDlyKeyOff;
		stHSDetInfo.bEnDlyKeyOn		= bEnDlyKeyOn;
		stHSDetInfo.bEnKeyOff		= bEnKeyOff;
		stHSDetInfo.bEnKeyOn		= bEnKeyOn;
		stHSDetInfo.bHsDetDbnc		= HSUNDETDBNC;
		stHSDetInfo.bDbncNumPlug	= HSUNDETDBNCNUM;
		stHSDetInfo.cbfunc		= NULL;
		err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
				(void *)&stHSDetInfo, NULL, 0x410000FE);
		if (err < MCDRV_SUCCESS)
			dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		stHSDetInfo.cbfunc	= hsdet_cb;
		err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
				(void *)&stHSDetInfo, NULL, 0x40000000);
		if (err < MCDRV_SUCCESS)
			dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		if (stHSDetInfo.bSgnlNum == 0xFF)
			mc_asoc_hpimpclass	= MC_ASOC_IMP_TBL_NUM - 1;
		mc_asoc_MBSEL4	= 0x80;
	}
	if ((mc_asoc_jack_status == SND_JACK_HEADPHONE)
	&& (dFlags & MCDRV_HSDET_EVT_MICDET_FLAG)
	&& (bEnMicDet & MCDRV_MICDET_ENABLE)) {
		if ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
		&& (mc_asoc_hpimpclass != 5)
		&& (mc_asoc_hpimpclass != 6)) {
			cancel_delayed_work(&delayed_work_mkdeten);

			stHSDetInfo.bEnMicDet		= MCDRV_MICDET_DISABLE;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
					(void *)&stHSDetInfo, NULL, 0x7C);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		} else {
			dbg_info("MICDET\n");
			mc_asoc_jack_status	= SND_JACK_HEADSET;
	#ifdef SW_DRV
			switch_set_state(h2w_sdev, 0);
			switch_set_state(h2w_sdev, 1);
	#endif
			cancel_delayed_work(&delayed_work_mkdeten);
			dbg_info("cancel_delayed_work_mkdeten\n");
			dbg_info("queue_delayed_work_mb4\n");
			queue_delayed_work(workq_mb4, &delayed_work_mb4,
							msecs_to_jiffies(MSDETMB4OFF));
		}
	}

	if (hpimpclass != mc_asoc_hpimpclass) {
		if ((mc_asoc_hpimpclass == (UINT8)-1)
		|| ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
		    && (mc_asoc_hpimpclass != 5)
		    && (mc_asoc_hpimpclass != 6))) {
			connect_path(mc_asoc_codec);
		} else {
			struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
			int	preset_idx	= 0;

			if (get_mixer_path_ctl_info(mc_asoc_codec,
				&mixer_ctl_info) < 0)
				goto exit;
			preset_idx	= get_path_preset_idx(&mixer_ctl_info);
			if ((preset_idx < 0) || (preset_idx > PRESET_PATH_N))
				goto exit;
			set_volume(mc_asoc_codec, &mixer_ctl_info, preset_idx);
		}
	}

exit:
	;
	mc_asoc_unlock("hsdet_cb");
	if (jack_status != mc_asoc_jack_status) {
		dbg_info("mc_asoc_jack_status=%d\n", mc_asoc_jack_status);
		if (mc_asoc_jack_status != 0) {
			;
			snd_soc_jack_report(&hs_jack, 0, SND_JACK_HEADSET);
		}
		snd_soc_jack_report(&hs_jack,
			mc_asoc_jack_status, SND_JACK_HEADSET);
	}
	if (jack_btn_on != 0) {
		snd_soc_jack_report(&hs_jack, jack_btn_stat, jack_btn_on);
	}
	if (jack_btn_off != 0) {
		snd_soc_jack_report(&hs_jack, 0, jack_btn_off);
	}
	mutex_unlock(&hsdet_mutex);
}

static struct workqueue_struct	*my_wq;

static void irq_func(struct work_struct *work)
{
	int	err;
	struct mc_asoc_data	*mc_asoc	= NULL;

	TRACE_FUNC();

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);

	err	= _McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0);
	if (err < 0)
		pr_info("irq_func %d\n", map_drv_error(err));

	if (IRQ_TYPE == IRQ_TYPE_LEVEL_LOW) {
		;
		enable_irq(mc_asoc->pdata->irq);
	}
	kfree((void *)work);
}
irqreturn_t irq_handler(int irq, void *data)
{
	int	ret;
	struct work_struct	*work;
	struct mc_asoc_data	*mc_asoc	= NULL;

	TRACE_FUNC();

	if (mc_asoc_suspended == 1) {
		mc_asoc_irq_func	= 1;
		return IRQ_HANDLED;
	}

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);

	work	= kmalloc(sizeof(struct work_struct), GFP_ATOMIC);
	if (work) {
		if (IRQ_TYPE == IRQ_TYPE_LEVEL_LOW) {
			;
			disable_irq_nosync(mc_asoc->pdata->irq);
		}
		INIT_WORK((struct work_struct *)work, irq_func);
		ret	= queue_work(my_wq, (struct work_struct *)work);
	}
	return IRQ_HANDLED;
}

static int init_irq(struct snd_soc_codec *codec)
{
	int	err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;

	TRACE_FUNC();

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (!mc_asoc->pdata->irq) {
		dev_err(codec->dev, "%s : No irq supported\n", __func__);
		return 0;
	}

	my_wq	= create_workqueue("irq_queue");

	err	= irq_set_irq_type(mc_asoc->pdata->irq, IRQ_TYPE);
	if (err < 0) {
		dev_err(codec->dev, "Failed to set_irq_type: %d\n", err);
		return -EIO;
	}
	err	= request_irq(mc_asoc->pdata->irq, irq_handler, IRQF_DISABLED,
							"MC_YAMAHA IRQ", NULL);
	if (err < 0) {
		dev_err(codec->dev, "Failed to request_irq: %d\n", err);
		return -EIO;
	}
	return 0;
}
static int term_irq(void)
{
	struct mc_asoc_data	*mc_asoc	= NULL;

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);

	if (!mc_asoc->pdata->irq) {
		dev_err(mc_asoc_codec->dev, "%s : No irq supported\n", __func__);
		return 0;
	}

	free_irq(mc_asoc->pdata->irq, NULL);
	destroy_workqueue(my_wq);
	if (workq_mb4)
		destroy_workqueue(workq_mb4);
	if (workq_mkdeten)
		destroy_workqueue(workq_mkdeten);

	return 0;
}


/*
 * Codec device
 */
static int mc_asoc_probe(
	struct snd_soc_codec *codec
)
{
	int	i;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct device		*dev		= NULL;
	struct MCDRV_DIO_INFO	sDioInfo;
	struct MCDRV_DIOPATH_INFO	sDioPathInfo;
	int	err, err2;
	UINT32	update	= 0;
	struct MCDRV_REG_INFO	reg_info;

	TRACE_FUNC();


	mc_asoc_codec		= codec;
	mc_asoc_suspended	= 0;
	mc_asoc_hpimpclass	= (UINT8)-1;
	mc_asoc_jack_status	= 0;
	mc_asoc_irq_func	= 0;
#ifdef DSP_MEM_STATIC
	dsp_mem_pt		= 0;
#endif

	workq_mb4	= create_workqueue("mb4");
	if (workq_mb4 == NULL) {
		err	= -ENOMEM;
		goto error_codec_data;
	}
	INIT_DELAYED_WORK(&delayed_work_mb4, work_mb4);

	workq_mkdeten	= create_workqueue("mkdeten");
	if (workq_mkdeten == NULL) {
		err	= -ENOMEM;
		goto error_codec_data;
	}
	INIT_DELAYED_WORK(&delayed_work_mkdeten, work_mkdeten);

#ifdef DELAY_CONNECT_XXX
	workq_cfg_slim_sch	= create_workqueue("cfg_slim_sch");
	if (workq_cfg_slim_sch == NULL) {
		err	= -ENOMEM;
		goto error_codec_data;
	}
	INIT_DELAYED_WORK(&delayed_work_cfg_slim_sch, work_cfg_slim_sch);
#endif

	if (codec == NULL) {
		/*printk(KERN_ERR "I2C bus is not probed successfully\n");*/
		err	= -ENODEV;
		goto error_codec_data;
	}

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	dev	= codec->dev;
	if (mc_asoc == NULL || dev == NULL) {
		err	= -ENODEV;
		goto error_codec_data;
	}

	/* init hardware */
	mc_asoc->setup	= mc_asoc_cfg_setup;
	mc_asoc->setup.init2.bOption[19]	= 1;
	if (mc_asoc->pdata != NULL) {
		if (mc_asoc->pdata->set_codec_ldod != NULL) {
			;
			mc_asoc->setup.init2.bOption[19]	= 0;
		}
	}
	err	= _McDrv_Ctrl(MCDRV_INIT, &mc_asoc->setup.init,
						&mc_asoc->setup.init2, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_INIT\n", err);
		err	= -EIO;
		goto error_init_hw;
	}

	reg_info.bRegType	= MCDRV_REGTYPE_ANA;
	reg_info.bAddress	= 0;
	err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_READ_REG\n", err);
		err	= -EIO;
		goto error_init_hw;
	}
	mc_asoc_ver_id	= reg_info.bData&0x07;

	if (mc_asoc_ver_id < 2) {
		err	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(dev, "%d: Error in MCDRV_TERM\n", err);
			err	= -EIO;
			goto error_init_hw;
		}
		mc_asoc->setup.init.bMbSel1	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel2	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel3	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel4	= MCDRV_MBSEL_20;
		err	= _McDrv_Ctrl(MCDRV_INIT, &mc_asoc->setup.init,
						&mc_asoc->setup.init2, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(dev, "%d: Error in MCDRV_INIT\n", err);
			err	= -EIO;
			goto error_init_hw;
		}
	}

	if (mc_asoc_ver_id == 0) {
		vreg_map[MC_ASOC_AVOL_HP].volmap	= volmap_hp_es1;
		vreg_map[MC_ASOC_AVOL_LINEOUT2].volmap	= volmap_lineout;
		vreg_map[MC_ASOC_DVOL_ADIF0IN].volmap	= volmap_adif;
		vreg_map[MC_ASOC_DVOL_ADIF1IN].volmap	= volmap_adif;
		vreg_map[MC_ASOC_DVOL_APLAY_D].volmap	= volmap_adif;
	} else {
		vreg_map[MC_ASOC_AVOL_SP].volmap	= volmap_sp[4];
	}

	/* controls */
	err	= mc_asoc_add_controls(codec, mc_asoc_snd_controls,
				ARRAY_SIZE(mc_asoc_snd_controls));
	if (err < 0) {
		dev_err(dev, "%d: Error in mc_asoc_add_controls\n", err);
		goto error_add_ctl;
	}

	err	= mc_asoc_add_widgets(codec);
	if (err < 0) {
		dev_err(dev, "%d: Error in mc_asoc_add_widgets\n", err);
		goto error_add_ctl;
	}

	/* hwdep */
	err	= mc_asoc_add_hwdep(codec);
	if (err < 0) {
		dev_err(dev, "%d: Error in mc_asoc_add_hwdep\n", err);
		goto error_add_hwdep;
	}

	write_cache(codec, MC_ASOC_EXT_MASTERSLAVE,
		stExtPort_Default.sDioCommon.bMasterSlave);
	write_cache(codec, MC_ASOC_EXT_RATE,
		stExtPort_Default.sDioCommon.bFs);
	write_cache(codec, MC_ASOC_EXT_BITCLOCK_RATE,
		stExtPort_Default.sDioCommon.bBckFs);
	write_cache(codec, MC_ASOC_EXT_INTERFACE,
		stExtPort_Default.sDioCommon.bInterface);
	write_cache(codec, MC_ASOC_EXT_BITCLOCK_INVERT,
		stExtPort_Default.sDioCommon.bBckInvert);
	write_cache(codec, MC_ASOC_EXT_INPUT_DA_BIT_WIDTH,
		stExtPort_Default.sDir.sDaFormat.bBitSel);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_DA_BIT_WIDTH,
		stExtPort_Default.sDit.sDaFormat.bBitSel);
	write_cache(codec, MC_ASOC_EXT_INPUT_DA_FORMAT,
		stExtPort_Default.sDir.sDaFormat.bMode);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_DA_FORMAT,
		stExtPort_Default.sDit.sDaFormat.bMode);
	write_cache(codec, MC_ASOC_EXT_INPUT_PCM_MONOSTEREO,
		stExtPort_Default.sDir.sPcmFormat.bMono);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_PCM_MONOSTEREO,
		stExtPort_Default.sDit.sPcmFormat.bMono);
	write_cache(codec, MC_ASOC_EXT_INPUT_PCM_BIT_ORDER,
		stExtPort_Default.sDir.sPcmFormat.bOrder);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_PCM_BIT_ORDER,
		stExtPort_Default.sDit.sPcmFormat.bOrder);
	write_cache(codec, MC_ASOC_EXT_INPUT_PCM_FORMAT,
		stExtPort_Default.sDir.sPcmFormat.bLaw);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_PCM_FORMAT,
		stExtPort_Default.sDit.sPcmFormat.bLaw);
	write_cache(codec, MC_ASOC_EXT_INPUT_PCM_BIT_WIDTH,
		stExtPort_Default.sDir.sPcmFormat.bBitSel);
	write_cache(codec, MC_ASOC_EXT_OUTPUT_PCM_BIT_WIDTH,
		stExtPort_Default.sDit.sPcmFormat.bBitSel);

	write_cache(codec, MC_ASOC_VOICE_MASTERSLAVE,
		stVoicePort_Default.sDioCommon.bMasterSlave);
	write_cache(codec, MC_ASOC_VOICE_RATE,
		stVoicePort_Default.sDioCommon.bFs);
	write_cache(codec, MC_ASOC_VOICE_BITCLOCK_RATE,
		stVoicePort_Default.sDioCommon.bBckFs);
	write_cache(codec, MC_ASOC_VOICE_INTERFACE,
		stVoicePort_Default.sDioCommon.bInterface);
	write_cache(codec, MC_ASOC_VOICE_BITCLOCK_INVERT,
		stVoicePort_Default.sDioCommon.bBckInvert);
	write_cache(codec, MC_ASOC_VOICE_INPUT_DA_BIT_WIDTH,
		stVoicePort_Default.sDir.sDaFormat.bBitSel);
	write_cache(codec, MC_ASOC_VOICE_OUTPUT_DA_BIT_WIDTH,
		stVoicePort_Default.sDit.sDaFormat.bBitSel);
	write_cache(codec, MC_ASOC_VOICE_INPUT_DA_FORMAT,
		stVoicePort_Default.sDir.sDaFormat.bMode);
	write_cache(codec, MC_ASOC_VOICE_OUTPUT_DA_FORMAT,
		stVoicePort_Default.sDit.sDaFormat.bMode);
	write_cache(codec, MC_ASOC_VOICE_INPUT_PCM_MONOSTEREO,
		stVoicePort_Default.sDir.sPcmFormat.bMono);
	write_cache(codec, MC_ASOC_VOICE_OUTPUT_PCM_MONOSTEREO,
		stVoicePort_Default.sDit.sPcmFormat.bMono);
	write_cache(codec, MC_ASOC_VOICE_INPUT_PCM_BIT_ORDER,
		stVoicePort_Default.sDir.sPcmFormat.bOrder);
	write_cache(codec, MC_ASOC_VOICE_OUTPUT_PCM_BIT_ORDER,
		stVoicePort_Default.sDit.sPcmFormat.bOrder);
	write_cache(codec, MC_ASOC_VOICE_INPUT_PCM_FORMAT,
		stVoicePort_Default.sDir.sPcmFormat.bLaw);
	write_cache(codec, MC_ASOC_VOICE_OUTPUT_PCM_FORMAT,
		stVoicePort_Default.sDit.sPcmFormat.bLaw);
	write_cache(codec, MC_ASOC_VOICE_INPUT_PCM_BIT_WIDTH,
		stVoicePort_Default.sDir.sPcmFormat.bBitSel);

	write_cache(codec, MC_ASOC_VOICE_OUTPUT_PCM_BIT_WIDTH,
		stVoicePort_Default.sDit.sPcmFormat.bBitSel);

	write_cache(codec, MC_ASOC_VOICE_RECORDING, VOICE_RECORDING_UNMUTE);
	write_cache(codec, MC_ASOC_INCALL_MIC_SP, INCALL_MIC_SP);
	write_cache(codec, MC_ASOC_INCALL_MIC_RC, INCALL_MIC_RC);
	write_cache(codec, MC_ASOC_INCALL_MIC_HP, INCALL_MIC_HP);
	write_cache(codec, MC_ASOC_INCALL_MIC_LO1, INCALL_MIC_LO1);
	write_cache(codec, MC_ASOC_INCALL_MIC_LO2, INCALL_MIC_LO2);

	write_cache(codec, MC_ASOC_MUSIC_PHYSICAL_PORT, MUSIC_PHYSICAL_PORT);
	write_cache(codec, MC_ASOC_EXT_PHYSICAL_PORT, EXT_PHYSICAL_PORT);
	write_cache(codec, MC_ASOC_VOICE_PHYSICAL_PORT, VOICE_PHYSICAL_PORT);
	write_cache(codec, MC_ASOC_HIFI_PHYSICAL_PORT, HIFI_PHYSICAL_PORT);

	write_cache(codec, MC_ASOC_MAIN_MIC, mc_asoc_main_mic);
	write_cache(codec, MC_ASOC_SUB_MIC, mc_asoc_sub_mic);
	write_cache(codec, MC_ASOC_HS_MIC, mc_asoc_hs_mic);
#ifdef MC_ASOC_TEST
	write_cache(codec, MC_ASOC_MIC1_BIAS, mc_asoc_mic1_bias);
	write_cache(codec, MC_ASOC_MIC2_BIAS, mc_asoc_mic2_bias);
	write_cache(codec, MC_ASOC_MIC3_BIAS, mc_asoc_mic3_bias);
	write_cache(codec, MC_ASOC_MIC4_BIAS, mc_asoc_mic4_bias);
#endif

	/* Headset jack detection */
	snd_soc_jack_new(codec, "Headset",
		SND_JACK_HEADSET|SND_JACK_BTN_0|SND_JACK_BTN_1|SND_JACK_BTN_2,
		&hs_jack);

	snd_jack_set_key(hs_jack.jack, SND_JACK_BTN_0, KEY_MEDIA);
	snd_jack_set_key(hs_jack.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(hs_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);

	snd_soc_jack_add_pins(&hs_jack,
		ARRAY_SIZE(hs_jack_pins),
		hs_jack_pins);

	mc_asoc->jack.hs_jack = &hs_jack;

	inp_dev	= input_allocate_device();
	inp_dev->name = "Headset keys";
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON0);
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON1);
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON2);
	for (i = 0; i < 8; i++) {
		input_set_capability(inp_dev,
			EV_KEY,
			mc_asoc_ev_key_delaykeyoff0[i]);
		input_set_capability(inp_dev,
			EV_KEY,
			mc_asoc_ev_key_delaykeyoff1[i]);
		input_set_capability(inp_dev,
			EV_KEY,
			mc_asoc_ev_key_delaykeyoff2[i]);
	}
	err	= input_register_device(inp_dev);
	if (err != 0) {
		dev_err(dev, "%d: Error in input_register_device\n", err);
		goto error_set_mode;
	}

#ifdef SW_DRV
	h2w_sdev	= kzalloc(sizeof(struct switch_dev), GFP_KERNEL);
	h2w_sdev->name	= "h2w";
	h2w_sdev->print_name	= headset_print_name;
	err	= switch_dev_register(h2w_sdev);
	if (err < 0) {
		dev_err(dev, "%d: Error in switch_dev_register\n", err);
		goto error_set_mode;
	}

	mc_asoc->jack.h2w_sdev = h2w_sdev;
#endif

	sDioInfo.asPortInfo[0]	= stMusicPort_Default;
	sDioInfo.asPortInfo[1]	= stExtPort_Default;
	sDioInfo.asPortInfo[2]	= stVoicePort_Default;
	sDioInfo.asPortInfo[3]	= stHifiPort_Default;

	update	= MCDRV_MUSIC_COM_UPDATE_FLAG
		| MCDRV_MUSIC_DIR_UPDATE_FLAG
		| MCDRV_MUSIC_DIT_UPDATE_FLAG
		| MCDRV_EXT_COM_UPDATE_FLAG
		| MCDRV_EXT_DIR_UPDATE_FLAG
		| MCDRV_EXT_DIT_UPDATE_FLAG
		| MCDRV_VOICE_COM_UPDATE_FLAG
		| MCDRV_VOICE_DIR_UPDATE_FLAG
		| MCDRV_VOICE_DIT_UPDATE_FLAG
		| MCDRV_HIFI_COM_UPDATE_FLAG
		| MCDRV_HIFI_DIR_UPDATE_FLAG
		| MCDRV_HIFI_DIT_UPDATE_FLAG;
	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sDioInfo, NULL, update);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_DIGITALIO\n", err);
		goto error_set_mode;
	}

	update	= MCDRV_PHYS0_UPDATE_FLAG
		| MCDRV_PHYS1_UPDATE_FLAG
		| MCDRV_PHYS2_UPDATE_FLAG
		| MCDRV_PHYS3_UPDATE_FLAG
		| MCDRV_DIR0SLOT_UPDATE_FLAG
		| MCDRV_DIR1SLOT_UPDATE_FLAG
		| MCDRV_DIR2SLOT_UPDATE_FLAG
		| MCDRV_DIT0SLOT_UPDATE_FLAG
		| MCDRV_DIT1SLOT_UPDATE_FLAG
		| MCDRV_DIT2SLOT_UPDATE_FLAG;
	sDioPathInfo.abPhysPort[0]	= MUSIC_PHYSICAL_PORT;
	sDioPathInfo.abPhysPort[1]	= EXT_PHYSICAL_PORT;
	sDioPathInfo.abPhysPort[2]	= VOICE_PHYSICAL_PORT;
	sDioPathInfo.abPhysPort[3]	= HIFI_PHYSICAL_PORT;
	sDioPathInfo.abMusicRSlot[0]	= mc_asoc_cfg_setup.rslot[0];
	sDioPathInfo.abMusicRSlot[1]	= mc_asoc_cfg_setup.rslot[1];
	sDioPathInfo.abMusicRSlot[2]	= mc_asoc_cfg_setup.rslot[2];
	sDioPathInfo.abMusicTSlot[0]	= mc_asoc_cfg_setup.tslot[0];
	sDioPathInfo.abMusicTSlot[1]	= mc_asoc_cfg_setup.tslot[1];
	sDioPathInfo.abMusicTSlot[2]	= mc_asoc_cfg_setup.tslot[2];
	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO_PATH, &sDioPathInfo, NULL,
			update);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_DIGITALIO_PATH\n", err);
		goto error_set_mode;
	}

	mc_asoc->hsdet_store	= stHSDetInfo_Default;
	mc_asoc->hsdet_store.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.cbfunc		= hsdet_cb;
	if (mc_asoc_ver_id == 0) {
		;
		mc_asoc->hsdet_store.bIrqType	= MCDRV_IRQTYPE_NORMAL;
	}
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET, (void *)&mc_asoc->hsdet_store,
			(void *)&stHSDet2Info_Default, 0x7fffffff);
	if (err < MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_HSDET\n", err);
		goto error_set_mode;
	}

	err	= _McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0);
	if (err < 0) {
		dev_err(dev, "%d: Error in MCDRV_IRQ\n", err);
		goto error_set_mode;
	}

	/* IRQ Initialize */
	err = init_irq(codec);
	if (err < 0) {
		dev_err(dev, "%d: Error in init_irq\n", err);
		goto error_set_mode;
	}

#ifdef HSDET_WHILE_SUSPEND
	device_init_wakeup(dev, 1);
#endif

	set_bias_level(codec, SND_SOC_BIAS_OFF);
	goto exit;

error_set_mode:
error_add_hwdep:
error_add_ctl:
	err2	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
	if (err2 < 0) {
		;
		dev_err(dev, "%d: Error in MCDRV_TERM\n", err2);
	}
error_init_hw:
error_codec_data:
	if (workq_mb4)
		destroy_workqueue(workq_mb4);
	if (workq_mkdeten)
		destroy_workqueue(workq_mkdeten);
	workq_mb4	= NULL;
	workq_mkdeten	= NULL;
#ifdef DELAY_CONNECT_XXX
	if (workq_cfg_slim_sch)
		destroy_workqueue(workq_cfg_slim_sch);
	workq_cfg_slim_sch	= NULL;
#endif
exit:
	return err;
}

static int mc_asoc_remove(struct snd_soc_codec *codec)
{
	int err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;

	/*TRACE_FUNC();*/

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	/* IRQ terminate */
	term_irq();

	input_unregister_device(inp_dev);
#ifdef SW_DRV
	if (h2w_sdev != NULL) {
		switch_dev_unregister(h2w_sdev);
		kfree(h2w_sdev);
		h2w_sdev	= NULL;
	}
#endif

	del_dsp_prm(mc_asoc);

	set_bias_level(codec, SND_SOC_BIAS_OFF);
	if (codec) {
		err	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(codec->dev, "%d: Error in MCDRV_TERM\n", err);
			err	= -EIO;
		}
	}

	destroy_workqueue(workq_mb4);
	destroy_workqueue(workq_mkdeten);
	workq_mb4	= NULL;
	workq_mkdeten	= NULL;
#ifdef DELAY_CONNECT_XXX
	destroy_workqueue(workq_cfg_slim_sch);
	workq_cfg_slim_sch	= NULL;
#endif
	return err;
}

static int mc_asoc_suspend(
	struct snd_soc_codec *codec
)
{
	int	err;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;
#ifdef HSDET_WHILE_SUSPEND
	struct MCDRV_HSDET_INFO	stHSDetInfo;
#else
	int	i;
#endif

	TRACE_FUNC();

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	get_mixer_path_ctl_info(codec, &mixer_ctl_info);
#ifdef HSDET_WHILE_SUSPEND
	if ((mixer_ctl_info.audio_mode_play == 0)
	&& (mixer_ctl_info.audio_mode_cap == 0)
	&& (mixer_ctl_info.mainmic_play == 0)
	&& (mixer_ctl_info.submic_play == 0)
	&& (mixer_ctl_info.msmic_play == 0)
	&& (mixer_ctl_info.hsmic_play == 0)
	&& (mixer_ctl_info.btmic_play == 0)
	&& (mixer_ctl_info.lin1_play == 0)
	&& (mixer_ctl_info.dtmf_control == 0))
		set_bias_level(codec, SND_SOC_BIAS_OFF);
#else
	if ((mixer_ctl_info.audio_mode_play != 0)
	|| (mixer_ctl_info.audio_mode_cap != 0)
	|| (mixer_ctl_info.mainmic_play != 0)
	|| (mixer_ctl_info.submic_play != 0)
	|| (mixer_ctl_info.msmic_play != 0)
	|| (mixer_ctl_info.hsmic_play != 0)
	|| (mixer_ctl_info.btmic_play != 0)
	|| (mixer_ctl_info.lin1_play != 0)
	|| (mixer_ctl_info.dtmf_control != 0))
		return 0;

	set_bias_level(codec, SND_SOC_BIAS_OFF);
#endif

	mc_asoc_lock("mc_asoc_suspend");

	err	= _McDrv_Ctrl(MCDRV_GET_HSDET,
				(void *)&mc_asoc->hsdet_store, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}
	mc_asoc->hsdet_store.bDlyIrqStop	=
					stHSDetInfo_Default.bDlyIrqStop;

#ifdef HSDET_WHILE_SUSPEND
	if (device_may_wakeup(codec->dev))
		enable_irq_wake(mc_asoc->pdata->irq);

	stHSDetInfo	= stHSDetInfo_Suspend;
	if (mc_asoc_ver_id == 0) {
		;
		stHSDetInfo.bIrqType	= MCDRV_IRQTYPE_NORMAL;
	}
	if (mc_asoc_jack_status != SND_JACK_HEADSET) {
		stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	}
	stHSDetInfo.bEnPlugDetDb	&= mc_asoc->hsdet_store.bEnPlugDetDb;
	stHSDetInfo.bEnMicDet		&= mc_asoc->hsdet_store.bEnMicDet;
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
			(void *)&stHSDetInfo, NULL, 0x7fffffff);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}
	stHSDetInfo.cbfunc	= hsdet_cb;
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
			(void *)&stHSDetInfo, NULL, 0x40000000);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}

#else
	/* store parameters */
	for (i = 0; i < MC_ASOC_N_INFO_STORE; i++) {
		if (info_store_tbl[i].get) {
			err	= _McDrv_Ctrl(info_store_tbl[i].get,
				(void *)mc_asoc + info_store_tbl[i].offset,
				NULL, 0);
			if (err != MCDRV_SUCCESS) {
				dev_err(codec->dev,
					"%d: Error in mc_asoc_suspend\n", err);
				err	= -EIO;
				goto error;
			}
		}
	}

	/* IRQ terminate */
	term_irq();

	err	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in MCDRV_TERM\n", err);
		err	= -EIO;
	}
#endif
#ifndef FEATURE_MCLK_CONTROL_BY_YMU831
	if ((mixer_ctl_info.audio_mode_play == 0)
	&& (mixer_ctl_info.audio_mode_cap == 0)
	&& (mixer_ctl_info.mainmic_play == 0)
	&& (mixer_ctl_info.submic_play == 0)
	&& (mixer_ctl_info.msmic_play == 0)
	&& (mixer_ctl_info.hsmic_play == 0)
	&& (mixer_ctl_info.btmic_play == 0)
	&& (mixer_ctl_info.lin1_play == 0)
	&& (mixer_ctl_info.dtmf_control == 0)) {
		if (mc_asoc->pdata->set_codec_mclk)
			mc_asoc->pdata->set_codec_mclk(0, 1);
	}
#endif
	mc_asoc_suspended	= 1;

error:
	mc_asoc_unlock("mc_asoc_suspend");

	return err;
}

static int mc_asoc_resume(
	struct snd_soc_codec *codec
)
{
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	err;
#ifdef HSDET_WHILE_SUSPEND
	struct MCDRV_HSDET_INFO	stHSDetInfo;
#else
	SINT16	*vol	= NULL;
	int	i, j;
	int	output_path;
	int	incall_mic;
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;
#endif
	struct mc_asoc_mixer_path_ctl_info	mixer_ctl_info;

	TRACE_FUNC();

	if (mc_asoc_suspended != 1)
		return 0;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

#ifndef FEATURE_MCLK_CONTROL_BY_YMU831
	if (mc_asoc->pdata->set_codec_mclk)
		mc_asoc->pdata->set_codec_mclk(1, 0);
#endif

	mc_asoc_suspended	= 0;

	if (mc_asoc_irq_func != 0) {
		err	= map_drv_error(_McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0));
		mc_asoc_irq_func	= 0;
	}

	mc_asoc_lock("mc_asoc_resume");

	get_mixer_path_ctl_info(codec, &mixer_ctl_info);
#ifdef HSDET_WHILE_SUSPEND
	if ((mixer_ctl_info.audio_mode_play == 0)
	&& (mixer_ctl_info.audio_mode_cap == 0)
	&& (mixer_ctl_info.mainmic_play == 0)
	&& (mixer_ctl_info.submic_play == 0)
	&& (mixer_ctl_info.msmic_play == 0)
	&& (mixer_ctl_info.hsmic_play == 0)
	&& (mixer_ctl_info.btmic_play == 0)
	&& (mixer_ctl_info.lin1_play == 0)
	&& (mixer_ctl_info.dtmf_control == 0))
		set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#else
	set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#endif

#ifdef HSDET_WHILE_SUSPEND

	err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}

	mc_asoc->hsdet_store.bEnPlugDetDb	=
		stHSDetInfo_Default.bEnPlugDetDb & stHSDetInfo.bEnPlugDetDb;
	mc_asoc->hsdet_store.bEnMicDet		= stHSDetInfo.bEnMicDet;
	if (mc_asoc_jack_status != SND_JACK_HEADSET) {
		mc_asoc->hsdet_store.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	} else {
		mc_asoc->hsdet_store.bEnDlyKeyOff	=
					stHSDetInfo_Default.bEnDlyKeyOff;
		mc_asoc->hsdet_store.bEnDlyKeyOn	=
					stHSDetInfo_Default.bEnDlyKeyOn;
		mc_asoc->hsdet_store.bEnKeyOff		=
					stHSDetInfo_Default.bEnKeyOff;
		mc_asoc->hsdet_store.bEnKeyOn		=
					stHSDetInfo_Default.bEnKeyOn;
	}
	mc_asoc->hsdet_store.cbfunc	= NULL;
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
			(void *)&mc_asoc->hsdet_store, NULL, 0x7fffffff);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}
	mc_asoc->hsdet_store.cbfunc	= hsdet_cb;
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
			(void *)&mc_asoc->hsdet_store, NULL, 0x40000000);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}
	if (device_may_wakeup(codec->dev)) {
		if (!mc_asoc->pdata->irq)
			dev_err(codec->dev, "%s : No irq supported\n", __func__);
		else
			disable_irq_wake(mc_asoc->pdata->irq);
	}

#else
	err	= _McDrv_Ctrl(MCDRV_INIT, &mc_asoc->setup.init,
					&mc_asoc->setup.init2, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in MCDRV_INIT\n", err);
		err	= -EIO;
		goto error;
	}

	/* restore parameters */
	output_path	= read_cache(codec, MC_ASOC_OUTPUT_PATH);
	if (output_path < 0) {
		err	= -EIO;
		goto error;
	}
	incall_mic	= get_incall_mic(codec, output_path);
	if (incall_mic < 0) {
		err	= -EIO;
		goto error;
	}

	for (i = 0; i <= DSP_PRM_VC_2MIC; i++) {
		if ((i == DSP_PRM_VC_1MIC)
		&& (incall_mic == MC_ASOC_INCALL_MIC_2MIC))
			continue;
		if ((i == DSP_PRM_VC_2MIC)
		&& (incall_mic != MC_ASOC_INCALL_MIC_2MIC))
			continue;

		for (j = 0; j <= DSP_PRM_USER; j++) {
			dsp_prm	= &mc_asoc->param_store[i][j];
			while ((dsp_prm != NULL)
			&& (dsp_prm->pabParam != NULL)) {
				dbg_info("pabParam = %8p\n",
					dsp_prm->pabParam);
				err	= _McDrv_Ctrl(MCDRV_SET_DSP,
						dsp_prm->pabParam,
						NULL,
						dsp_prm->dSize);
				if (err != 0) {
					dev_err(codec->dev,
					"%d:Error in mc_asoc_resume(SET_DSP)\n"
					, err);
					dev_err(codec->dev, "i=%d, j=%d\n",
						i, j);
					err	= -EIO;
					goto error;
				}
				dsp_prm	= dsp_prm->next;
			}
		}
	}

	vol	= (SINT16 *)&mc_asoc->vol_store;
	for (i = 0; i < sizeof(struct MCDRV_VOL_INFO)/sizeof(SINT16);
		i++, vol++)
		*vol |= 0x0001;

	/* When pvPrm is "NULL" ,dPrm is "0" */
	for (i = 0; i < MC_ASOC_N_INFO_STORE; i++) {
		if (info_store_tbl[i].set) {
			err	= _McDrv_Ctrl(info_store_tbl[i].set,
					(void *)mc_asoc +
					info_store_tbl[i].offset,
					NULL,
					info_store_tbl[i].flags);
			if (err != MCDRV_SUCCESS) {
				dev_err(codec->dev,
					"%d: Error in mc_asoc_resume\n", err);
				err	= -EIO;
				goto error;
			}
		}
	}

	mc_asoc->hsdet_store.bEnPlugDetDb	=
					stHSDetInfo_Default.bEnPlugDetDb;
	err	= _McDrv_Ctrl(MCDRV_SET_HSDET,
			(void *)&mc_asoc->hsdet_store,
			(void *)&mc_asoc->hsdet2_store,
			0x7fffffff);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}

	/* IRQ Initialize */
	err	= init_irq(codec);
	if (err < 0) {
		dev_err(codec->dev, "%d: Error in init_irq\n", err);
		goto error;
	}
#endif

error:
	mc_asoc_unlock("mc_asoc_resume");
	return err;
}

static int set_bias_level(
	struct snd_soc_codec	*codec,
	enum snd_soc_bias_level	level)
{
	pr_debug("%s codec[%p] level[%d]\n", __func__, codec, level);
	codec->dapm.bias_level	= level;
	return 0;
}
static int mc_asoc_set_bias_level(
	struct snd_soc_codec	*codec,
	enum snd_soc_bias_level	level)
{
	mc_asoc_lock("mc_asoc_set_bias_level");
	set_bias_level(codec, level);
	mc_asoc_unlock("mc_asoc_set_bias_level");
	return 0;
}

struct snd_soc_codec_driver	mc_asoc_codec_dev	= {
	.probe		= mc_asoc_probe,
	.remove		= mc_asoc_remove,
	.suspend	= mc_asoc_suspend,
	.resume		= mc_asoc_resume,
	.read		= mc_asoc_read_reg,
	.write		= mc_asoc_write_reg,
	.reg_cache_size	= MC_ASOC_N_REG,
	.reg_word_size	= sizeof(u16),
	.reg_cache_step	= 1,
	.idle_bias_off	= true,
	.set_bias_level	= mc_asoc_set_bias_level
};

static int spi_rw(u8 *tx, u8 *rx, int len)
{
	struct spi_message	spi_msg;
	struct spi_transfer	spi_xfer;

	/* Initialize SPI ,message */
	spi_message_init(&spi_msg);

	/* Initialize SPI transfer */
	memset(&spi_xfer, 0, sizeof spi_xfer);
	spi_xfer.len	= len;
	spi_xfer.tx_buf	= tx;
	spi_xfer.rx_buf	= rx;

	/* Add SPI transfer to SPI message */
	spi_message_add_tail(&spi_xfer, &spi_msg);

#if 0
	{
		int i;
		char	tmp[20], str[256];
		tmp[0]	= str[0]	= 0;
		sprintf(tmp, "tx len %d:\n", spi_xfer.len);
		strcat(str, tmp);
		for (i = 0; i < spi_xfer.len && i < 32; i++) {
			sprintf(tmp, " %02X", ((u8 *)spi_xfer.tx_buf)[i]);
			strcat(str, tmp);
		}
		strcat(str, "\n");
		dbg_info("%s", str);
	}
#endif

	/* Perform synchronous SPI transfer */
	if (spi_sync(mc_asoc_spi, &spi_msg)) {
		dev_err(&mc_asoc_spi->dev, "spi_sync failure\n");
		return -EIO;
	}

#if 0
	if (spi_xfer.rx_buf) {
		int i;
		char	tmp[20], str[256];
		tmp[0]	= str[0]	= 0;
		sprintf(tmp, "rx len %d:\n", spi_xfer.len);
		strcat(str, tmp);
		for (i = 1; i < spi_xfer.len && i < 32; i++) {
			sprintf(tmp, " %02X", ((u8 *)spi_xfer.rx_buf)[i]);
			strcat(str, tmp);
		}
		strcat(str, "\n");
		dbg_info("%s", str);
	}
#endif
	return 0;
}

static u8	buf[1024];
void mc_asoc_read_data(
	UINT8 bSlaveAdr,
	UINT32 dAddress,
	UINT8 *pbData,
	UINT32 dSize)
{
	u8	*rx	= NULL;
	u8	*readBuf	= buf;

	if ((dSize+2) > sizeof(buf)) {
		rx	= kmalloc(dSize+2, GFP_KERNEL);
		if (rx == NULL) {
			printk(KERN_ERR "Failed to ReadReg\n");
			return;
		}
		readBuf	= rx;
	}
	readBuf[0]	= (u8)(dAddress<<1) | 0x80;
	if (dSize > 1)
		readBuf[0]	|= 0x01;	/*	burst	*/
	spi_rw(readBuf, readBuf+2, dSize+1);
	memcpy(pbData, readBuf+3, dSize);
#ifdef CONFIG_SND_SOC_YAMAHA_YMU831_DEBUG
	{
		int i;
		char	tmp[20], str[256];
		tmp[0]	= str[0]	= 0;
#ifdef SHOW_REG_ACCESS
		pr_info("read %02X:", (UINT8)dAddress);
#endif
		strcat(str, "rx data:");
		for (i = 0; i < dSize && i < 32; i++) {
			sprintf(tmp, " %02X", pbData[i]);
			strcat(str, tmp);
		}
		strcat(str, "\n");
#ifdef SHOW_REG_ACCESS
		pr_info("%s", str);
#endif
	}
#endif
	if (rx != NULL)
		kfree(rx);
}

void mc_asoc_write_data(
	UINT8 bSlaveAdr,
	const UINT8 *pbData,
	UINT32 dSize)
{
	spi_rw((u8 *)pbData, NULL, dSize);
#ifdef CONFIG_SND_SOC_YAMAHA_YMU831_DEBUG
	{
#ifdef SHOW_REG_ACCESS
		int i;
		char	tmp[20], str[256];
		tmp[0]	= str[0]	= 0;
		strcat(str, "tx data:");
		for (i = 0; i < dSize; i++) {
			if (strlen(str) >= 72) {
				strcat(str, "\n");
				pr_info("%s", str);
				str[0]	= 0;
			}
			sprintf(tmp, " %02X", pbData[i]);
			strcat(str, tmp);
		}
		strcat(str, "\n");
		pr_info("%s", str);
#endif
	}
#endif
}

void mc_asoc_set_codec_ldod(int status)
{
	struct mc_asoc_data	*mc_asoc	= NULL;

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	if (mc_asoc->pdata != NULL) {
		if (mc_asoc->pdata->set_codec_ldod != NULL) {
			(*mc_asoc->pdata->set_codec_ldod)(status);
			if (status == 1) {
				;
				usleep_range(500, 600);
			}
		}
	}
}

static int gpio_codec_en;

static void ymu831_set_ldod(int status)
{
	if (gpio_codec_en)
		gpio_set_value(gpio_codec_en, status);
}

static struct mc_asoc_platform_data *mc_asoc_parse_dt(struct device *dev)
{
	struct mc_asoc_platform_data *pdata;
	struct device_node *node = dev->of_node;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "failed to allocate platform data\n");
		return ERR_PTR(-ENOMEM);
	}

	gpio_codec_en = of_get_named_gpio(node, "codec-en-gpios", 0);
	if (!gpio_is_valid(gpio_codec_en))
		return 0;
	ret = devm_gpio_request_one(dev, gpio_codec_en, GPIOF_OUT_INIT_HIGH, "codec-enable");
	if (ret < 0) {
		dev_err(dev, "failed to request gpio\n");
		return ERR_PTR(-ENOMEM);
	}
	pr_info("gpio: %d, ret: %d\n", gpio_codec_en, ret);

	pdata->set_codec_mclk = NULL;
	pdata->set_codec_ldod = ymu831_set_ldod;
	return pdata;
}

static int mc_asoc_spi_probe(
	struct spi_device *spi)
{
	struct mc_asoc_priv	*mc_asoc_priv;
	struct mc_asoc_data	*mc_asoc;
	int	err;

	TRACE_FUNC();

	mc_asoc_priv	= kzalloc(sizeof(struct mc_asoc_priv), GFP_KERNEL);
	if (!mc_asoc_priv) {
		err	= -ENOMEM;
		goto err_alloc_priv;
	}
	mc_asoc	= &mc_asoc_priv->data;
	mc_asoc->pdata	=
			(struct mc_asoc_platform_data *)spi->dev.platform_data;

	if (spi->dev.of_node) {
		mc_asoc->pdata = mc_asoc_parse_dt(&spi->dev);
		mc_asoc->pdata->irq = spi->irq;
		if (IS_ERR(mc_asoc->pdata)) {
			dev_err(&spi->dev, "failed to parse DT data\n");
			return PTR_ERR(mc_asoc->pdata);
		}
	}

#ifndef FEATURE_MCLK_CONTROL_BY_YMU831
	if (mc_asoc->pdata->set_codec_mclk)
		mc_asoc->pdata->set_codec_mclk(1, 0);
#endif

	mutex_init(&mc_asoc->mutex);
	dev_set_drvdata(&spi->dev, mc_asoc_priv);
	mc_asoc_spi	= spi;

	err	= snd_soc_register_codec(&spi->dev, &mc_asoc_codec_dev,
				mc_asoc_dai, ARRAY_SIZE(mc_asoc_dai));
	if (err < 0)
		goto err_reg_codec;

	goto exit;

err_reg_codec:
	kfree(mc_asoc_priv);
err_alloc_priv:
	dev_err(&spi->dev, "err=%d: failed to probe MC_ASOC\n", err);

exit:
	return err;
}

static int mc_asoc_spi_remove(struct spi_device *spi)
{
	struct mc_asoc_priv	*mc_asoc_priv	= dev_get_drvdata(&spi->dev);
	struct mc_asoc_data	*mc_asoc;

	TRACE_FUNC();

	mc_asoc = &mc_asoc_priv->data;
	mc_asoc->pdata	=
			(struct mc_asoc_platform_data *)spi->dev.platform_data;

#ifndef FEATURE_MCLK_CONTROL_BY_YMU831
	if (mc_asoc->pdata->set_codec_mclk)
		mc_asoc->pdata->set_codec_mclk(0, 0);
#endif

	if (mc_asoc_priv != 0) {
		mutex_destroy(&mc_asoc_priv->data.mutex);
		kfree(mc_asoc_priv);
	}
	return 0;
}

static struct spi_driver	mc_asoc_spi_driver = {
	.driver = {
		.name	= MC_ASOC_HWDEP_ID,
		.owner	= THIS_MODULE,
	},
	.probe	= mc_asoc_spi_probe,
	.remove	= mc_asoc_spi_remove,
};
/*
 * Module init and exit
 */
static int __init ymu831_init(void)
{
	int	err	= 0;

	TRACE_FUNC();

	mutex_init(&mc_asoc_mutex);
	mutex_init(&hsdet_mutex);

	err	= spi_register_driver(&mc_asoc_spi_driver);

	if (err != 0) {
		;
		printk(KERN_ERR "Failed to register MC ASoC Bus driver: %d\n",
									err);
	}
	return err;
}
module_init(ymu831_init);

static void __exit ymu831_exit(void)
{
	spi_unregister_driver(&mc_asoc_spi_driver);
	mutex_destroy(&mc_asoc_mutex);
	mutex_destroy(&hsdet_mutex);
}
module_exit(ymu831_exit);

MODULE_AUTHOR("Yamaha Corporation");
MODULE_DESCRIPTION("Yamaha YMU831 ALSA SoC codec driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MC_ASOC_DRIVER_VERSION);
