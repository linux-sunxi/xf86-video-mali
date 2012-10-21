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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"

#include "mali_def.h"
#include "mali_fbdev.h"
#include "mali_exa.h"
#include "mali_dri.h"

#define  FBIO_WAITFORVSYNC    _IOW('F', 0x20, __u32)

typedef struct
{
	PixmapPtr pPixmap;
	unsigned int attachment;
	Bool isPageFlipped;
} MaliDRI2BufferPrivateRec, *MaliDRI2BufferPrivatePtr;

static DRI2Buffer2Ptr MaliDRI2CreateBuffer( DrawablePtr pDraw, unsigned int attachment, unsigned int format )
{
	ScreenPtr pScreen = pDraw->pScreen;
	DRI2Buffer2Ptr buffer;
	MaliDRI2BufferPrivatePtr privates;
	PixmapPtr pPixmap;
	PrivPixmap *privPixmap;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);
	
	buffer = calloc(1, sizeof *buffer);
	if ( NULL == buffer ) return NULL;

	privates = calloc(1, sizeof *privates);
	if ( NULL == privates ) 
	{
		free( buffer );
		return NULL;
	}

	/* initialize privates info to default values */
	privates->pPixmap = NULL;
	privates->attachment = attachment;
	privates->isPageFlipped = FALSE;

	/* initialize buffer info to default values */
	buffer->attachment = attachment;
	buffer->driverPrivate = privates;
	buffer->format = format;
	buffer->flags = 0;

	if ( DRI2CanFlip( pDraw ) && fPtr->use_pageflipping && DRAWABLE_PIXMAP != pDraw->type)
	{
		ump_secure_id ump_id = UMP_INVALID_SECURE_ID;

		if ( (fPtr->fb_lcd_var.yres*2) > fPtr->fb_lcd_var.yres_virtual )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] lcd driver does not have enough virtual y resolution. Need: %i Have: %i\n", 
			            __FUNCTION__, __LINE__, fPtr->fb_lcd_var.yres*2, fPtr->fb_lcd_var.yres_virtual );
			return NULL;
		}

		(void)ioctl(fPtr->fb_lcd_fd, GET_UMP_SECURE_ID, &ump_id );

		if ( UMP_INVALID_SECURE_ID == ump_id )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to retrieve UMP memory handle for flipping\n", __FUNCTION__, __LINE__ );

			free( buffer );
			free( privates );
			return NULL;
		}

		buffer->name = ump_id;

		if ( DRI2BufferBackLeft == attachment )
		{
			/* Use the "flags" attribute in order to provide EGL with enough information to offset the provided UMP memory for this buffer
			 * Flags will only be set in cases where it is possible to do page flipping instead of offscreen rendering with EXA copy
			 * Offset is set to the second virtual screen, in y direction
			 * Example: 
			 * Physical resolution: 1366 x 768 
			 * Virtual resolution:  1366 x 1536
			 * Offset: 768
			 */
			buffer->flags = (fPtr->fb_lcd_var.xres_virtual * fPtr->fb_lcd_var.bits_per_pixel/8) * fPtr->fb_lcd_var.yres;
		}

		if ( ioctl( fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] failed in FBIOGET_VSCREENINFO\n", __FUNCTION__, __LINE__ );
			free( buffer ),
			free( privates );
			TRACE_EXIT();
			return NULL;
		}

		fPtr->fb_lcd_var.yoffset = fPtr->fb_lcd_var.yres;
		fPtr->fb_lcd_var.activate = FB_ACTIVATE_NOW;

		if ( ioctl( fPtr->fb_lcd_fd, FBIOPUT_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] failed in FBIOPUT_VSCREENINFO\n", __FUNCTION__, __LINE__ );
			free( buffer );
			free( privates );
			return NULL;
		}

		if ( DRAWABLE_PIXMAP == pDraw->type ) pPixmap = (PixmapPtr)pDraw;
		else pPixmap = pScreen->GetWindowPixmap( (WindowPtr) pDraw );

		privates->isPageFlipped = TRUE;

		pPixmap->refcnt++;

		buffer->pitch = pPixmap->devKind;
		if ( 0 == buffer->pitch )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] Warning: Autocalculating pitch\n", __FUNCTION__, __LINE__ );
			buffer->pitch = ( (pPixmap->drawable.width*pPixmap->drawable.bitsPerPixel) + 7 ) / 8;
		}
		buffer->cpp = pPixmap->drawable.bitsPerPixel / 8;
		xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] Enabled Page Flipping (pitch: %i flags: %i width: %i height: %i)\n", __FUNCTION__, __LINE__, buffer->pitch, buffer->flags, pPixmap->drawable.width, pPixmap->drawable.height );

		ioctl( fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var );
	}
	else
	{
		if ( DRI2BufferFrontLeft == attachment )
		{
			if ( DRAWABLE_PIXMAP == pDraw->type ) pPixmap = (PixmapPtr)pDraw;
			else pPixmap = pScreen->GetWindowPixmap( (WindowPtr) pDraw );

			pPixmap->refcnt++;
		}
		else
		{
			/* create a new pixmap for the offscreen data */
			pPixmap = (*pScreen->CreatePixmap)( pScreen, pDraw->width, pDraw->height, (format != 0) ? format : pDraw->depth, 0 );
			if ( NULL == pPixmap )
			{
				xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "[%s:%d] unable to allocate pixmap\n", __FUNCTION__, __LINE__ );
				free( buffer );
				free( privates );
				return NULL;
			}

			exaMoveInPixmap(pPixmap);
		}

		privates->pPixmap = pPixmap;
		privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate( pPixmap );

		if (!privPixmap->isFrameBuffer)
			privPixmap->gpu_access = TRUE;
	
		/* if no CPU access the memory before dri2GetBuffers, no need to sync the CPU cache */
		if ( privPixmap->addr )
		{
			ump_cpu_msync_now( privPixmap->mem_info->handle, UMP_MSYNC_CLEAN, (void *)privPixmap->addr, privPixmap->mem_info->usize );
		}

		buffer->pitch = pPixmap->devKind;
		buffer->cpp = pPixmap->drawable.bitsPerPixel / 8;
		buffer->name = ump_secure_id_get( privPixmap->mem_info->handle );
	}

	return buffer;
}

