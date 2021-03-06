/*
 * diag.c - GPIO interface driver for Broadcom boards
 *
 * Copyright (C) 2006 Mike Baker <mbm@openwrt.org>,
 * Copyright (C) 2006-2007 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id$
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kmod.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#ifndef LINUX_2_4
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <net/sock.h>
extern struct sock *uevent_sock;
extern u64 uevent_next_seqnum(void);
#else
#include <linux/tqueue.h>
#define INIT_WORK INIT_TQUEUE
#define schedule_work schedule_task
#define work_struct tq_struct
#endif

#include "gpio.h"
#include "diag.h"
#define getvar(str) (nvram_get(str)?:"")

static int fill_event(struct event_t *);
static unsigned int gpiomask = 0;
module_param(gpiomask, int, 0644);

enum {
	/* Linksys */
	WAP54GV1,
	WAP54GV3,
	WRT54GV1,
	WRT54G,
	WRTSL54GS,
	WRT54G3G,
	WRT350N,
	
	/* ASUS */
	WLHDD,
	WL300G,
	WL500G,
	WL500GD,
	WL500GP,
	WL500W,
	ASUS_4702,
	WL700GE,
	
	/* Buffalo */
	WBR2_G54,
	WHR_G54S,
	WHR_HP_G54,
	WHR2_A54G54,
	WLA2_G54L,
	WZR_G300N,
	WZR_RS_G54,
	WZR_RS_G54HP,
	BUFFALO_UNKNOWN,
	BUFFALO_UNKNOWN_4710,

	/* Siemens */
	SE505V1,
	SE505V2,
	
	/* US Robotics */
	USR5461,

	/* Dell */
	TM2300,

	/* Motorola */
	WE800G,
	WR850GV1,
	WR850GV2V3,
	WR850GP,

	/* Belkin */
	BELKIN_UNKNOWN,

	/* Netgear */
	WGT634U,

	/* Trendware */
	TEW411BRPP,
	
	/* SimpleTech */
	STI_NAS,

	/* D-Link */
	DIR130,
	DIR330,
	DWL3150,
};

static void __init bcm4780_init(void) {
		int pin = 1 << 3;

		/* Enables GPIO 3 that controls HDD and led power on ASUS WL-700gE */
		printk(MODULE_NAME ": Spinning up HDD and enabling leds\n");
		gpio_outen(pin, pin);
		gpio_control(pin, 0);
		gpio_out(pin, pin);

		/* Wait 5s, so the HDD can spin up */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ * 5);
}

