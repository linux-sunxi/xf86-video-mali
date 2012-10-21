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
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mali_def.h"
#include "mali_fbdev.h"
#include "mali_exa.h"

static struct mali_info mi;

#define MALI_EXA_FUNC(s) exa->s = mali ## s
#define MALI_ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))

static int fd_fbdev = -1;

static Bool maliPrepareSolid( PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg )
{
	IGNORE( pPixmap );
	IGNORE( alu );
	IGNORE( planemask );
	IGNORE( fg );

 	return FALSE;
}

static void maliSolid( PixmapPtr pPixmap, int x1, int y1, int x2, int y2 )
{
	IGNORE( pPixmap );
	IGNORE( x1 );
	IGNORE( y1 );
	IGNORE( x2 );
	IGNORE( y2 );
}

static void maliDoneSolid( PixmapPtr pPixmap )
{
	IGNORE( pPixmap );
}

static Bool maliPrepareCopy( PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap, int xdir, int ydir, int alu, Pixel planemask )
{
	IGNORE( pSrcPixmap );
	IGNORE( pDstPixmap );
	IGNORE( xdir );
	IGNORE( ydir );
	IGNORE( alu );
	IGNORE( planemask );

	return FALSE;
}

static void maliCopy( PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY, int width, int height )
{
	IGNORE( pDstPixmap );
	IGNORE( srcX );
	IGNORE( srcY );
	IGNORE( dstX );
	IGNORE( dstY );
	IGNORE( width );
	IGNORE( height );
}

static void maliDoneCopy( PixmapPtr pDstPixmap )
{
	IGNORE( pDstPixmap );
}

static void maliWaitMarker( ScreenPtr pScreen, int marker )
{
	IGNORE( pScreen );
	IGNORE( marker );
}

static void* maliCreatePixmap(ScreenPtr pScreen, int size, int align )
{
	PrivPixmap *privPixmap = calloc(1, sizeof(PrivPixmap));

	IGNORE( pScreen );
	IGNORE( size );
	IGNORE( align );
	privPixmap->bits_per_pixel = 0;

	return privPixmap;
}

static void maliDestroyPixmap(ScreenPtr pScreen, void *driverPriv )
{
	PrivPixmap *privPixmap = (PrivPixmap *)driverPriv;

	IGNORE( pScreen );
	if ( NULL != privPixmap->mem_info )
	{
		ump_reference_release(privPixmap->mem_info->handle);
		free( privPixmap->mem_info );
		privPixmap->mem_info = NULL;
		free( privPixmap );
	}
}

