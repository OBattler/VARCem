/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the SoundBlaster DSP device.
 *
 *		Jazz sample rates:
 *		  386-33 - 12kHz
 *		  486-33 - 20kHz
 *		  486-50 - 32kHz
 *		  Pentium - 45kHz
 *
 * Version:	@(#)snd_sb_dsp.c	1.0.14	2020/07/14
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#define dbglog sound_card_log
#include "../../emu.h"
#include "../../io.h"
#include "../../timer.h"
#include "../../device.h"
#include "../system/dma.h"
#include "../system/pic.h"
#include "sound.h"
#include "midi.h"
#include "snd_mpu401.h"
#include "snd_filters.h"
#include "snd_sb.h"
#include "snd_sb_dsp.h"

#define ADPCM_4		1
#define ADPCM_26	2
#define ADPCM_2		3

/*The recording safety margin is intended for uneven "len" calls to the get_buffer mixer calls on sound_sb*/
#define SB_DSP_REC_SAFEFTY_MARGIN 4096


extern void	pollsb(priv_t);
extern void	sb_poll_i(priv_t);


static const int sbe2dat[4][9] = {
  {  0x01, -0x02, -0x04,  0x08, -0x10,  0x20,  0x40, -0x80, -106 },
  { -0x01,  0x02, -0x04,  0x08,  0x10, -0x20,  0x40, -0x80,  165 },
  { -0x01,  0x02,  0x04, -0x08,  0x10, -0x20, -0x40,  0x80, -151 },
  {  0x01, -0x02,  0x04, -0x08, -0x10,  0x20, -0x40,  0x80,   90 }
};

/*These tables were 'borrowed' from DOSBox*/
static const int8_t scaleMap4[64] = {
     0,   1,   2,   3,   4,   5,   6,   7,
     0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
     1,   3,   5,   7,   9,  11,  13,  15,
    -1,  -3,  -5,  -7,  -9, -11, -13, -15,
     2,   6,  10,  14,  18,  22,  26,  30,
    -2,  -6, -10, -14, -18, -22, -26, -30,
     4,  12,  20,  28,  36,  44,  52,  60,
    -4, -12, -20, -28, -36, -44, -52, -60
};
static const uint8_t adjustMap4[64] = {
      0, 0, 0, 0, 0, 16, 16, 16,
      0, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0,  0,  0,  0,
    240, 0, 0, 0, 0,  0,  0,  0
};
static const int8_t scaleMap26[40] = {
    0,  1,  2,  3,  0,  -1,  -2,  -3,
    1,  3,  5,  7, -1,  -3,  -5,  -7,
    2,  6, 10, 14, -2,  -6, -10, -14,
    4, 12, 20, 28, -4, -12, -20, -28,
    5, 15, 25, 35, -5, -15, -25, -35
};
static const uint8_t adjustMap26[40] = {
      0, 0, 0, 8,   0, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 0, 248, 0, 0, 0
};
static const int8_t scaleMap2[24] = {
    0,  1,  0,  -1, 1,  3,  -1,  -3,
    2,  6, -2,  -6, 4, 12,  -4, -12,
    8, 24, -8, -24, 6, 48, -16, -48
};
static const uint8_t adjustMap2[24] = {
    0,   4,   0, 4,
    252, 4, 252, 4, 252, 4, 252, 4,
    252, 4, 252, 4, 252, 4, 252, 4,
    252, 0, 252, 0
};

static const char sb16_copyright[] =
		"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
static const uint16_t sb_dsp_versions[] = {
    0, 0, 0x105, 0x200, 0x201, 0x300, 0x302, 0x405, 0x40d
};


static int sb_commands[256] = {
    -1, 2,-1,-1, 1, 2,-1, 0, 1,-1,-1,-1,-1,-1, 2, 1,
     1,-1,-1,-1, 2,-1, 2, 2,-1,-1,-1,-1, 0,-1,-1, 0,
     0,-1,-1,-1, 2,-1,-1,-1,-1,-1,-1,-1, 0,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     1, 2, 2,-1,-1,-1,-1,-1, 2,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1, 2, 2, 2, 2,-1,-1,-1,-1,-1, 0,-1, 0,
     2, 2,-1,-1,-1,-1,-1,-1, 2, 2,-1,-1,-1,-1,-1,-1,
     0,-1,-1,-1,-1,-1,-1,-1, 0,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     0, 0,-1, 0, 0, 0, 0,-1, 0, 0, 0,-1,-1,-1,-1,-1,
     1, 0, 1, 0, 1,-1,-1, 0, 0,-1,-1,-1,-1,-1,-1,-1,
    -1,-1, 0,-1,-1,-1,-1,-1,-1, 1, 2,-1,-1,-1,-1, 0
};
static mpu_t *mpu;

float low_fir_sb16_coef[SB16_NCoef];


static inline double
sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
recalc_sb16_filter(int playback_freq)
{
    /*Cutoff frequency = playback / 2*/
    float fC = (float)((float)playback_freq / (float)2.0) / (float)48000.0;
    float gain;
    int n;
    double w, h;

    for (n = 0; n < SB16_NCoef; n++) {
	/*Blackman window*/
	w = 0.42 - (0.5 * cos((2.0*n*M_PI)/(double)(SB16_NCoef-1))) + (0.08 * cos((4.0*n*M_PI)/(double)(SB16_NCoef-1)));
	/*Sinc filter*/
	h = sinc(2.0 * fC * ((double)n - ((double)(SB16_NCoef-1) / 2.0)));

	/*Create windowed-sinc filter*/
	low_fir_sb16_coef[n] = (float)(w * h);
    }

    low_fir_sb16_coef[(SB16_NCoef - 1) / 2] = 1.0;

    gain = 0.0;
    for (n = 0; n < SB16_NCoef; n++)
	gain += low_fir_sb16_coef[n];

    /*Normalise filter, to produce unity gain*/
    for (n = 0; n < SB16_NCoef; n++)
	low_fir_sb16_coef[n] /= gain;
}


void
sb_irq(sb_dsp_t *dsp, int irq8)
{
    DBGLOG(1, "IRQ %i %02X\n",irq8,pic.mask);
    if (irq8)
	dsp->sb_irq8  = 1;
    else
	dsp->sb_irq16 = 1;
    picint(1 << dsp->sb_irqnum);
}

void
sb_irqc(sb_dsp_t *dsp, int irq8)
{
    if (irq8)
	dsp->sb_irq8  = 0;
    else
	dsp->sb_irq16 = 0;
    picintc(1 << dsp->sb_irqnum);
}

void
sb_dsp_reset(sb_dsp_t *dsp)
{
    dsp->sbenable = dsp->sb_enable_i = 0;
    dsp->sb_command = 0;

    dsp->sb_8_length = 0xffff;
    dsp->sb_8_autolen = 0xffff;

    sb_irqc(dsp, 0);
    sb_irqc(dsp, 1);
    dsp->sb_16_pause = 0;
    dsp->sb_read_wp = dsp->sb_read_rp = 0;
    dsp->sb_data_stat = -1;
    dsp->sb_speaker = 0;
    dsp->sb_pausetime = -1LL;
    dsp->sbe2 = 0xAA;
    dsp->sbe2count = 0;

    dsp->sbreset = 0;
    dsp->sbenable = dsp->sb_enable_i = dsp->sb_count_i = 0;

    dsp->record_pos_read=0;
    dsp->record_pos_write=SB_DSP_REC_SAFEFTY_MARGIN;

    picintc(1 << dsp->sb_irqnum);

    dsp->asp_data_len = 0;
}

void
sb_doreset(sb_dsp_t *dsp)
{
    int c;

    sb_dsp_reset(dsp);

    if (dsp->sb_type==SB16)
	sb_commands[8] =  1;
    else
	sb_commands[8] = -1;

    for (c = 0; c < 256; c++)
	dsp->sb_asp_regs[c] = 0;

    dsp->sb_asp_regs[5] = 0x01;
    dsp->sb_asp_regs[9] = 0xf8;
}

void
sb_dsp_speed_changed(sb_dsp_t *dsp)
{
    if (dsp->sb_timeo < 256LL)
	dsp->sblatcho = TIMER_USEC * (256LL - dsp->sb_timeo);
    else
	dsp->sblatcho = (tmrval_t)(TIMER_USEC * (1000000.0f / (float)(dsp->sb_timeo - 256LL)));

    if (dsp->sb_timei < 256LL)
	dsp->sblatchi = TIMER_USEC * (256LL - dsp->sb_timei);
    else
	dsp->sblatchi = (tmrval_t)(TIMER_USEC * (1000000.0f / (float)(dsp->sb_timei - 256LL)));
}

void
sb_add_data(sb_dsp_t *dsp, uint8_t v)
{
    dsp->sb_read_data[dsp->sb_read_wp++] = v;
    dsp->sb_read_wp &= 0xff;
}

void
sb_start_dma(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    dsp->sb_pausetime = -1LL;

    if (dma8) {
	dsp->sb_8_length = len;
	dsp->sb_8_format = format;
	dsp->sb_8_autoinit = autoinit;
	dsp->sb_8_pause = 0;
	dsp->sb_8_enable = 1;
	if (dsp->sb_16_enable && dsp->sb_16_output)
		dsp->sb_16_enable = 0;
	dsp->sb_8_output = 1;
	timer_process();
	dsp->sbenable = dsp->sb_8_enable;
	timer_update_outstanding();
	dsp->sbleftright = 0;
	dsp->sbdacpos = 0;
    }
    else {
	dsp->sb_16_length = len;
	dsp->sb_16_format = format;
	dsp->sb_16_autoinit = autoinit;
	dsp->sb_16_pause = 0;
	dsp->sb_16_enable = 1;
	if (dsp->sb_8_enable && dsp->sb_8_output)
		dsp->sb_8_enable = 0;
	dsp->sb_16_output = 1;
	timer_process();
	dsp->sbenable = dsp->sb_16_enable;
	timer_update_outstanding();
    }
}

void
sb_start_dma_i(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    if (dma8) {
	dsp->sb_8_length = len;
	dsp->sb_8_format = format;
	dsp->sb_8_autoinit = autoinit;
	dsp->sb_8_pause = 0;
	dsp->sb_8_enable = 1;
	if (dsp->sb_16_enable && !dsp->sb_16_output)
		dsp->sb_16_enable = 0;
	dsp->sb_8_output = 0;
	timer_process();
	dsp->sb_enable_i = dsp->sb_8_enable;
	timer_update_outstanding();
    }
    else {
	dsp->sb_16_length = len;
	dsp->sb_16_format = format;
	dsp->sb_16_autoinit = autoinit;
	dsp->sb_16_pause = 0;
	dsp->sb_16_enable = 1;
	if (dsp->sb_8_enable && !dsp->sb_8_output)
		dsp->sb_8_enable = 0;
	dsp->sb_16_output = 0;
	timer_process();
	dsp->sb_enable_i = dsp->sb_16_enable;
	timer_update_outstanding();
    }

    memset(dsp->record_buffer,0,sizeof(dsp->record_buffer));
}

int
sb_8_read_dma(sb_dsp_t *dsp)
{
    return dma_channel_read(dsp->sb_8_dmanum);
}

void
sb_8_write_dma(sb_dsp_t *dsp, uint8_t val)
{
    dma_channel_write(dsp->sb_8_dmanum, val);
}

int
sb_16_read_dma(sb_dsp_t *dsp)
{
    return dma_channel_read(dsp->sb_16_dmanum);
}
int
sb_16_write_dma(sb_dsp_t *dsp, uint16_t val)
{
    int ret = dma_channel_write(dsp->sb_16_dmanum, val);
    return (ret == DMA_NODATA);
}

void
sb_dsp_setirq(sb_dsp_t *dsp, int irq)
{
    dsp->sb_irqnum = irq;
}

void
sb_dsp_setdma8(sb_dsp_t *dsp, int dma)
{
    dsp->sb_8_dmanum = dma;
}

void
sb_dsp_setdma16(sb_dsp_t *dsp, int dma)
{
    dsp->sb_16_dmanum = dma;
}
void
sb_exec_command(sb_dsp_t *dsp)
{
    int temp,c;

    DBGLOG(1, "sb_exec_command : SB command %02X\n", dsp->sb_command);

    switch (dsp->sb_command) {
	case 0x01: /*???*/
		if (dsp->sb_type >= SB16)
			dsp->asp_data_len = dsp->sb_data[0] + (dsp->sb_data[1] << 8) + 1;
                break;

	case 0x03: /*ASP status*/
		sb_add_data(dsp, 0);
		break;

	case 0x10: /*8-bit direct mode*/
		sb_dsp_update(dsp);
		dsp->sbdat = dsp->sbdatl = dsp->sbdatr = (dsp->sb_data[0] ^ 0x80) << 8;
		break;

	case 0x14: /*8-bit single cycle DMA output*/
		sb_start_dma(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
		break;

	case 0x17: /*2-bit ADPCM output with reference*/
		dsp->sbref = sb_8_read_dma(dsp);
		dsp->sbstep = 0;

	case 0x16: /*2-bit ADPCM output*/
		sb_start_dma(dsp, 1, 0, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
		dsp->sbdat2 = sb_8_read_dma(dsp);
		dsp->sb_8_length--;
		if (dsp->sb_command == 0x17)
			dsp->sb_8_length--;
		break;

	case 0x1C: /*8-bit autoinit DMA output*/
		if (dsp->sb_type >= SB15)
			sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
		break;

	case 0x1F: /*2-bit ADPCM autoinit output*/
		if (dsp->sb_type >= SB15) {
			sb_start_dma(dsp, 1, 1, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
			dsp->sbdat2 = sb_8_read_dma(dsp);
			dsp->sb_8_length--;
		}
		break;

	case 0x20: /*8-bit direct input*/
		sb_add_data(dsp, (dsp->record_buffer[dsp->record_pos_read]>>8) ^0x80);
		/*Due to the current implementation, I need to emulate a samplerate, even if this
		* mode does not imply such samplerate. Position is increased in sb_poll_i*/
		if (dsp->sb_enable_i==0) {
			dsp->sb_timei = 256LL - 22LL;
			dsp->sblatchi = TIMER_USEC * 22LL;
			temp = 1000000 / 22;
			dsp->sb_freq = temp;
			timer_process();
			dsp->sb_enable_i = 1;
			timer_update_outstanding();
		}
		break;

	case 0x24: /*8-bit single cycle DMA input*/
		sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
	break;

	case 0x2C: /*8-bit autoinit DMA input*/
		if (dsp->sb_type >= SB15)
			sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
		break;

	case 0x30: case 0x31:
		break;

	case 0x34:
		dsp->uart_midi = 1;
		dsp->uart_irq = 0;
		break;

	case 0x35:
		dsp->uart_midi = 1;
		dsp->uart_irq = 1;
		break;

	case 0x36: case 0x37:
		break;

	case 0x38:
		dsp->onebyte_midi = 1;
		break;

	case 0x40: /*Set time constant*/
		dsp->sb_timei = dsp->sb_timeo = dsp->sb_data[0];
		dsp->sblatcho = dsp->sblatchi = TIMER_USEC * (256 - dsp->sb_data[0]);
		temp = 256 - dsp->sb_data[0];
		temp = 1000000 / temp;
		DBGLOG(1, "Sample rate - %ihz (%i)\n",temp, dsp->sblatcho);
		if (dsp->sb_freq != temp && dsp->sb_type >= SB16)
			recalc_sb16_filter(temp);
		dsp->sb_freq = temp;
                break;
                
	case 0x41: case 0x42: /*Set output/input sampling rate*/
		if (dsp->sb_type >= SB16) {
			dsp->sblatcho = (tmrval_t)(TIMER_USEC * (1000000.0f / (float)(dsp->sb_data[1] + (dsp->sb_data[0] << 8))));
			DBGLOG(1, "Sample rate - %ihz (%i)\n",dsp->sb_data[1]+(dsp->sb_data[0]<<8), dsp->sblatcho);
			temp = dsp->sb_freq;
			dsp->sb_freq = dsp->sb_data[1] + (dsp->sb_data[0] << 8);
			dsp->sb_timeo = 256LL + dsp->sb_freq;
			dsp->sblatchi = dsp->sblatcho;
			dsp->sb_timei = dsp->sb_timeo;
			if (dsp->sb_freq != temp && dsp->sb_type >= SB16)
				recalc_sb16_filter(dsp->sb_freq);
		}
		break;
		
	case 0x48: /*Set DSP block transfer size*/
		dsp->sb_8_autolen = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
		break;
		
	case 0x75: /*4-bit ADPCM output with reference*/
		dsp->sbref = sb_8_read_dma(dsp);
		dsp->sbstep = 0;
		/* Fall through */
		
	case 0x74: /*4-bit ADPCM output*/
		sb_start_dma(dsp, 1, 0, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
		dsp->sbdat2 = sb_8_read_dma(dsp);
		dsp->sb_8_length--;
		if (dsp->sb_command == 0x75)
			dsp->sb_8_length--;
		break;
		
	case 0x77: /*2.6-bit ADPCM output with reference*/
		dsp->sbref = sb_8_read_dma(dsp);
		dsp->sbstep = 0;
		/* Fall through */
		
	case 0x76: /*2.6-bit ADPCM output*/
		sb_start_dma(dsp, 1, 0, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
		dsp->sbdat2 = sb_8_read_dma(dsp);
		dsp->sb_8_length--;
		if (dsp->sb_command == 0x77)
			dsp->sb_8_length--;
		break;
		
	case 0x7D: /*4-bit ADPCM autoinit output*/
                if (dsp->sb_type >= SB15) {
			sb_start_dma(dsp, 1, 1, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
			dsp->sbdat2 = sb_8_read_dma(dsp);
			dsp->sb_8_length--;
		}
		break;
		
	case 0x7F: /*2.6-bit ADPCM autoinit output*/
		if (dsp->sb_type >= SB15) {
			sb_start_dma(dsp, 1, 1, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
			dsp->sbdat2 = sb_8_read_dma(dsp);
			dsp->sb_8_length--;
		}
		break;
                
	case 0x80: /*Pause DAC*/
		dsp->sb_pausetime = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
		timer_process();
		dsp->sbenable = 1;
		timer_update_outstanding();
		break;
		
	case 0x90: /*High speed 8-bit autoinit DMA output*/
		if (dsp->sb_type >= SB2)
			sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
		break;
                
	case 0x91: /*High speed 8-bit single cycle DMA output*/
		if (dsp->sb_type >= SB2)
			sb_start_dma(dsp, 1, 0, 0, dsp->sb_8_autolen);
		break;
                
	case 0x98: /*High speed 8-bit autoinit DMA input*/
		if (dsp->sb_type >= SB2)
			sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_8_autolen);
		break;
                
	case 0x99: /*High speed 8-bit single cycle DMA input*/
		if (dsp->sb_type >= SB2)
			sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_8_autolen);
		break;
		
	case 0xA0: case 0xA8: /*Set input mode to mono / stereo */
		if (dsp->sb_type < SB2 || dsp->sb_type > SBPRO2) 
			break;
		/* TODO: Implement. 3.xx-only command. */
		break;
		
	case 0xB0: case 0xB1: case 0xB2: case 0xB3:
	case 0xB4: case 0xB5: case 0xB6: case 0xB7: /*16-bit DMA output*/
		if (dsp->sb_type >= SB16) {
			sb_start_dma(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
			dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
                }
		break;
		
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF: /*16-bit DMA input*/
		if (dsp->sb_type >= SB16) {
			sb_start_dma_i(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
			dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
                }
		break;
		
	case 0xC0: case 0xC1: case 0xC2: case 0xC3:
	case 0xC4: case 0xC5: case 0xC6: case 0xC7: /*8-bit DMA output*/
		if (dsp->sb_type >= SB16) {
			sb_start_dma(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
			dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
                }
		break;
		
	case 0xC8: case 0xC9: case 0xCA: case 0xCB:
	case 0xCC: case 0xCD: case 0xCE: case 0xCF: /*8-bit DMA input*/
		if (dsp->sb_type >= SB16) {
			sb_start_dma_i(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
			dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
                }
		break;
		
	case 0xD0: /*Pause 8-bit DMA*/
		dsp->sb_8_pause = 1;
		break;
		
	case 0xD1: /*Speaker on*/
		if (dsp->sb_type < SB15 )
			dsp->sb_8_pause = 1;
		else if ( dsp->sb_type < SB16 )
			dsp->muted = 0;
		dsp->sb_speaker = 1;
		break;
		
	case 0xD3: /*Speaker off*/
		if (dsp->sb_type < SB15 )
			dsp->sb_8_pause = 1;
		else if ( dsp->sb_type < SB16 )
                        dsp->muted = 1;
		dsp->sb_speaker = 0;
		break;
		
	case 0xD4: /*Continue 8-bit DMA*/
		dsp->sb_8_pause = 0;
		break;
		
	case 0xD5: /*Pause 16-bit DMA*/
		if (dsp->sb_type >= SB16)
			dsp->sb_16_pause = 1;
		break;
		
	case 0xD6: /*Continue 16-bit DMA*/
		if (dsp->sb_type >= SB16)
			dsp->sb_16_pause = 0;
		break;
		
	case 0xD8: /*Get speaker status*/
		sb_add_data(dsp, dsp->sb_speaker ? 0xff : 0);
		break;
		
	case 0xD9: /*Exit 16-bit auto-init mode*/
		if (dsp->sb_type >= SB16)
			dsp->sb_16_autoinit = 0;
		break;
	
	case 0xDA: /*Exit 8-bit auto-init mode*/
		dsp->sb_8_autoinit = 0;
		break;
                
	case 0xE0: /*DSP identification*/
		sb_add_data(dsp, ~dsp->sb_data[0]);
		break;
                
	case 0xE1: /*Get DSP version*/
		sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] >> 8);
		sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] & 0xff);
		break;
		
	case 0xE2: /*Stupid ID/protection*/
		for (c = 0; c < 8; c++) {
			if (dsp->sb_data[0] & (1 << c)) 
				dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][c];
                }
		dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][8];
		dsp->sbe2count++;
		sb_8_write_dma(dsp, dsp->sbe2);
		break;
		
	case 0xE3: /*DSP copyright*/
		if (dsp->sb_type >= SB16) {
			c = 0;
			while (sb16_copyright[c])
				sb_add_data(dsp, sb16_copyright[c++]);
			sb_add_data(dsp, 0);
		}
		break;
		
	case 0xE4: /*Write test register*/
		dsp->sb_test = dsp->sb_data[0];
		break;
		
	case 0xE8: /*Read test register*/
		sb_add_data(dsp, dsp->sb_test);
		break;
		
	case 0xF2: /*Trigger 8-bit IRQ*/
		DBGLOG(1, "Trigger IRQ\n");
		sb_irq(dsp, 1);
		break;
		
	case 0xF3: /*Trigger 16-bit IRQ*/
		DBGLOG(1, "Trigger IRQ\n");
		sb_irq(dsp, 0);
		break;
		
	case 0xE7: case 0xFA: 
		break;
                
	case 0x07: case 0xFF: /*No, that's not how you program auto-init DMA*/
		break;
		
	case 0x08: /*ASP get version*/
		if (dsp->sb_type >= SB16)
			sb_add_data(dsp, 0x18);
		break;
		
	case 0x0E: /*ASP set register*/
		if (dsp->sb_type >= SB16)
			dsp->sb_asp_regs[dsp->sb_data[0]] = dsp->sb_data[1];
		break;
		
	case 0x0F: /*ASP get register*/
		if (dsp->sb_type >= SB16)
			sb_add_data(dsp, dsp->sb_asp_regs[dsp->sb_data[0]]);
		break;
                
	case 0xF8:
		if (dsp->sb_type < SB16)
			sb_add_data(dsp, 0);
		break;
		
	case 0xF9:
		if (dsp->sb_type >= SB16) {
			if (dsp->sb_data[0] == 0x0e)
				sb_add_data(dsp, 0xff);
		else if (dsp->sb_data[0] == 0x0f) 
			sb_add_data(dsp, 0x07);
		else if (dsp->sb_data[0] == 0x37) 
			sb_add_data(dsp, 0x38);
		else
			sb_add_data(dsp, 0x00);
                }
	/*Fall Through*/
	case 0x04: case 0x05:
		break;

                /*TODO: Some more data about the DSP registeres
                 * http://the.earth.li/~tfm/oldpage/sb_dsp.html
                 * http://www.synchrondata.com/pheaven/www/area19.htm
                 * http://www.dcee.net/Files/Programm/Sound/
 0E3h           DSP Copyright                                       SBPro2???
 0F0h           Sine Generator                                      SB
 0F1h           DSP Auxiliary Status (Obsolete)                     SB-Pro2
 0F2h           IRQ Request, 8-bit                                  SB
 0F3h           IRQ Request, 16-bit                                 SB16
 0FBh           DSP Status                                          SB16
 0FCh           DSP Auxiliary Status                                SB16
 0FDh           DSP Command Status                                  SB16
                 */

        }
}

void 
sb_write(uint16_t a, uint8_t v, priv_t priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *)priv;

    switch (a&0xF) {
	case 6: /*Reset*/
		if (!(v & 1) && (dsp->sbreset & 1)) {
			sb_dsp_reset(dsp);
			sb_add_data(dsp, 0xAA);
		}
		dsp->sbreset = v;
		return;
		
	case 0xC: /*Command/data write*/
		if (dsp->uart_midi || dsp->onebyte_midi) {
			midi_write(v);
			dsp->onebyte_midi = 0;
			return;
		}
		timer_process();
		dsp->wb_time = TIMER_USEC * 1LL;
		dsp->wb_full = 1LL;
		timer_update_outstanding();
		if (dsp->asp_data_len) {
			DBGLOG(1, "ASP data %i\n", dsp->asp_data_len);
			dsp->asp_data_len--;
			if (!dsp->asp_data_len)
				sb_add_data(dsp, 0);
			return;
		}
		if (dsp->sb_data_stat == -1) {
			dsp->sb_command = v;
			if (v == 0x01)
				sb_add_data(dsp, 0);
			dsp->sb_data_stat++;
		}
		else
			dsp->sb_data[dsp->sb_data_stat++] = v;
		if (dsp->sb_data_stat == sb_commands[dsp->sb_command] || sb_commands[dsp->sb_command] == -1) {
			sb_exec_command(dsp);
			dsp->sb_data_stat = -1;
		}
		break;
    }
}

uint8_t 
sb_read(uint16_t a, priv_t priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *)priv;
    uint8_t ret = 0x00;
    
    switch (a & 0xf) {
	case 0xA: /*Read data*/
		if (mpu && dsp->uart_midi)
			ret = MPU401_ReadData(mpu);
		else {
			dsp->sbreaddat = dsp->sb_read_data[dsp->sb_read_rp];
			if (dsp->sb_read_rp != dsp->sb_read_wp) {
				dsp->sb_read_rp++;
				dsp->sb_read_rp &= 0xFF;
			}
			return dsp->sbreaddat;
		}
		break;
		
	case 0xC: /*Write data ready*/
		if (dsp->sb_8_enable || dsp->sb_type >= SB16)
			dsp->busy_count = (dsp->busy_count + 1) & 3;
		else
			dsp->busy_count = 0;
		if (dsp->wb_full || (dsp->busy_count & 2)) {
			dsp->wb_full = dsp->wb_time;
                        return 0xff;
                }
		ret = 0x7f;
		break;
		
	case 0xE: /*Read data ready*/
		picintc(1 << dsp->sb_irqnum);
		dsp->sb_irq8 = dsp->sb_irq16 = 0;
		ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x7f : 0xff;
		break;
		
	case 0xF: /*16-bit ack*/
		dsp->sb_irq16 = 0;
		if (!dsp->sb_irq8) 
			picintc(1 << dsp->sb_irqnum);
		ret = 0xff;
		break;
	}
        return ret;
}

static void 
sb_wb_clear(priv_t priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *)priv;

    dsp->wb_time = 0LL;
}

void 
sb_dsp_set_mpu(mpu_t *src_mpu)
{
    mpu = src_mpu;
}

void 
sb_dsp_init(sb_dsp_t *dsp, int type)
{
    dsp->sb_type = type;

    // Default values. Use sb_dsp_setxxx() methods to change.
    dsp->sb_irqnum = 7;
    dsp->sb_8_dmanum = 1;
    dsp->sb_16_dmanum = 5;
    mpu = NULL;

    sb_doreset(dsp);

    timer_add(pollsb, dsp, &dsp->sbcount, &dsp->sbenable);
    timer_add(sb_poll_i, dsp, &dsp->sb_count_i, &dsp->sb_enable_i);
    timer_add(sb_wb_clear, dsp, &dsp->wb_time, &dsp->wb_time);

    /*Initialise SB16 filter to same cutoff as 8-bit SBs (3.2 kHz). This will be recalculated when
          a set frequency command is sent.*/
    recalc_sb16_filter(3200*2);
}

void sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr)
{
    DBGLOG(1, "sb_dsp_setaddr : %04X\n", addr);
    if (dsp->sb_addr != 0) {
	io_removehandler(dsp->sb_addr + 6,   0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
	io_removehandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
    dsp->sb_addr = addr;
    if (dsp->sb_addr != 0) {
	io_sethandler(dsp->sb_addr + 6,   0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
	io_sethandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
}

void 
sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo)
{
    dsp->stereo = stereo;
}

void 
pollsb(priv_t priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *)priv;
    int tempi,ref;
    int data[2];

    dsp->sbcount += dsp->sblatcho;
    if (dsp->sb_8_enable && !dsp->sb_8_pause && dsp->sb_pausetime < 0 && dsp->sb_8_output) {
	sb_dsp_update(dsp);
		
    switch (dsp->sb_8_format) {
	
	case 0x00: /*Mono unsigned*/
		data[0] = sb_8_read_dma(dsp);
		/*Needed to prevent clicking in Worms, which programs the DSP to
		auto-init DMA but programs the DMA controller to single cycle*/
		if (data[0] == DMA_NODATA)
			break;
		dsp->sbdat = (data[0] ^ 0x80) << 8;
		if (dsp->sb_type >= SBPRO && dsp->sb_type < SB16 && dsp->stereo) {
			if (dsp->sbleftright) 
				dsp->sbdatl = dsp->sbdat;
			else
				dsp->sbdatr = dsp->sbdat;
			dsp->sbleftright = !dsp->sbleftright;
		}
		else
			dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
		dsp->sb_8_length--;
		break;
			
	case 0x10: /*Mono signed*/
		data[0] = sb_8_read_dma(dsp);
		if (data[0] == DMA_NODATA)
			break;
		dsp->sbdat = data[0] << 8;
		if (dsp->sb_type >= SBPRO && dsp->sb_type < SB16 && dsp->stereo) {
			if (dsp->sbleftright) 
				dsp->sbdatl = dsp->sbdat;
			else
				dsp->sbdatr = dsp->sbdat;
			dsp->sbleftright = !dsp->sbleftright;
			}
		else
			dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
		dsp->sb_8_length--;
		break;
		
	case 0x20: /*Stereo unsigned*/
		data[0] = sb_8_read_dma(dsp);
		data[1] = sb_8_read_dma(dsp);
		if (data[0] == DMA_NODATA || data[1] == DMA_NODATA)
			break;
		dsp->sbdatl = (data[0] ^ 0x80) << 8;
		dsp->sbdatr = (data[1] ^ 0x80) << 8;
		dsp->sb_8_length -= 2;
		break;
		
	case 0x30: /*Stereo signed*/
		data[0] = sb_8_read_dma(dsp);
		data[1] = sb_8_read_dma(dsp);
		if (data[0] == DMA_NODATA || data[1] == DMA_NODATA)
			break;
		dsp->sbdatl = data[0] << 8;
		dsp->sbdatr = data[1] << 8;
		dsp->sb_8_length -= 2;
		break;

	case ADPCM_4:
		if (dsp->sbdacpos)
			tempi = (dsp->sbdat2 & 0xF) + dsp->sbstep;
		else
			tempi = (dsp->sbdat2 >> 4)  + dsp->sbstep;
		if (tempi < 0)
			tempi = 0;
		if (tempi > 63)
			tempi = 63;

		ref = dsp->sbref + scaleMap4[tempi];
		if (ref > 0xff)
			dsp->sbref = 0xff;
		else if (ref < 0x00) 
			dsp->sbref = 0x00;
		else 
			dsp->sbref = ref;

		dsp->sbstep = (dsp->sbstep + adjustMap4[tempi]) & 0xff;

		dsp->sbdat = (dsp->sbref ^ 0x80) << 8;

		dsp->sbdacpos++;
		if (dsp->sbdacpos >= 2) {
			dsp->sbdacpos = 0;
			dsp->sbdat2 = sb_8_read_dma(dsp);
			dsp->sb_8_length--;
		}

		if (dsp->sb_type >= SBPRO && dsp->sb_type < SB16 && dsp->stereo) {
			if (dsp->sbleftright)
				dsp->sbdatl = dsp->sbdat;
			else
				dsp->sbdatr = dsp->sbdat;
			dsp->sbleftright = !dsp->sbleftright;
		}
		else
			dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
		break;

	case ADPCM_26:
		if (!dsp->sbdacpos)
			tempi = (dsp->sbdat2 >> 5) + dsp->sbstep;
		else if (dsp->sbdacpos == 1) 
			tempi = ((dsp->sbdat2 >> 2) & 7) + dsp->sbstep;
		else                         
			tempi = ((dsp->sbdat2 << 1) & 7) + dsp->sbstep;

		if (tempi < 0)
			tempi = 0;
		if (tempi > 39)
			tempi = 39;

		ref = dsp->sbref + scaleMap26[tempi];
		if (ref > 0xff)
			dsp->sbref = 0xff;
		else if (ref < 0x00) 
			dsp->sbref = 0x00;
		else dsp->sbref = ref;
		
		dsp->sbstep = (dsp->sbstep + adjustMap26[tempi]) & 0xff;

		dsp->sbdat = (dsp->sbref ^ 0x80) << 8;

		dsp->sbdacpos++;
		if (dsp->sbdacpos>=3) {
			dsp->sbdacpos = 0;
			dsp->sbdat2 = sb_8_read_dma(dsp);
			dsp->sb_8_length--;
		}

		if (dsp->sb_type >= SBPRO && dsp->sb_type < SB16 && dsp->stereo) {
			if (dsp->sbleftright) 
				dsp->sbdatl = dsp->sbdat;
			else
				dsp->sbdatr = dsp->sbdat;
			dsp->sbleftright = !dsp->sbleftright;
			}
		else
			dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
		break;

	case ADPCM_2:
		tempi = ((dsp->sbdat2 >> ((3 - dsp->sbdacpos) * 2)) & 3) + dsp->sbstep;
		if (tempi < 0)
			tempi = 0;
		if (tempi > 23) 
			tempi = 23;

		ref = dsp->sbref + scaleMap2[tempi];
		if (ref > 0xff)
			dsp->sbref = 0xff;
		else if (ref < 0x00)
			dsp->sbref = 0x00;
		else dsp->sbref = ref;
		dsp->sbstep = (dsp->sbstep + adjustMap2[tempi]) & 0xff;

		dsp->sbdat = (dsp->sbref ^ 0x80) << 8;

		dsp->sbdacpos++;
		if (dsp->sbdacpos >= 4) {
			dsp->sbdacpos = 0;
			dsp->sbdat2 = sb_8_read_dma(dsp);
		}

		if (dsp->sb_type >= SBPRO && dsp->sb_type < SB16 && dsp->stereo) {
			if (dsp->sbleftright)
				dsp->sbdatl = dsp->sbdat;
			else
				dsp->sbdatr = dsp->sbdat;
			dsp->sbleftright = !dsp->sbleftright;
		}
		else
			dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
		break;

//                        default:
                                //fatal("Unrecognised SB 8-bit format %02X\n",sb_8_format);
    }

    if (dsp->sb_8_length < 0) {
	if (dsp->sb_8_autoinit) 
		dsp->sb_8_length = dsp->sb_8_autolen;
	else
		dsp->sbenable = dsp->sb_8_enable = 0;
	sb_irq(dsp, 1);
	}
    }
    
    if (dsp->sb_16_enable && !dsp->sb_16_pause && dsp->sb_pausetime < 0LL && dsp->sb_16_output) {

	sb_dsp_update(dsp);

	switch (dsp->sb_16_format) {
		case 0x00: /*Mono unsigned*/
			data[0] = sb_16_read_dma(dsp);
			if (data[0] == DMA_NODATA)
				break;
			dsp->sbdatl = dsp->sbdatr = data[0] ^ 0x8000;
			dsp->sb_16_length--;
			break;
                        
		case 0x10: /*Mono signed*/
			data[0] = sb_16_read_dma(dsp);
			if (data[0] == DMA_NODATA)
				break;
			dsp->sbdatl = dsp->sbdatr = data[0];
			dsp->sb_16_length--;
			break;
			
		case 0x20: /*Stereo unsigned*/
			data[0] = sb_16_read_dma(dsp);
			data[1] = sb_16_read_dma(dsp);
			if (data[0] == DMA_NODATA || data[1] == DMA_NODATA)
				break;
			dsp->sbdatl = data[0] ^ 0x8000;
			dsp->sbdatr = data[1] ^ 0x8000;
			dsp->sb_16_length -= 2;
			break;
			
		case 0x30: /*Stereo signed*/
			data[0] = sb_16_read_dma(dsp);
			data[1] = sb_16_read_dma(dsp);
			if (data[0] == DMA_NODATA || data[1] == DMA_NODATA)
				break;
			dsp->sbdatl = data[0];
			dsp->sbdatr = data[1];
			dsp->sb_16_length -= 2;
			break;
	}

	if (dsp->sb_16_length < 0) {
		DBGLOG(1, "16DMA over %i\n",dsp->sb_16_autoinit);
		if (dsp->sb_16_autoinit)
			dsp->sb_16_length = dsp->sb_16_autolen;
		else
			dsp->sbenable = dsp->sb_16_enable = 0;
		sb_irq(dsp, 0);
	}
    }
    
    if (dsp->sb_pausetime > -1LL) {
	dsp->sb_pausetime--;
	if (dsp->sb_pausetime < 0LL) {
			sb_irq(dsp, 1);
			dsp->sbenable = dsp->sb_8_enable;
			DBGLOG(1, "SB pause over\n");
	}
    }
}

void 
sb_poll_i(priv_t priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *)priv;
    int processed=0;
     
    dsp->sb_count_i += dsp->sblatchi;
    
    if (dsp->sb_8_enable && !dsp->sb_8_pause && dsp->sb_pausetime < 0 && !dsp->sb_8_output) {
	switch (dsp->sb_8_format) {
		case 0x00: /*Mono unsigned As the manual says, only the left channel is recorded*/
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read]>>8) ^0x80);
			dsp->sb_8_length--;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x10: /*Mono signed As the manual says, only the left channel is recorded*/
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read]>>8));
			dsp->sb_8_length--;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x20: /*Stereo unsigned*/
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read]>>8)^0x80);
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read+1]>>8)^0x80);
			dsp->sb_8_length -= 2;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x30: /*Stereo signed*/
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read]>>8));
			sb_8_write_dma(dsp, (dsp->record_buffer[dsp->record_pos_read+1]>>8));
			dsp->sb_8_length -= 2;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
	}

	if (dsp->sb_8_length < 0) {
		if (dsp->sb_8_autoinit)
			dsp->sb_8_length = dsp->sb_8_autolen;
		else
			dsp->sbenable = dsp->sb_8_enable = 0;
		sb_irq(dsp, 1);
	}
	processed=1;
    }
    
    if (dsp->sb_16_enable && !dsp->sb_16_pause && dsp->sb_pausetime < 0LL && !dsp->sb_16_output) {

	switch (dsp->sb_16_format) {
                        
		case 0x00: /*Unsigned mono. As the manual says, only the left channel is recorded*/
			if (sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read]^0x8000))
				return;
			dsp->sb_16_length--;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x10: /*Signed mono. As the manual says, only the left channel is recorded*/
			if (sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read]))
                                return;
			dsp->sb_16_length--;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x20: /*Unsigned stereo*/
			if (sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read]^0x8000))
				return;
			sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read+1]^0x8000);
			dsp->sb_16_length -= 2;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
			
		case 0x30: /*Signed stereo*/
			if (sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read]))
				return;
			sb_16_write_dma(dsp, dsp->record_buffer[dsp->record_pos_read+1]);
			dsp->sb_16_length -= 2;
			dsp->record_pos_read+=2;
			dsp->record_pos_read&=0xFFFF;
			break;
	}

	if (dsp->sb_16_length < 0) {
		if (dsp->sb_16_autoinit)
			dsp->sb_16_length = dsp->sb_16_autolen;
		else
			dsp->sbenable = dsp->sb_16_enable = 0;
		sb_irq(dsp, 0);
	}
	processed=1;
    }
        
    /* Assume this is direct mode */
    if (!processed) {
	dsp->record_pos_read+=2;
	dsp->record_pos_read&=0xFFFF;
    }
}

void 
sb_dsp_update(sb_dsp_t *dsp)
{
    if (dsp->muted) {
	dsp->sbdatl=0;
	dsp->sbdatr=0;
    }
    
    for (; dsp->pos < sound_pos_global; dsp->pos++) {
	dsp->buffer[dsp->pos*2] = dsp->sbdatl;
	dsp->buffer[dsp->pos*2 + 1] = dsp->sbdatr;
    }
}

void 
sb_dsp_close(sb_dsp_t *dsp)
{
}