static struct platform_t __initdata platforms[] = {
	/* Linksys */
	[WAP54GV1] = {
		.name		= "Linksys WAP54G V1",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 0 },
		},
		.leds		= { 
			{ .name = "diag",	.gpio = 1 << 3 },
			{ .name = "wlan",	.gpio = 1 << 4 },
		},
	},
	[WAP54GV3] = {
		.name		= "Linksys WAP54G V3",
		.buttons	= {
			/* FIXME: verify this */
			{ .name = "reset",	.gpio = 1 << 7 },
			{ .name = "ses",	.gpio = 1 << 0 },
		},
		.leds		= { 
			/* FIXME: diag? */
			{ .name = "ses",	.gpio = 1 << 1 },
		},
	},
	[WRT54GV1] = {
		.name		= "Linksys WRT54G V1.x",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= { 
			{ .name = "diag",	.gpio = 0x13 | GPIO_TYPE_EXTIF, .polarity = NORMAL },
			{ .name = "dmz",	.gpio = 0x12 | GPIO_TYPE_EXTIF, .polarity = NORMAL },
		},
	},
	[WRT54G] = {
		.name		= "Linksys WRT54G/GS/GL",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
			{ .name = "ses",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "dmz",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "ses_white",	.gpio = 1 << 2, .polarity = REVERSE },
			{ .name = "ses_orange",	.gpio = 1 << 3, .polarity = REVERSE },
			{ .name = "wlan",	.gpio = 1 << 0, .polarity = REVERSE },
		},
	},
	[WRTSL54GS] = {
		.name		= "Linksys WRTSL54GS",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
			{ .name = "ses",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "dmz",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "ses_white",	.gpio = 1 << 5, .polarity = REVERSE },
			{ .name = "ses_orange",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},
	[WRT54G3G] = {
		.name		= "Linksys WRT54G3G",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
			{ .name = "3g",		.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "dmz",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "3g_green",	.gpio = 1 << 2, .polarity = NORMAL },
			{ .name = "3g_blue",	.gpio = 1 << 3, .polarity = NORMAL },
			{ .name = "3g_blink",	.gpio = 1 << 5, .polarity = NORMAL },
		},
	},
	[WRT350N] = {
		.name		= "Linksys WRT350N",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
			{ .name = "ses",	.gpio = 1 << 8 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "ses",	.gpio = 1 << 3, .polarity = REVERSE },
		},
	},
	/* Asus */
	[WLHDD] = {
		.name		= "ASUS WL-HDD",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "usb",	.gpio = 1 << 2, .polarity = NORMAL },
		},
	},
	[WL300G] = {
		.name		= "ASUS WL-300g",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 0, .polarity = REVERSE },
		},
	},
	[WL500G] = {
		.name		= "ASUS WL-500g",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 0, .polarity = REVERSE },
		},
	},
	[WL500GD] = {
		.name		= "ASUS WL-500g Deluxe",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 0, .polarity = REVERSE },
		},
	},
	[WL500GP] = {
		.name		= "ASUS WL-500g Premium",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 0 },
			{ .name = "ses",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[WL500W] = {
		.name		= "ASUS WL-500W",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
			{ .name = "ses",	.gpio = 1 << 7 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 5, .polarity = REVERSE },
		},
	},
	[ASUS_4702] = {
		.name		= "ASUS (unknown, BCM4702)",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 6 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 0, .polarity = REVERSE },
		},
	},
	[WL700GE] = {
		.name		= "ASUS WL-700gE",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7 }, // on back, hardwired, always resets device regardless OS state
			{ .name = "ses",	.gpio = 1 << 4 }, // on back, actual name ezsetup
			{ .name = "power",	.gpio = 1 << 0 }, // on front
			{ .name = "copy",	.gpio = 1 << 6 }, // on front
		},
		.leds		= {
#if 0
			// GPIO that controls power led also enables/disables some essential functions
			// - power to HDD
			// - switch leds
			{ .name = "power",	.gpio = 1 << 3, .polarity = NORMAL },  // actual name power
#endif
			{ .name = "diag",	.gpio = 1 << 1, .polarity = REVERSE }, // actual name ready
		},
		.platform_init = bcm4780_init,
	},
	/* Buffalo */
	[WHR_G54S] = {
		.name		= "Buffalo WHR-G54S",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 4 },
			{ .name = "bridge",	.gpio = 1 << 5 },
			{ .name = "ses",	.gpio = 1 << 0 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "internal",	.gpio = 1 << 3, .polarity = REVERSE },
			{ .name = "ses",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "bridge",	.gpio = 1 << 1, .polarity = REVERSE },
			{ .name = "wlan",	.gpio = 1 << 2, .polarity = REVERSE },
		},
	},
	[WBR2_G54] = {
		.name		= "Buffalo WBR2-G54",
		/* FIXME: verify */
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[WHR_HP_G54] = {
		.name		= "Buffalo WHR-HP-G54",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 4 },
			{ .name = "bridge",	.gpio = 1 << 5 },
			{ .name = "ses",	.gpio = 1 << 0 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "internal",	.gpio = 1 << 3, .polarity = REVERSE },
			{ .name = "bridge",	.gpio = 1 << 1, .polarity = REVERSE },
			{ .name = "ses",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "wlan",	.gpio = 1 << 2, .polarity = REVERSE },
		},
	},
	[WHR2_A54G54] = {
		.name		= "Buffalo WHR2-A54G54",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},
	[WLA2_G54L] = {
		.name		= "Buffalo WLA2-G54L",
		/* FIXME: verify */
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[WZR_G300N] = {
		.name		= "Buffalo WZR-G300N",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "bridge",	.gpio = 1 << 1, .polarity = REVERSE },
			{ .name = "ses",	.gpio = 1 << 6, .polarity = REVERSE },
		},
	},
	[WZR_RS_G54] = {
		.name		= "Buffalo WZR-RS-G54",
		.buttons	= {
			{ .name = "ses",	.gpio = 1 << 0 },
			{ .name = "reset",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "ses",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "vpn",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[WZR_RS_G54HP] = {
		.name		= "Buffalo WZR-RS-G54HP",
		.buttons	= {
			{ .name = "ses",	.gpio = 1 << 0 },
			{ .name = "reset",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
			{ .name = "ses",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "vpn",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[BUFFALO_UNKNOWN] = {
		.name		= "Buffalo (unknown)",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	[BUFFALO_UNKNOWN_4710] = {
		.name		= "Buffalo (unknown, BCM4710)",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 4 },
		},
		.leds		= {
			{ .name = "diag",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	/* Siemens */
	[SE505V1] = {
		.name		= "Siemens SE505 V1",
		.buttons	= {
			/* No usable buttons */
		},
		.leds		= {
			{ .name = "dmz",	.gpio = 1 << 4, .polarity = REVERSE },
			{ .name = "wlan",	.gpio = 1 << 3, .polarity = REVERSE },
		},
	},
	[SE505V2] = {
		.name		= "Siemens SE505 V2",
		.buttons	= {
			/* No usable buttons */
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 5, .polarity = REVERSE },
			{ .name = "dmz",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "wlan",	.gpio = 1 << 3, .polarity = REVERSE },
		},
	},
	/* US Robotics */
	[USR5461] = {
		.name		= "U.S. Robotics USR5461",
		.buttons	= {
			/* No usable buttons */
		},
		.leds		= {
			{ .name = "wlan",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "printer",	.gpio = 1 << 1, .polarity = REVERSE },
		},
	},
	/* Dell */
	[TM2300] = {
		.name		= "Dell TrueMobile 2300",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 0 },
		},
		.leds		= {
			{ .name = "wlan",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "power",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},
	/* Motorola */
	[WE800G] = {
		.name		= "Motorola WE800G",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 0 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 4, .polarity = NORMAL },
			{ .name = "diag",	.gpio = 1 << 2, .polarity = REVERSE },
			{ .name = "wlan_amber",	.gpio = 1 << 1, .polarity = NORMAL },
		},
	},
	[WR850GV1] = {
		.name		= "Motorola WR850G V1",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 0 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 4, .polarity = NORMAL },
			{ .name = "diag",	.gpio = 1 << 3, .polarity = REVERSE },
			{ .name = "dmz",	.gpio = 1 << 6, .polarity = NORMAL },
			{ .name = "wlan_red",	.gpio = 1 << 5, .polarity = REVERSE },
			{ .name = "wlan_green",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},
	[WR850GV2V3] = {
		.name		= "Motorola WR850G V2/V3",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 5 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "wlan",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "wan",	.gpio = 1 << 6, .polarity = INPUT },
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},
	[WR850GP] = {
		.name		= "Motorola WR850GP",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 5 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "wlan",	.gpio = 1 << 0, .polarity = REVERSE },
			{ .name = "dmz",	.gpio = 1 << 6, .polarity = REVERSE },
			{ .name = "diag",	.gpio = 1 << 7, .polarity = REVERSE },
		},
	},

	/* Belkin */
	[BELKIN_UNKNOWN] = {
		.name		= "Belkin (unknown)",
		/* FIXME: verify & add detection */
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 5, .polarity = NORMAL },
			{ .name = "wlan",	.gpio = 1 << 3, .polarity = NORMAL },
			{ .name = "connected",	.gpio = 1 << 0, .polarity = NORMAL },
		},
	},
	/* Netgear */
	[WGT634U] = {
		.name		= "Netgear WGT634U",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 2 },
		},
		.leds		= {
			{ .name = "power",	.gpio = 1 << 3, .polarity = NORMAL },
		},
	},
	/* Trendware */
	[TEW411BRPP] = {
		.name           = "Trendware TEW411BRP+",
		.buttons        = {
			{ /* No usable buttons */ },
		},
		.leds           = {
			{ .name = "power",      .gpio = 1 << 7, .polarity = NORMAL },
			{ .name = "wlan",       .gpio = 1 << 1, .polarity = NORMAL },
			{ .name = "bridge",     .gpio = 1 << 6, .polarity = NORMAL },
		},
	},
	/* SimpleTech */
	[STI_NAS] = {
		.name	   = "SimpleTech SimpleShare NAS",
		.buttons	= {
			{ .name = "reset",      .gpio = 1 << 7 }, // on back, hardwired, always resets device regardless OS state
			{ .name = "power",      .gpio = 1 << 0 }, // on back
		},
		.leds	   = {
			{ .name = "diag",       .gpio = 1 << 1, .polarity = REVERSE }, // actual name ready
		},
		.platform_init = bcm4780_init,
	},
	/* D-Link */
	[DIR130] = {
		.name	  = "D-Link DIR-130",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 3},
			{ .name = "reserved",	.gpio = 1 << 7},
		},
		.leds	   = {
			{ .name = "diag", 	.gpio = 1 << 0},
			{ .name = "blue",	.gpio = 1 << 6},
		},
	},
	[DIR330] = {
		.name	  = "D-Link DIR-330",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 3},
			{ .name = "reserved",	.gpio = 1 << 7},
		},
		.leds	   = {
			{ .name = "diag", 	.gpio = 1 << 0},
			{ .name = "usb", 	.gpio = 1 << 4},
			{ .name = "blue",	.gpio = 1 << 6},
		},
	},
	[DWL3150] = {
		.name	= "D-Link DWL-3150",
		.buttons	= {
			{ .name = "reset",	.gpio = 1 << 7},
		},
		.leds	  = {
			{ .name = "diag",	.gpio = 1 << 2},
			{ .name = "status",	.gpio = 1 << 1},
		},
	},
};