static void MaliDRI2DestroyBuffer( DrawablePtr pDraw, DRI2Buffer2Ptr buffer )
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);

	if ( NULL != buffer )
	{
		MaliDRI2BufferPrivatePtr private = buffer->driverPrivate;
		ScreenPtr pScreen = pDraw->pScreen;

		if ( NULL != private )
		{
			if ( TRUE == private->isPageFlipped )
			{
				if ( ioctl( fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
				{
					xf86DrvMsg( pScrn->scrnIndex, X_WARNING, "[%s:%d] failed in FBIOGET_VSCREENINFO\n", __FUNCTION__, __LINE__ );
				}

				fPtr->fb_lcd_var.yoffset = 0;
				fPtr->fb_lcd_var.activate = FB_ACTIVATE_NOW;

				if ( ioctl( fPtr->fb_lcd_fd, FBIOPUT_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
				{
					xf86DrvMsg( pScrn->scrnIndex, X_WARNING, "[%s:%d] failed in FBIOPUT_VSCREENINFO\n", __FUNCTION__, __LINE__ );
				}
			}
			if( NULL != private->pPixmap ) (*pScreen->DestroyPixmap)(private->pPixmap);
		}

		free( private );
		free( buffer );
	}
}

static void MaliDRI2CopyRegion( DrawablePtr pDraw, RegionPtr pRegion, DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer )
{
	GCPtr pGC;
	RegionPtr copyRegion;
	ScreenPtr pScreen = pDraw->pScreen;
	MaliDRI2BufferPrivatePtr srcPrivate = pSrcBuffer->driverPrivate;
	MaliDRI2BufferPrivatePtr dstPrivate = pDstBuffer->driverPrivate;
	DrawablePtr src = (srcPrivate->attachment == DRI2BufferFrontLeft) ? pDraw : &srcPrivate->pPixmap->drawable;
	DrawablePtr dst = (dstPrivate->attachment == DRI2BufferFrontLeft) ? pDraw : &dstPrivate->pPixmap->drawable;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);

	if ( TRUE == dstPrivate->isPageFlipped && TRUE == srcPrivate->isPageFlipped )
	{
		fPtr->fb_lcd_var.yoffset = (fPtr->fb_lcd_var.yoffset + fPtr->fb_lcd_var.yres) % (fPtr->fb_lcd_var.yres*2);


#if 1
		if ( ioctl( fPtr->fb_lcd_fd, FBIOPUT_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_WARNING, "[%s:%d] failed in FBIOPUT_VSCREENINFO (offset: %i)\n", __FUNCTION__, __LINE__, fPtr->fb_lcd_var.yoffset );
		}

		if ( fPtr->use_pageflipping_vsync )
		{
			fPtr->fb_lcd_var.activate = FB_ACTIVATE_VBL;
			if ( ioctl( fPtr->fb_lcd_fd, FBIO_WAITFORVSYNC, 0 ) < 0 )
			{
				xf86DrvMsg( pScrn->scrnIndex, X_WARNING, "[%s:%d] failed in FBIO_WAITFORVSYNC\n", __FUNCTION__, __LINE__ );
			}
		}
#else
		if ( ioctl( fPtr->fb_lcd_fd, FBIOPAN_DISPLAY, &fPtr->fb_lcd_var ) < 0 )
		{
			xf86DrvMsg( pScrn->scrnIndex, X_WARNING, "[%s:%d] failed in FBIOPAN_DISPLAY (offset: %i)\n", __FUNCTION__, __LINE__, fPtr->fb_lcd_var.yoffset );
		}
#endif
		ioctl( fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var );

		return;
	}

	if ( DRI2BufferFakeFrontLeft == srcPrivate->attachment || DRI2BufferFakeFrontLeft == dstPrivate->attachment ) return;

	pGC = GetScratchGC(pDraw->depth, pScreen);

	copyRegion = REGION_CREATE( pScreen, NULL, 0 );
	REGION_COPY( pScreen, copyRegion, pRegion );
	(*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0 );
	ValidateGC( dst, pGC );
	(*pGC->ops->CopyArea)( src, dst, pGC, 0, 0, pDraw->width, pDraw->height, 0, 0 );

	FreeScratchGC(pGC);
}

Bool MaliDRI2ScreenInit( ScreenPtr pScreen )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);
	DRI2InfoRec info;
	int dri2_major = 2, dri2_minor = 0, i;
	struct stat sbuf;
	dev_t d;
	char *p;

	if ( xf86LoaderCheckSymbol( "DRI2Version") ) DRI2Version( &dri2_major, &dri2_minor );

	if ( dri2_minor < 1 )
	{
		xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "%s requires DRI2 module version 1.1.0 or later\n", __func__);
		return FALSE;
	}

	xf86DrvMsg( pScrn->scrnIndex, X_INFO, "DRI2 version: %i.%i\n", dri2_major, dri2_minor );


	/* extract deviceName */
	info.fd = fPtr->drm_fd;
	fstat( info.fd, &sbuf );
	d = sbuf.st_rdev;

	p = fPtr->deviceName;
	for ( i=0; i<DRM_MAX_MINOR; i++ )
	{
		sprintf( p, DRM_DEV_NAME, DRM_DIR_NAME, i );
		if ( stat( p, &sbuf ) == 0 && sbuf.st_rdev == d ) break;
	}

	if ( i == DRM_MAX_MINOR )
	{
		xf86DrvMsg( pScrn->scrnIndex, X_ERROR, "%s failed to open drm device\n", __func__ );
		return FALSE;
	}


	info.driverName = "Mali DRI2";
	info.deviceName = p;

#if DRI2INFOREC_VERSION == 1
	info.version = 1;
	info.CreateBuffers = MaliDRI2CreateBuffers;
	info.DestroyBuffers = MaliDRI2DestroyBuffers;
#elif DRI2INFOREC_VERSION == 2
	info.version = 2;
	info.CreateBuffer = MaliDRI2CreateBuffer;
	info.DestroyBuffer = MaliDRI2DestroyBuffer;
#else
	info.version = 3;
	info.CreateBuffer = MaliDRI2CreateBuffer;
	info.DestroyBuffer = MaliDRI2DestroyBuffer;
#endif

	info.CopyRegion = MaliDRI2CopyRegion;

	if ( FALSE == DRI2ScreenInit( pScreen, &info ) ) return FALSE;

	return TRUE;
}

void MaliDRI2CloseScreen( ScreenPtr pScreen )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);

	DRI2CloseScreen( pScreen );

	fPtr->dri_render = DRI_NONE;
}