static Bool maliModifyPixmapHeader(PixmapPtr pPixmap, int width, int height, int depth, int bitsPerPixel, int devKind, pointer pPixData)
{
	unsigned int size;
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPixmap);
	mali_mem_info *mem_info;

	if (!pPixmap) 
	{
		return FALSE;
	}

	miModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);

	if (pPixData == mi.fb_virt) 
	{
		ump_secure_id ump_id = UMP_INVALID_SECURE_ID;

		privPixmap->isFrameBuffer = TRUE;

		mem_info = privPixmap->mem_info;
		if ( mem_info ) 
		{
			return TRUE;
		}

		/* create new mem_info for the on-screen buffer */
		mem_info = calloc(1, sizeof(*mem_info));
		if (!mem_info) 
		{
			xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate for memory metadata\n", __FUNCTION__, __LINE__);
			return FALSE;
		}

		/* get the secure ID for the framebuffer */
		(void)ioctl( fd_fbdev, GET_UMP_SECURE_ID, &ump_id );

		if ( UMP_INVALID_SECURE_ID == ump_id)
		{
			free( mem_info );
			privPixmap->mem_info = NULL;
			xf86DrvMsg( mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] UMP failed to retrieve secure id\n", __FUNCTION__, __LINE__);
			return FALSE;
		}

		mem_info->handle = ump_handle_create_from_secure_id( ump_id );
		if ( UMP_INVALID_MEMORY_HANDLE == mem_info->handle )
		{
			xf86DrvMsg( mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] UMP failed to create handle from secure id\n", __FUNCTION__, __LINE__);
			free( mem_info );
			privPixmap->mem_info = NULL;
			return FALSE;
		}

		size = exaGetPixmapPitch(pPixmap) * pPixmap->drawable.height;
		mem_info->usize = size;

		privPixmap->mem_info = mem_info;
		if( bitsPerPixel != 0 ) privPixmap->bits_per_pixel = bitsPerPixel;

		return TRUE;
	}
	else
	{
		privPixmap->isFrameBuffer = FALSE;
	}

	if ( pPixData ) 
	{
		if ( privPixmap->mem_info != NULL ) 
		{
			return TRUE;
		}

		return FALSE;
	}

	pPixmap->devKind = ( (pPixmap->drawable.width*pPixmap->drawable.bitsPerPixel) + 7 ) / 8;
	pPixmap->devKind = MALI_ALIGN( pPixmap->devKind, 8 );

	size = exaGetPixmapPitch(pPixmap) * pPixmap->drawable.height;

	/* allocate pixmap data */
	mem_info = privPixmap->mem_info;

	if ( mem_info && mem_info->usize == size ) 
	{
		return TRUE;
	}

	if ( mem_info && mem_info->usize != 0 )
	{
		ump_reference_release(mem_info->handle);
		mem_info->handle = NULL;
		memset(privPixmap, 0, sizeof(*privPixmap));

		return TRUE;
	}

	if (!size) 
	{
		return TRUE;
	}

	if ( NULL == mem_info )
	{
		mem_info = calloc(1, sizeof(*mem_info));
		if (!mem_info) 
		{
			xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate memory metadata\n", __FUNCTION__, __LINE__);
			return FALSE;
		}
	}

	mem_info->handle = ump_ref_drv_allocate( size, UMP_REF_DRV_CONSTRAINT_USE_CACHE );
	if ( UMP_INVALID_MEMORY_HANDLE == mem_info->handle )
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate UMP memory (%i bytes)\n", __FUNCTION__, __LINE__, size);
		return FALSE;
	}

	mem_info->usize = size;
	privPixmap->gpu_access = FALSE;
	privPixmap->mem_info = mem_info;
	privPixmap->mem_info->usize = size;
	privPixmap->bits_per_pixel = 16;

	return TRUE;
}

static Bool maliPixmapIsOffscreen( PixmapPtr pPix )
{
	ScreenPtr pScreen = pPix->drawable.pScreen;
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);

	if (pScreen->GetScreenPixmap(pScreen) == pPix ) 
	{
		return TRUE;
	}

	if ( privPixmap )
	{
		return pPix->devPrivate.ptr ? FALSE : TRUE;
	}

	return FALSE;
}

static Bool maliPrepareAccess(PixmapPtr pPix, int index)
{
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);
	mali_mem_info *mem_info;

	IGNORE( index );

	if ( !privPixmap ) 
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] Failed to get private pixmap data\n", __FUNCTION__, __LINE__);
		return FALSE;
	}

	mem_info = privPixmap->mem_info;
	if ( NULL != mem_info ) 
	{
		if ( privPixmap->refs == 0 ) 
		{
			privPixmap->addr = (unsigned long)ump_mapped_pointer_get( mem_info->handle );
		}
	}
	else
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] No mem_info on pixmap\n", __FUNCTION__, __LINE__);
		return FALSE;
	}

	pPix->devPrivate.ptr = (void *)(privPixmap->addr);
	if ( NULL == pPix->devPrivate.ptr ) 
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] cpu address not set\n", __FUNCTION__, __LINE__);
		return FALSE;
	}

	if ( privPixmap->gpu_access )
	{
		ump_cpu_msync_now( mem_info->handle, UMP_MSYNC_CLEAN_AND_INVALIDATE, (void *)privPixmap->addr, mem_info->usize );
	}

	privPixmap->refs++;

	return TRUE;
}

static void maliFinishAccess(PixmapPtr pPix, int index)
{
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);
	mali_mem_info *mem_info;

	IGNORE( index );

	if ( !privPixmap ) 
	{
		return;
	}

	if ( !pPix ) 
	{
		return;
	}

	mem_info = privPixmap->mem_info;

	if ( privPixmap->gpu_access )
	{
		ump_cpu_msync_now( mem_info->handle, UMP_MSYNC_CLEAN, (void *)privPixmap->addr, mem_info->usize );
	}

	if ( !privPixmap->isFrameBuffer ) 
	{
		if ( privPixmap->refs == 1 )
		{
			if ( NULL != mem_info ) ump_mapped_pointer_release( mem_info->handle );
		}
	}

	pPix->devPrivate.ptr = NULL;
	privPixmap->refs--;
}