static struct platform_t __init *platform_detect(void)
{
	char *boardnum, *boardtype, *buf;

	if (strcmp(getvar("nvram_type"), "cfe") == 0)
		return &platforms[WGT634U];

	/* Look for a model identifier */

	/* Based on "model_name" */
	if (buf = nvram_get("model_name")) {
		if (!strcmp(buf, "DIR-130"))
			return &platforms[DIR130];
		if (!strcmp(buf, "DIR-330"))
			return &platforms[DIR330];
	}

	/* Based on "model_no" */
	if (buf = nvram_get("model_no")) {
		if (!strncmp(buf,"WL700", 5)) /* WL700* */
			return &platforms[WL700GE];
	}

	/* Based on "ModelId" */
	if (buf = nvram_get("ModelId")) {
		if (!strcmp(buf, "WR850GP"))
			return &platforms[WR850GP];
		if (!strcmp(buf,"WX-5565"))
			return &platforms[TM2300];
		if (!strncmp(buf,"WE800G", 6)) /* WE800G* */
			return &platforms[WE800G];
	}

	/* Buffalo */
	if ((buf = (nvram_get("melco_id") ?: nvram_get("buffalo_id")))) {
		/* Buffalo hardware, check id for specific hardware matches */
		if (!strcmp(buf, "29bb0332"))
			return &platforms[WBR2_G54];
		if (!strcmp(buf, "29129"))
			return &platforms[WLA2_G54L];
		if (!strcmp(buf, "30189"))
			return &platforms[WHR_HP_G54];
		if (!strcmp(buf, "30182"))
			return &platforms[WHR_G54S];
		if (!strcmp(buf, "290441dd"))
			return &platforms[WHR2_A54G54];
		if (!strcmp(buf, "31120"))
			return &platforms[WZR_G300N];
		if (!strcmp(buf, "30083"))
			return &platforms[WZR_RS_G54];
		if (!strcmp(buf, "30103"))
			return &platforms[WZR_RS_G54HP];
	}

