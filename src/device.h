/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the device handler.
 *
 * Version:	@(#)device.h	1.0.12	2019/04/18
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef EMU_DEVICE_H
# define EMU_DEVICE_H


#define CONFIG_STRING		0
#define CONFIG_INT		1
#define CONFIG_BINARY		2
#define CONFIG_SELECTION	3
#define CONFIG_MIDI		4
#define CONFIG_FNAME		5
#define CONFIG_SPINNER		6
#define CONFIG_HEX16		7
#define CONFIG_HEX20		8
#define CONFIG_MAC		9


enum {
    DEVICE_ALL = 0x00000000,		/* any/all device */
    DEVICE_UNSTABLE = 0x00000001,	/* unstable device, be cautious */
    DEVICE_PCJR = 0x00000002,		/* requires an IBM PCjr */
    DEVICE_AT = 0x00000004,		/* requires an AT-compatible system */
    DEVICE_PS2 = 0x00000008,		/* requires a PS/1 or PS/2 system */
    DEVICE_ISA = 0x00000100,		/* requires the ISA bus */
    DEVICE_EISA = 0x00000200,		/* requires the EISA bus */
    DEVICE_VLB = 0x00000400,		/* requires the VLB bus */
    DEVICE_PCI = 0x00000800,		/* requires the PCI bus */
    DEVICE_AGP = 0x00001000,		/* requires the AGP bus */
    DEVICE_MCA = 0x00002000,		/* requires the MCA bus */
    DEVICE_CBUS = 0x00004000,		/* requires the C-BUS bus (PC98) */
    DEVICE_VIDTYPE = 0xc0000000,	/* video type bits in device flags */
    DEVICE_ROOT = 0xffffffff		/* machine root device */
};
#define DEVICE_SYS_MASK	0x0006
#define DEVICE_BUS_MASK	0xff00

#define DEVICE_VIDEO(x)	(((x) & 0x03) << 30)
#define DEVICE_VIDEO_GET(x)	(((x) >> 30) & 0x03)


typedef struct {
    const char *description;
    int  value;
} device_config_selection_t;

typedef struct {
    const char *description;
    const char *extensions[5];
} device_config_file_filter_t;

typedef struct {
    int min;
    int max;
    int step;
} device_config_spinner_t;

typedef struct {
    const char *name;
    const char *description;
    int type;
    const char *default_string;
    int default_int;
    device_config_selection_t selection[16];
    device_config_file_filter_t file_filter[16];
    device_config_spinner_t spinner;
} device_config_t;

typedef struct _device_ {
    const char	*name;
    uint32_t	flags;			/* system flags */
    uint32_t	local;			/* flags local to device */

    const wchar_t *path;		/* path to BIOS/ROM file(s) */

    void	*(*init)(const struct _device_ *, void *arg);
    void	(*close)(void *priv);
    void	(*reset)(void *priv);
    void	*u1_reuse;
#define ms_poll		u1_reuse
#define dev_available	u1_reuse
    void	(*speed_changed)(void *priv);
    void	(*force_redraw)(void *priv);
    const void	*u2_reuse;
#define vid_timing	u2_reuse
#define mca_reslist	u2_reuse
#define mach_info	u2_reuse
    const device_config_t *config;
} device_t;


#ifdef __cplusplus
extern "C" {
#endif

extern void		device_reset(void);
#ifdef _DEBUG
extern void		device_dump(void);
#endif
extern const device_t	*device_clone(const device_t *master);
extern void		*device_add(const device_t *);
extern void		*device_add_parent(const device_t *, void *parent);
extern void		device_add_ex(const device_t *, void *priv);
extern void		device_close_all(void);
extern void		device_reset_all(int flags);
extern void		*device_get_priv(const device_t *);
extern const char	*device_get_bus_name(const device_t *);
extern int		device_available(const device_t *);
extern void		device_speed_changed(void);
extern void		device_force_redraw(void);

extern int		device_is_valid(const device_t *, int machine_flags);

extern int		device_get_config_int(const char *name);
extern int		device_get_config_int_ex(const char *s, int dflt_int);
extern int		device_get_config_hex16(const char *name);
extern int		device_get_config_hex20(const char *name);
extern int		device_get_config_mac(const char *name, int dflt_int);
extern void		device_set_config_int(const char *s, int val);
extern void		device_set_config_hex16(const char *s, int val);
extern void		device_set_config_hex20(const char *s, int val);
extern void		device_set_config_mac(const char *s, int val);
extern const char	*device_get_config_string(const char *name);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_DEVICE_H*/