static Bool maliCheckComposite( int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture )
{
	IGNORE( op );
	IGNORE( pSrcPicture );
	IGNORE( pMaskPicture );
	IGNORE( pDstPicture );

	return FALSE;
}

static Bool maliPrepareComposite( int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture, PixmapPtr pSrcPixmap, PixmapPtr pMask, PixmapPtr pDstPixmap )
{
	IGNORE( op );
	IGNORE( pSrcPicture );
	IGNORE( pMaskPicture );
	IGNORE( pDstPicture );
	IGNORE( pSrcPixmap );
	IGNORE( pMask );
	IGNORE( pDstPixmap );

	return FALSE;
}

static void maliComposite( PixmapPtr pDstPixmap, int srcX, int srcY, int maskX, int maskY, int dstX, int dstY, int width, int height)
{
	IGNORE( pDstPixmap );
	IGNORE( srcX );
	IGNORE( srcY );
	IGNORE( maskX );
	IGNORE( maskY );
	IGNORE( dstX );
	IGNORE( dstY );
	IGNORE( width );
	IGNORE( height );
}

static void maliDoneComposite( PixmapPtr pDst )
{
	IGNORE( pDst );
}


static void maliDumpInfo()
{
	xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "XRES: %i YRES: %i PHYS: 0x%x VIRT: 0x%x\n", mi.fb_xres, mi.fb_yres, (int)mi.fb_phys, (int)mi.fb_virt);
}

Bool maliSetupExa( ScreenPtr pScreen, ExaDriverPtr exa, int xres, int yres, unsigned char *virt )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);

	if ( NULL == exa ) return FALSE;

	memset(&mi, 0, sizeof(mi));
	mi.pScrn = pScrn;
	mi.fb_xres = xres;
	mi.fb_yres = yres;
	mi.fb_phys = pScrn->memPhysBase;
	mi.fb_virt = virt;

	TRACE_ENTER();

	maliDumpInfo();

	exa->exa_major = 2;
	exa->exa_minor = 0;
	exa->memoryBase = fPtr->fbmem;
	exa->maxX = fPtr->fb_lcd_var.xres_virtual;
	exa->maxY = fPtr->fb_lcd_var.yres_virtual;
	exa->flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->offScreenBase = (fPtr->fb_lcd_fix.line_length*fPtr->fb_lcd_var.yres);
	exa->memorySize = fPtr->fb_lcd_fix.smem_len;
	exa->pixmapOffsetAlign = 4096;
	exa->pixmapPitchAlign = 8;

	fd_fbdev = fPtr->fb_lcd_fd;

	maliDumpInfo();

	MALI_EXA_FUNC(PrepareSolid);
	MALI_EXA_FUNC(Solid);
	MALI_EXA_FUNC(DoneSolid);

	MALI_EXA_FUNC(PrepareCopy);
	MALI_EXA_FUNC(Copy);
	MALI_EXA_FUNC(DoneCopy);

	MALI_EXA_FUNC(CheckComposite);
	MALI_EXA_FUNC(PrepareComposite);
	MALI_EXA_FUNC(Composite);
	MALI_EXA_FUNC(DoneComposite);

	MALI_EXA_FUNC(WaitMarker);

	MALI_EXA_FUNC(CreatePixmap);
	MALI_EXA_FUNC(DestroyPixmap);
	MALI_EXA_FUNC(ModifyPixmapHeader);
	MALI_EXA_FUNC(PixmapIsOffscreen);

	MALI_EXA_FUNC(PrepareAccess);
	MALI_EXA_FUNC(FinishAccess);

	if ( UMP_OK != ump_open() )
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to open UMP subsystem\n", __FUNCTION__, __LINE__);
		TRACE_EXIT();
		return FALSE;
	}


	xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "Mali EXA driver is loaded successfully\n");
	TRACE_EXIT();

	return TRUE;
}