	/* no easy model number, attempt to guess */
	boardnum = getvar("boardnum");
	boardtype = getvar("boardtype");

	if (strncmp(getvar("pmon_ver"), "CFE", 3) == 0) {
		/* CFE based - newer hardware */
		if (!strcmp(boardnum, "42")) { /* Linksys */
			if (!strcmp(boardtype, "0x478") && !strcmp(getvar("cardbus"), "1"))
				return &platforms[WRT350N];

			if (!strcmp(boardtype, "0x0101") && !strcmp(getvar("boot_ver"), "v3.6"))
				return &platforms[WRT54G3G];

			if (!strcmp(getvar("et1phyaddr"),"5") && !strcmp(getvar("et1mdcport"), "1"))
				return &platforms[WRTSL54GS];
			
			/* default to WRT54G */
			return &platforms[WRT54G];
		}
		
		if (!strcmp(boardnum, "45")) { /* ASUS */
			if (!strcmp(boardtype,"0x042f"))
				return &platforms[WL500GP];
			else if (!strcmp(boardtype,"0x0472"))
				return &platforms[WL500W];
			else
				return &platforms[WL500GD];
		}
		
		if (!strcmp(boardnum, "10496"))
			return &platforms[USR5461];

	} else { /* PMON based - old stuff */
		if ((simple_strtoul(getvar("GemtekPmonVer"), NULL, 0) == 9) &&
			(simple_strtoul(getvar("et0phyaddr"), NULL, 0) == 30)) {
			return &platforms[WR850GV1];
		}
		if (!strncmp(boardtype, "bcm94710dev", 11)) {
			if (!strcmp(boardnum, "42"))
				return &platforms[WRT54GV1];
			if (simple_strtoul(boardnum, NULL, 0) == 2)
				return &platforms[WAP54GV1];
		}
		if (!strncmp(getvar("hardware_version"), "WL500-", 6))
			return &platforms[WL500G];
		if (!strncmp(getvar("hardware_version"), "WL300-", 6)) {
			/* Either WL-300g or WL-HDD, do more extensive checks */
			if ((simple_strtoul(getvar("et0phyaddr"), NULL, 0) == 0) &&
				(simple_strtoul(getvar("et1phyaddr"), NULL, 0) == 1))
				return &platforms[WLHDD];
			if ((simple_strtoul(getvar("et0phyaddr"), NULL, 0) == 0) &&
				(simple_strtoul(getvar("et1phyaddr"), NULL, 0) == 10))
				return &platforms[WL300G];
		}

		/* unknown asus stuff, probably bcm4702 */
		if (!strncmp(boardnum, "asusX", 5))
			return &platforms[ASUS_4702];
	}

