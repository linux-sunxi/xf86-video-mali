/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _MALI_EXA_H_
#define _MALI_EXA_H_

#include "exa.h"
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

/* Change this ioctl according to your specific UMP integration with LCD kernel driver */
#define GET_UMP_SECURE_ID _IOWR('m', 310, unsigned int)

struct mali_info
{
	ScrnInfoPtr pScrn;
	unsigned long fb_phys;
	unsigned char *fb_virt;
	int fb_xres;
	int fb_yres;
	int fd;
};

typedef struct
{
	ump_handle handle;
	unsigned long usize;
} mali_mem_info;

typedef struct
{
	Bool isFrameBuffer;
    Bool gpu_access;
	int refs;
	int bits_per_pixel;
	unsigned long addr;
	mali_mem_info *mem_info;
} PrivPixmap;

extern Bool maliSetupExa( ScreenPtr pScreen, ExaDriverPtr exa, int xres, int yres, unsigned char *virt );

#endif /* _MALI_EXA_H_ */