	if (buf || !strcmp(boardnum, "00")) {/* probably buffalo */
		if (!strncmp(boardtype, "bcm94710ap", 10))
			return &platforms[BUFFALO_UNKNOWN_4710];
		else
			return &platforms[BUFFALO_UNKNOWN];
	}

	if (!strcmp(getvar("CFEver"), "MotoWRv203") ||
		!strcmp(getvar("MOTO_BOARD_TYPE"), "WR_FEM1")) {

		return &platforms[WR850GV2V3];
	}

	if (!strcmp(boardnum, "44")) {  /* Trendware TEW-411BRP+ */
		return &platforms[TEW411BRPP];
	}

	if (!strncmp(boardnum, "04FN52", 6)) /* SimpleTech SimpleShare */
		return &platforms[STI_NAS];

	if (!strcmp(getvar("boardnum"), "10") && !strcmp(getvar("boardrev"), "0x13")) /* D-Link DWL-3150 */
		return &platforms[DWL3150];

	/* not found */
	return NULL;
}

static void register_buttons(struct button_t *b)
{
	for (; b->name; b++)
		platform.button_mask |= b->gpio;

	platform.button_mask &= ~gpiomask;

	gpio_outen(platform.button_mask, 0);
	gpio_control(platform.button_mask, 0);
	platform.button_polarity = gpio_in() & platform.button_mask;
	gpio_intpolarity(platform.button_mask, platform.button_polarity);
	gpio_intmask(platform.button_mask, platform.button_mask);

	gpio_set_irqenable(1, button_handler);
}

static void unregister_buttons(struct button_t *b)
{
	gpio_intmask(platform.button_mask, 0);

	gpio_set_irqenable(0, button_handler);
}


#ifndef LINUX_2_4
static void add_msg(struct event_t *event, char *msg, int argv)
{
	char *s;

	if (argv)
		return;

	s = skb_put(event->skb, strlen(msg) + 1);
	strcpy(s, msg);
}

static void hotplug_button(struct work_struct *work)
{
	struct event_t *event = container_of(work, struct event_t, wq);
	char *s;

	if (!uevent_sock)
		return;

	event->skb = alloc_skb(2048, GFP_KERNEL);

	s = skb_put(event->skb, strlen(event->action) + 2);
	sprintf(s, "%s@", event->action);
	fill_event(event);

	NETLINK_CB(event->skb).dst_group = 1;
	netlink_broadcast(uevent_sock, event->skb, 0, 1, GFP_KERNEL);

	kfree(event);
}

#else /* !LINUX_2_4 */
static inline char *kzalloc(unsigned int size, unsigned int gfp)
{
	char *p;

	p = kmalloc(size, gfp);
	if (p == NULL)
		return NULL;

	memset(p, 0, size);

	return p;
}

static void add_msg(struct event_t *event, char *msg, int argv)
{
	if (argv)
		event->argv[event->anr++] = event->scratch;
	else
		event->envp[event->enr++] = event->scratch;

	event->scratch += sprintf(event->scratch, "%s", msg) + 1;
}

static void hotplug_button(struct event_t *event)
{
	char *scratch = kzalloc(256, GFP_KERNEL);
	event->scratch = scratch;

	add_msg(event, hotplug_path, 1);
	add_msg(event, "button", 1);
	fill_event(event);
	call_usermodehelper (event->argv[0], event->argv, event->envp);
	kfree(scratch);
	kfree(event);
}
#endif /* !LINUX_2_4 */

static int fill_event (struct event_t *event)
{
	static char buf[128];

	add_msg(event, "HOME=/", 0);
	add_msg(event, "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0);
	add_msg(event, "SUBSYSTEM=button", 0);
	snprintf(buf, 128, "ACTION=%s", event->action);
	add_msg(event, buf, 0);
	snprintf(buf, 128, "BUTTON=%s", event->name);
	add_msg(event, buf, 0);
	snprintf(buf, 128, "SEEN=%ld", event->seen);
	add_msg(event, buf, 0);
#ifndef LINUX_2_4
	snprintf(buf, 128, "SEQNUM=%llu", uevent_next_seqnum());
	add_msg(event, buf, 0);
#endif

	return 0;
}


#ifndef LINUX_2_4
static irqreturn_t button_handler(int irq, void *dev_id)
#else
static irqreturn_t button_handler(int irq, void *dev_id, struct pt_regs *regs)
#endif
{
	struct button_t *b;
	u32 in, changed;

	in = gpio_in() & platform.button_mask;
	gpio_intpolarity(platform.button_mask, in);
	changed = platform.button_polarity ^ in;
	platform.button_polarity = in;

	changed &= ~gpio_outen(0, 0);

	for (b = platform.buttons; b->name; b++) { 
		struct event_t *event;

		if (!(b->gpio & changed)) continue;

		b->pressed ^= 1;

		if ((event = (struct event_t *)kzalloc (sizeof(struct event_t), GFP_ATOMIC))) {
			event->seen = (jiffies - b->seen)/HZ;
			event->name = b->name;
			event->action = b->pressed ? "pressed" : "released";
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
			INIT_WORK(&event->wq, (void *)(void *)hotplug_button);
#else
			INIT_WORK(&event->wq, (void *)(void *)hotplug_button, (void *)event);
#endif
			schedule_work(&event->wq);
		}

		b->seen = jiffies;
	}
	return IRQ_HANDLED;
}

static void register_leds(struct led_t *l)
{
	struct proc_dir_entry *p;
	u32 mask = 0;
	u32 oe_mask = 0;
	u32 val = 0;

	leds = proc_mkdir("led", diag);
	if (!leds) 
		return;

	for(; l->name; l++) {
		if (l->gpio & gpiomask)
			continue;
	
		if (l->gpio & GPIO_TYPE_EXTIF) {
			l->state = 0;
			set_led_extif(l);
		} else {
			if (l->polarity != INPUT) oe_mask != l->gpio;
			mask |= l->gpio;
			val |= (l->polarity == NORMAL)?0:l->gpio;
		}

		if (l->polarity == INPUT) continue;

		if ((p = create_proc_entry(l->name, S_IRUSR, leds))) {
			l->proc.type = PROC_LED;
			l->proc.ptr = l;
			p->data = (void *) &l->proc;
			p->proc_fops = &diag_proc_fops;
		}
	}

	gpio_outen(mask, oe_mask);
	gpio_control(mask, 0);
	gpio_out(mask, val);
}

static void unregister_leds(struct led_t *l)
{
	for(; l->name; l++)
		remove_proc_entry(l->name, leds);

	remove_proc_entry("led", diag);
}

static void set_led_extif(struct led_t *led)
{
	gpio_set_extif(led->gpio, led->state);
}

static void led_flash(unsigned long dummy) {
	struct led_t *l;
	u32 mask = 0;
	u8 extif_blink = 0;

	for (l = platform.leds; l->name; l++) {
		if (l->flash) {
			if (l->gpio & GPIO_TYPE_EXTIF) {
				extif_blink = 1;
				l->state = !l->state;
				set_led_extif(l);
			} else {
				mask |= l->gpio;
			}
		}
	}

	mask &= ~gpiomask;
	if (mask) {
		u32 val = ~gpio_in();

		gpio_outen(mask, mask);
		gpio_control(mask, 0);
		gpio_out(mask, val);
	}
	if (mask || extif_blink) {
		mod_timer(&led_timer, jiffies + FLASH_TIME);
	}
}

static ssize_t diag_proc_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
#ifdef LINUX_2_4
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry *dent = inode->u.generic_ip;
#else
	struct proc_dir_entry *dent = PDE(file->f_dentry->d_inode);
#endif
	char *page;
	int len = 0;
	
	if ((page = kmalloc(1024, GFP_KERNEL)) == NULL)
		return -ENOBUFS;
	
	if (dent->data != NULL) {
		struct prochandler_t *handler = (struct prochandler_t *) dent->data;
		switch (handler->type) {
			case PROC_LED: {
				struct led_t * led = (struct led_t *) handler->ptr;
				if (led->flash) {
					len = sprintf(page, "f\n");
				} else {
					if (led->gpio & GPIO_TYPE_EXTIF) {
						len = sprintf(page, "%d\n", led->state);
					} else {
						u32 in = (gpio_in() & led->gpio ? 1 : 0);
						u8 p = (led->polarity == NORMAL ? 0 : 1);
						len = sprintf(page, "%d\n", ((in ^ p) ? 1 : 0));
					}
				}
				break;
			}
			case PROC_MODEL:
				len = sprintf(page, "%s\n", platform.name);
				break;
			case PROC_GPIOMASK:
				len = sprintf(page, "0x%04x\n", gpiomask);
				break;
		}
	}
	len += 1;

	if (*ppos < len) {
		len = min_t(int, len - *ppos, count);
		if (copy_to_user(buf, (page + *ppos), len)) {
			kfree(page);
			return -EFAULT;
		}
		*ppos += len;
	} else {
		len = 0;
	}

	kfree(page);
	return len;
}


static ssize_t diag_proc_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
#ifdef LINUX_2_4
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry *dent = inode->u.generic_ip;
#else
	struct proc_dir_entry *dent = PDE(file->f_dentry->d_inode);
#endif
	char *page;
	int ret = -EINVAL;

	if ((page = kmalloc(count + 1, GFP_KERNEL)) == NULL)
		return -ENOBUFS;

	if (copy_from_user(page, buf, count)) {
		kfree(page);
		return -EINVAL;
	}
	page[count] = 0;
	
	if (dent->data != NULL) {
		struct prochandler_t *handler = (struct prochandler_t *) dent->data;
		switch (handler->type) {
			case PROC_LED: {
				struct led_t *led = (struct led_t *) handler->ptr;
				int p = (led->polarity == NORMAL ? 0 : 1);
				
				if (page[0] == 'f') {
					led->flash = 1;
					led_flash(0);
				} else {
					led->flash = 0;
					if (led->gpio & GPIO_TYPE_EXTIF) {
						led->state = p ^ ((page[0] == '1') ? 1 : 0);
						set_led_extif(led);
					} else {
						gpio_outen(led->gpio, led->gpio);
						gpio_control(led->gpio, 0);
						gpio_out(led->gpio, ((p ^ (page[0] == '1')) ? led->gpio : 0));
					}
				}
				break;
			}
			case PROC_GPIOMASK:
				gpiomask = simple_strtoul(page, NULL, 0);

				if (platform.buttons) {
					unregister_buttons(platform.buttons);
					register_buttons(platform.buttons);
				}

				if (platform.leds) {
					unregister_leds(platform.leds);
					register_leds(platform.leds);
				}
				break;
		}
		ret = count;
	}

	kfree(page);
	return ret;
}

static int __init diag_init(void)
{
	static struct proc_dir_entry *p;
	static struct platform_t *detected;

	detected = platform_detect();
	if (!detected) {
		printk(MODULE_NAME ": Router model not detected.\n");
		return -ENODEV;
	}
	memcpy(&platform, detected, sizeof(struct platform_t));

	printk(MODULE_NAME ": Detected '%s'\n", platform.name);
	if (platform.platform_init != NULL) {
		platform.platform_init();
	}

	if (!(diag = proc_mkdir("diag", NULL))) {
		printk(MODULE_NAME ": proc_mkdir on /proc/diag failed\n");
		return -EINVAL;
	}

	if ((p = create_proc_entry("model", S_IRUSR, diag))) {
		p->data = (void *) &proc_model;
		p->proc_fops = &diag_proc_fops;
	}

	if ((p = create_proc_entry("gpiomask", S_IRUSR | S_IWUSR, diag))) {
		p->data = (void *) &proc_gpiomask;
		p->proc_fops = &diag_proc_fops;
	}

	if (platform.buttons)
		register_buttons(platform.buttons);

	if (platform.leds)
		register_leds(platform.leds);

	return 0;
}

static void __exit diag_exit(void)
{
	del_timer(&led_timer);

	if (platform.buttons)
		unregister_buttons(platform.buttons);

	if (platform.leds)
		unregister_leds(platform.leds);

	remove_proc_entry("model", diag);
	remove_proc_entry("gpiomask", diag);
	remove_proc_entry("diag", NULL);
}

module_init(diag_init);
module_exit(diag_exit);

MODULE_AUTHOR("Mike Baker, Felix Fietkau / OpenWrt.org");
MODULE_LICENSE("GPL");
