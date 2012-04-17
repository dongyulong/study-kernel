/*
 * V4L2 Driver for PXA camera host
 *
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-sg.h>
#include <media/soc_camera.h>

#include <linux/videodev2.h>

#include <mach/dma.h>
#include <mach/camera.h>

#define PXA_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)
#define PXA_CAM_DRV_NAME "pxa27x-camera"

/* Camera Interface */
#define CICR0		0x0000
#define CICR1		0x0004
#define CICR2		0x0008
#define CICR3		0x000C
#define CICR4		0x0010
#define CISR		0x0014
#define CIFR		0x0018
#define CITOR		0x001C
#define CIBR0		0x0028
#define CIBR1		0x0030
#define CIBR2		0x0038

#define CICR0_DMAEN	(1 << 31)	/* DMA request enable */
#define CICR0_PAR_EN	(1 << 30)	/* Parity enable */
#define CICR0_SL_CAP_EN	(1 << 29)	/* Capture enable for slave mode */
#define CICR0_ENB	(1 << 28)	/* Camera interface enable */
#define CICR0_DIS	(1 << 27)	/* Camera interface disable */
#define CICR0_SIM	(0x7 << 24)	/* Sensor interface mode mask */
#define CICR0_TOM	(1 << 9)	/* Time-out mask */
#define CICR0_RDAVM	(1 << 8)	/* Receive-data-available mask */
#define CICR0_FEM	(1 << 7)	/* FIFO-empty mask */
#define CICR0_EOLM	(1 << 6)	/* End-of-line mask */
#define CICR0_PERRM	(1 << 5)	/* Parity-error mask */
#define CICR0_QDM	(1 << 4)	/* Quick-disable mask */
#define CICR0_CDM	(1 << 3)	/* Disable-done mask */
#define CICR0_SOFM	(1 << 2)	/* Start-of-frame mask */
#define CICR0_EOFM	(1 << 1)	/* End-of-frame mask */
#define CICR0_FOM	(1 << 0)	/* FIFO-overrun mask */

#define CICR1_TBIT	(1 << 31)	/* Transparency bit */
#define CICR1_RGBT_CONV	(0x3 << 29)	/* RGBT conversion mask */
#define CICR1_PPL	(0x7ff << 15)	/* Pixels per line mask */
#define CICR1_RGB_CONV	(0x7 << 12)	/* RGB conversion mask */
#define CICR1_RGB_F	(1 << 11)	/* RGB format */
#define CICR1_YCBCR_F	(1 << 10)	/* YCbCr format */
#define CICR1_RGB_BPP	(0x7 << 7)	/* RGB bis per pixel mask */
#define CICR1_RAW_BPP	(0x3 << 5)	/* Raw bis per pixel mask */
#define CICR1_COLOR_SP	(0x3 << 3)	/* Color space mask */
#define CICR1_DW	(0x7 << 0)	/* Data width mask */

#define CICR2_BLW	(0xff << 24)	/* Beginning-of-line pixel clock
					   wait count mask */
#define CICR2_ELW	(0xff << 16)	/* End-of-line pixel clock
					   wait count mask */
#define CICR2_HSW	(0x3f << 10)	/* Horizontal sync pulse width mask */
#define CICR2_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR2_FSW	(0x7 << 0)	/* Frame stabilization
					   wait count mask */

#define CICR3_BFW	(0xff << 24)	/* Beginning-of-frame line clock
					   wait count mask */
#define CICR3_EFW	(0xff << 16)	/* End-of-frame line clock
					   wait count mask */
#define CICR3_VSW	(0x3f << 10)	/* Vertical sync pulse width mask */
#define CICR3_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR3_LPF	(0x7ff << 0)	/* Lines per frame mask */

#define CICR4_MCLK_DLY	(0x3 << 24)	/* MCLK Data Capture Delay mask */
#define CICR4_PCLK_EN	(1 << 23)	/* Pixel clock enable */
#define CICR4_PCP	(1 << 22)	/* Pixel clock polarity */
#define CICR4_HSP	(1 << 21)	/* Horizontal sync polarity */
#define CICR4_VSP	(1 << 20)	/* Vertical sync polarity */
#define CICR4_MCLK_EN	(1 << 19)	/* MCLK enable */
#define CICR4_FR_RATE	(0x7 << 8)	/* Frame rate mask */
#define CICR4_DIV	(0xff << 0)	/* Clock divisor mask */

#define CISR_FTO	(1 << 15)	/* FIFO time-out */
#define CISR_RDAV_2	(1 << 14)	/* Channel 2 receive data available */
#define CISR_RDAV_1	(1 << 13)	/* Channel 1 receive data available */
#define CISR_RDAV_0	(1 << 12)	/* Channel 0 receive data available */
#define CISR_FEMPTY_2	(1 << 11)	/* Channel 2 FIFO empty */
#define CISR_FEMPTY_1	(1 << 10)	/* Channel 1 FIFO empty */
#define CISR_FEMPTY_0	(1 << 9)	/* Channel 0 FIFO empty */
#define CISR_EOL	(1 << 8)	/* End of line */
#define CISR_PAR_ERR	(1 << 7)	/* Parity error */
#define CISR_CQD	(1 << 6)	/* Camera interface quick disable */
#define CISR_CDD	(1 << 5)	/* Camera interface disable done */
#define CISR_SOF	(1 << 4)	/* Start of frame */
#define CISR_EOF	(1 << 3)	/* End of frame */
#define CISR_IFO_2	(1 << 2)	/* FIFO overrun for Channel 2 */
#define CISR_IFO_1	(1 << 1)	/* FIFO overrun for Channel 1 */
#define CISR_IFO_0	(1 << 0)	/* FIFO overrun for Channel 0 */

#define CIFR_FLVL2	(0x7f << 23)	/* FIFO 2 level mask */
#define CIFR_FLVL1	(0x7f << 16)	/* FIFO 1 level mask */
#define CIFR_FLVL0	(0xff << 8)	/* FIFO 0 level mask */
#define CIFR_THL_0	(0x3 << 4)	/* Threshold Level for Channel 0 FIFO */
#define CIFR_RESET_F	(1 << 3)	/* Reset input FIFOs */
#define CIFR_FEN2	(1 << 2)	/* FIFO enable for channel 2 */
#define CIFR_FEN1	(1 << 1)	/* FIFO enable for channel 1 */
#define CIFR_FEN0	(1 << 0)	/* FIFO enable for channel 0 */

#define CICR0_SIM_MP	(0 << 24)
#define CICR0_SIM_SP	(1 << 24)
#define CICR0_SIM_MS	(2 << 24)
#define CICR0_SIM_EP	(3 << 24)
#define CICR0_SIM_ES	(4 << 24)

#define CICR1_DW_VAL(x)   ((x) & CICR1_DW)	    /* Data bus width */
#define CICR1_PPL_VAL(x)  (((x) << 15) & CICR1_PPL) /* Pixels per line */
#define CICR1_COLOR_SP_VAL(x)	(((x) << 3) & CICR1_COLOR_SP)	/* color space */
#define CICR1_RGB_BPP_VAL(x)	(((x) << 7) & CICR1_RGB_BPP)	/* bpp for rgb */
#define CICR1_RGBT_CONV_VAL(x)	(((x) << 29) & CICR1_RGBT_CONV)	/* rgbt conv */

#define CICR2_BLW_VAL(x)  (((x) << 24) & CICR2_BLW) /* Beginning-of-line pixel clock wait count */
#define CICR2_ELW_VAL(x)  (((x) << 16) & CICR2_ELW) /* End-of-line pixel clock wait count */
#define CICR2_HSW_VAL(x)  (((x) << 10) & CICR2_HSW) /* Horizontal sync pulse width */
#define CICR2_BFPW_VAL(x) (((x) << 3) & CICR2_BFPW) /* Beginning-of-frame pixel clock wait count */
#define CICR2_FSW_VAL(x)  (((x) << 0) & CICR2_FSW)  /* Frame stabilization wait count */

#define CICR3_BFW_VAL(x)  (((x) << 24) & CICR3_BFW) /* Beginning-of-frame line clock wait count  */
#define CICR3_EFW_VAL(x)  (((x) << 16) & CICR3_EFW) /* End-of-frame line clock wait count */
#define CICR3_VSW_VAL(x)  (((x) << 11) & CICR3_VSW) /* Vertical sync pulse width */
#define CICR3_LPF_VAL(x)  (((x) << 0) & CICR3_LPF)  /* Lines per frame */

#define CICR0_IRQ_MASK (CICR0_TOM | CICR0_RDAVM | CICR0_FEM | CICR0_EOLM | \
			CICR0_PERRM | CICR0_QDM | CICR0_CDM | CICR0_SOFM | \
			CICR0_EOFM | CICR0_FOM)

/*
 * YUV422P picture size should be a multiple of 16, so the heuristic aligns
 * height, width on 4 byte boundaries to reach the 16 multiple for the size.
 */
#define YUV422P_X_Y_ALIGN 4
#define YUV422P_SIZE_ALIGN YUV422P_X_Y_ALIGN * YUV422P_X_Y_ALIGN

/*
 * Structures
 */
enum pxa_camera_active_dma {
	DMA_Y = 0x1,
	DMA_U = 0x2,
	DMA_V = 0x4,
};

/* descriptor needed for the PXA DMA engine */
struct pxa_cam_dma {
	dma_addr_t		sg_dma;
	struct pxa_dma_desc	*sg_cpu;
	size_t			sg_size;
	int			sglen;
};

/* buffer for one video frame */
struct pxa_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	const struct soc_camera_data_format        *fmt;

	/* our descriptor lists for Y, U and V channels */
	struct pxa_cam_dma dmas[3];

	int			inwork;

	enum pxa_camera_active_dma active_dma;
};

struct pxa_camera_dev {
	struct device		*dev;
	/* PXA27x is only supposed to handle one camera on its Quick Capture
	 * interface. If anyone ever builds hardware to enable more than
	 * one camera, they will have to modify this driver too */
	struct soc_camera_device *icd;
	struct clk		*clk;

	unsigned int		irq;
	void __iomem		*base;

	int			channels;
	unsigned int		dma_chans[3];

	struct pxacamera_platform_data *pdata;
	struct resource		*res;
	unsigned long		platform_flags;
	unsigned long		ciclk;
	unsigned long		mclk;
	u32			mclk_divisor;

	struct list_head	capture;

	spinlock_t		lock;

	struct pxa_buffer	*active;
	struct pxa_dma_desc	*sg_tail[3];

	u32			save_cicr[5];
};

static const char *pxa_cam_driver_description = "PXA_Camera";

static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */

/*
 *  Videobuf operations
 */
static int pxa_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
			      unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	*size = roundup(icd->width * icd->height *
			((icd->current_fmt->depth + 7) >> 3), 8);

	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct pxa_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	int i;

	BUG_ON(in_interrupt());

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	/* This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE */
	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_dma_unmap(vq, dma);
	videobuf_dma_free(dma);

	for (i = 0; i < ARRAY_SIZE(buf->dmas); i++) {
		if (buf->dmas[i].sg_cpu)
			dma_free_coherent(pcdev->dev, buf->dmas[i].sg_size,
					  buf->dmas[i].sg_cpu,
					  buf->dmas[i].sg_dma);
		buf->dmas[i].sg_cpu = NULL;
	}

	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int calculate_dma_sglen(struct scatterlist *sglist, int sglen,
			       int sg_first_ofs, int size)
{
	int i, offset, dma_len, xfer_len;
	struct scatterlist *sg;

	offset = sg_first_ofs;
	for_each_sg(sglist, sg, sglen, i) {
		dma_len = sg_dma_len(sg);

		/* PXA27x Developer's Manual 27.4.4.1: round up to 8 bytes */
		xfer_len = roundup(min(dma_len - offset, size), 8);

		size = max(0, size - xfer_len);
		offset = 0;
		if (size == 0)
			break;
	}

	BUG_ON(size != 0);
	return i + 1;
}

/**
 * pxa_init_dma_channel - init dma descriptors
 * @pcdev: pxa camera device
 * @buf: pxa buffer to find pxa dma channel
 * @dma: dma video buffer
 * @channel: dma channel (0 => 'Y', 1 => 'U', 2 => 'V')
 * @cibr: camera Receive Buffer Register
 * @size: bytes to transfer
 * @sg_first: first element of sg_list
 * @sg_first_ofs: offset in first element of sg_list
 *
 * Prepares the pxa dma descriptors to transfer one camera channel.
 * Beware sg_first and sg_first_ofs are both input and output parameters.
 *
 * Returns 0 or -ENOMEM if no coherent memory is available
 */
static int pxa_init_dma_channel(struct pxa_camera_dev *pcdev,
				struct pxa_buffer *buf,
				struct videobuf_dmabuf *dma, int channel,
				int cibr, int size,
				struct scatterlist **sg_first, int *sg_first_ofs)
{
	struct pxa_cam_dma *pxa_dma = &buf->dmas[channel];
	struct scatterlist *sg;
	int i, offset, sglen;
	int dma_len = 0, xfer_len = 0;

	if (pxa_dma->sg_cpu)
		dma_free_coherent(pcdev->dev, pxa_dma->sg_size,
				  pxa_dma->sg_cpu, pxa_dma->sg_dma);

	sglen = calculate_dma_sglen(*sg_first, dma->sglen,
				    *sg_first_ofs, size);

	pxa_dma->sg_size = (sglen + 1) * sizeof(struct pxa_dma_desc);
	pxa_dma->sg_cpu = dma_alloc_coherent(pcdev->dev, pxa_dma->sg_size,
					     &pxa_dma->sg_dma, GFP_KERNEL);
	if (!pxa_dma->sg_cpu)
		return -ENOMEM;

	pxa_dma->sglen = sglen;
	offset = *sg_first_ofs;

	dev_dbg(pcdev->dev, "DMA: sg_first=%p, sglen=%d, ofs=%d, dma.desc=%x\n",
		*sg_first, sglen, *sg_first_ofs, pxa_dma->sg_dma);


	for_each_sg(*sg_first, sg, sglen, i) {
		dma_len = sg_dma_len(sg);

		/* PXA27x Developer's Manual 27.4.4.1: round up to 8 bytes */
		xfer_len = roundup(min(dma_len - offset, size), 8);

		size = max(0, size - xfer_len);

		pxa_dma->sg_cpu[i].dsadr = pcdev->res->start + cibr;
		pxa_dma->sg_cpu[i].dtadr = sg_dma_address(sg) + offset;
		pxa_dma->sg_cpu[i].dcmd =
			DCMD_FLOWSRC | DCMD_BURST8 | DCMD_INCTRGADDR | xfer_len;
#ifdef DEBUG
		if (!i)
			pxa_dma->sg_cpu[i].dcmd |= DCMD_STARTIRQEN;
#endif
		pxa_dma->sg_cpu[i].ddadr =
			pxa_dma->sg_dma + (i + 1) * sizeof(struct pxa_dma_desc);

		dev_vdbg(pcdev->dev, "DMA: desc.%08x->@phys=0x%08x, len=%d\n",
			 pxa_dma->sg_dma + i * sizeof(struct pxa_dma_desc),
			 sg_dma_address(sg) + offset, xfer_len);
		offset = 0;

		if (size == 0)
			break;
	}

	pxa_dma->sg_cpu[sglen].ddadr = DDADR_STOP;
	pxa_dma->sg_cpu[sglen].dcmd  = DCMD_FLOWSRC | DCMD_BURST8 | DCMD_ENDIRQEN;

	/*
	 * Handle 1 special case :
	 *  - in 3 planes (YUV422P format), we might finish with xfer_len equal
	 *    to dma_len (end on PAGE boundary). In this case, the sg element
	 *    for next plane should be the next after the last used to store the
	 *    last scatter gather RAM page
	 */
	if (xfer_len >= dma_len) {
		*sg_first_ofs = xfer_len - dma_len;
		*sg_first = sg_next(sg);
	} else {
		*sg_first_ofs = xfer_len;
		*sg_first = sg;
	}

	return 0;
}

static void pxa_videobuf_set_actdma(struct pxa_camera_dev *pcdev,
				    struct pxa_buffer *buf)
{
	buf->active_dma = DMA_Y;
	if (pcdev->channels == 3)
		buf->active_dma |= DMA_U | DMA_V;
}

/*
 * Please check the DMA prepared buffer structure in :
 *   Documentation/video4linux/pxa_camera.txt
 * Please check also in pxa_camera_check_link_miss() to understand why DMA chain
 * modification while DMA chain is running will work anyway.
 */
static int pxa_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
	int ret;
	int size_y, size_u = 0, size_v = 0;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/* This can be useful if you want to see if we actually fill
	 * the buffer with something */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	/* I think, in buf_prepare you only have to protect global data,
	 * the actual buffer is yours */
	buf->inwork = 1;

	if (buf->fmt	!= icd->current_fmt ||
	    vb->width	!= icd->width ||
	    vb->height	!= icd->height ||
	    vb->field	!= field) {
		buf->fmt	= icd->current_fmt;
		vb->width	= icd->width;
		vb->height	= icd->height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = vb->width * vb->height * ((buf->fmt->depth + 7) >> 3);
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int size = vb->size;
		int next_ofs = 0;
		struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
		struct scatterlist *sg;

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;

		if (pcdev->channels == 3) {
			size_y = size / 2;
			size_u = size_v = size / 4;
		} else {
			size_y = size;
		}

		sg = dma->sglist;

		/* init DMA for Y channel */
		ret = pxa_init_dma_channel(pcdev, buf, dma, 0, CIBR0, size_y,
					   &sg, &next_ofs);
		if (ret) {
			dev_err(pcdev->dev,
				"DMA initialization for Y/RGB failed\n");
			goto fail;
		}

		/* init DMA for U channel */
		if (size_u)
			ret = pxa_init_dma_channel(pcdev, buf, dma, 1, CIBR1,
						   size_u, &sg, &next_ofs);
		if (ret) {
			dev_err(pcdev->dev,
				"DMA initialization for U failed\n");
			goto fail_u;
		}

		/* init DMA for V channel */
		if (size_v)
			ret = pxa_init_dma_channel(pcdev, buf, dma, 2, CIBR2,
						   size_v, &sg, &next_ofs);
		if (ret) {
			dev_err(pcdev->dev,
				"DMA initialization for V failed\n");
			goto fail_v;
		}

		vb->state = VIDEOBUF_PREPARED;
	}

	buf->inwork = 0;
	pxa_videobuf_set_actdma(pcdev, buf);

	return 0;

fail_v:
	dma_free_coherent(pcdev->dev, buf->dmas[1].sg_size,
			  buf->dmas[1].sg_cpu, buf->dmas[1].sg_dma);
fail_u:
	dma_free_coherent(pcdev->dev, buf->dmas[0].sg_size,
			  buf->dmas[0].sg_cpu, buf->dmas[0].sg_dma);
fail:
	free_buffer(vq, buf);
out:
	buf->inwork = 0;
	return ret;
}

/**
 * pxa_dma_start_channels - start DMA channel for active buffer
 * @pcdev: pxa camera device
 *
 * Initialize DMA channels to the beginning of the active video buffer, and
 * start these channels.
 */
static void pxa_dma_start_channels(struct pxa_camera_dev *pcdev)
{
	int i;
	struct pxa_buffer *active;

	active = pcdev->active;

	for (i = 0; i < pcdev->channels; i++) {
		dev_dbg(pcdev->dev, "%s (channel=%d) ddadr=%08x\n", __func__,
			i, active->dmas[i].sg_dma);
		DDADR(pcdev->dma_chans[i]) = active->dmas[i].sg_dma;
		DCSR(pcdev->dma_chans[i]) = DCSR_RUN;
	}
}

static void pxa_dma_stop_channels(struct pxa_camera_dev *pcdev)
{
	int i;

	for (i = 0; i < pcdev->channels; i++) {
		dev_dbg(pcdev->dev, "%s (channel=%d)\n", __func__, i);
		DCSR(pcdev->dma_chans[i]) = 0;
	}
}

static void pxa_dma_add_tail_buf(struct pxa_camera_dev *pcdev,
				 struct pxa_buffer *buf)
{
	int i;
	struct pxa_dma_desc *buf_last_desc;

	for (i = 0; i < pcdev->channels; i++) {
		buf_last_desc = buf->dmas[i].sg_cpu + buf->dmas[i].sglen;
		buf_last_desc->ddadr = DDADR_STOP;

		if (pcdev->sg_tail[i])
			/* Link the new buffer to the old tail */
			pcdev->sg_tail[i]->ddadr = buf->dmas[i].sg_dma;

		/* Update the channel tail */
		pcdev->sg_tail[i] = buf_last_desc;
	}
}

/**
 * pxa_camera_start_capture - start video capturing
 * @pcdev: camera device
 *
 * Launch capturing. DMA channels should not be active yet. They should get
 * activated at the end of frame interrupt, to capture only whole frames, and
 * never begin the capture of a partial frame.
 */
static void pxa_camera_start_capture(struct pxa_camera_dev *pcdev)
{
	unsigned long cicr0, cifr;

	dev_dbg(pcdev->dev, "%s\n", __func__);
	/* Reset the FIFOs */
	cifr = __raw_readl(pcdev->base + CIFR) | CIFR_RESET_F;
	__raw_writel(cifr, pcdev->base + CIFR);
	/* Enable End-Of-Frame Interrupt */
	cicr0 = __raw_readl(pcdev->base + CICR0) | CICR0_ENB;
	cicr0 &= ~CICR0_EOFM;
	__raw_writel(cicr0, pcdev->base + CICR0);
}

static void pxa_camera_stop_capture(struct pxa_camera_dev *pcdev)
{
	unsigned long cicr0;

	pxa_dma_stop_channels(pcdev);

	cicr0 = __raw_readl(pcdev->base + CICR0) & ~CICR0_ENB;
	__raw_writel(cicr0, pcdev->base + CICR0);

	pcdev->active = NULL;
	dev_dbg(pcdev->dev, "%s\n", __func__);
}

static void pxa_videobuf_queue(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
	unsigned long flags;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d active=%p\n", __func__,
		vb, vb->baddr, vb->bsize, pcdev->active);

	spin_lock_irqsave(&pcdev->lock, flags);

	list_add_tail(&vb->queue, &pcdev->capture);

	vb->state = VIDEOBUF_ACTIVE;
	pxa_dma_add_tail_buf(pcdev, buf);

	if (!pcdev->active)
		pxa_camera_start_capture(pcdev);

	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void pxa_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
#ifdef DEBUG
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	switch (vb->state) {
	case VIDEOBUF_ACTIVE:
		dev_dbg(&icd->dev, "%s (active)\n", __func__);
		break;
	case VIDEOBUF_QUEUED:
		dev_dbg(&icd->dev, "%s (queued)\n", __func__);
		break;
	case VIDEOBUF_PREPARED:
		dev_dbg(&icd->dev, "%s (prepared)\n", __func__);
		break;
	default:
		dev_dbg(&icd->dev, "%s (unknown)\n", __func__);
		break;
	}
#endif

	free_buffer(vq, buf);
}

static void pxa_camera_wakeup(struct pxa_camera_dev *pcdev,
			      struct videobuf_buffer *vb,
			      struct pxa_buffer *buf)
{
	int i;

	/* _init is used to debug races, see comment in pxa_camera_reqbufs() */
	list_del_init(&vb->queue);
	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);
	dev_dbg(pcdev->dev, "%s dequeud buffer (vb=0x%p)\n", __func__, vb);

	if (list_empty(&pcdev->capture)) {
		pxa_camera_stop_capture(pcdev);
		for (i = 0; i < pcdev->channels; i++)
			pcdev->sg_tail[i] = NULL;
		return;
	}

	pcdev->active = list_entry(pcdev->capture.next,
				   struct pxa_buffer, vb.queue);
}

/**
 * pxa_camera_check_link_miss - check missed DMA linking
 * @pcdev: camera device
 *
 * The DMA chaining is done with DMA running. This means a tiny temporal window
 * remains, where a buffer is queued on the chain, while the chain is already
 * stopped. This means the tailed buffer would never be transfered by DMA.
 * This function restarts the capture for this corner case, where :
 *  - DADR() == DADDR_STOP
 *  - a videobuffer is queued on the pcdev->capture list
 *
 * Please check the "DMA hot chaining timeslice issue" in
 *   Documentation/video4linux/pxa_camera.txt
 *
 * Context: should only be called within the dma irq handler
 */
static void pxa_camera_check_link_miss(struct pxa_camera_dev *pcdev)
{
	int i, is_dma_stopped = 1;

	for (i = 0; i < pcdev->channels; i++)
		if (DDADR(pcdev->dma_chans[i]) != DDADR_STOP)
			is_dma_stopped = 0;
	dev_dbg(pcdev->dev, "%s : top queued buffer=%p, dma_stopped=%d\n",
		__func__, pcdev->active, is_dma_stopped);
	if (pcdev->active && is_dma_stopped)
		pxa_camera_start_capture(pcdev);
}

static void pxa_camera_dma_irq(int channel, struct pxa_camera_dev *pcdev,
			       enum pxa_camera_active_dma act_dma)
{
	struct pxa_buffer *buf;
	unsigned long flags;
	u32 status, camera_status, overrun;
	struct videobuf_buffer *vb;

	spin_lock_irqsave(&pcdev->lock, flags);

	status = DCSR(channel);
	DCSR(channel) = status;

	camera_status = __raw_readl(pcdev->base + CISR);
	overrun = CISR_IFO_0;
	if (pcdev->channels == 3)
		overrun |= CISR_IFO_1 | CISR_IFO_2;

	if (status & DCSR_BUSERR) {
		dev_err(pcdev->dev, "DMA Bus Error IRQ!\n");
		goto out;
	}

	if (!(status & (DCSR_ENDINTR | DCSR_STARTINTR))) {
		dev_err(pcdev->dev, "Unknown DMA IRQ source, "
			"status: 0x%08x\n", status);
		goto out;
	}

	/*
	 * pcdev->active should not be NULL in DMA irq handler.
	 *
	 * But there is one corner case : if capture was stopped due to an
	 * overrun of channel 1, and at that same channel 2 was completed.
	 *
	 * When handling the overrun in DMA irq for channel 1, we'll stop the
	 * capture and restart it (and thus set pcdev->active to NULL). But the
	 * DMA irq handler will already be pending for channel 2. So on entering
	 * the DMA irq handler for channel 2 there will be no active buffer, yet
	 * that is normal.
	 */
	if (!pcdev->active)
		goto out;

	vb = &pcdev->active->vb;
	buf = container_of(vb, struct pxa_buffer, vb);
	WARN_ON(buf->inwork || list_empty(&vb->queue));

	dev_dbg(pcdev->dev, "%s channel=%d %s%s(vb=0x%p) dma.desc=%x\n",
		__func__, channel, status & DCSR_STARTINTR ? "SOF " : "",
		status & DCSR_ENDINTR ? "EOF " : "", vb, DDADR(channel));

	if (status & DCSR_ENDINTR) {
		/*
		 * It's normal if the last frame creates an overrun, as there
		 * are no more DMA descriptors to fetch from QCI fifos
		 */
		if (camera_status & overrun &&
		    !list_is_last(pcdev->capture.next, &pcdev->capture)) {
			dev_dbg(pcdev->dev, "FIFO overrun! CISR: %x\n",
				camera_status);
			pxa_camera_stop_capture(pcdev);
			pxa_camera_start_capture(pcdev);
			goto out;
		}
		buf->active_dma &= ~act_dma;
		if (!buf->active_dma) {
			pxa_camera_wakeup(pcdev, vb, buf);
			pxa_camera_check_link_miss(pcdev);
		}
	}

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void pxa_camera_dma_irq_y(int channel, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	pxa_camera_dma_irq(channel, pcdev, DMA_Y);
}

static void pxa_camera_dma_irq_u(int channel, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	pxa_camera_dma_irq(channel, pcdev, DMA_U);
}

static void pxa_camera_dma_irq_v(int channel, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	pxa_camera_dma_irq(channel, pcdev, DMA_V);
}

static struct videobuf_queue_ops pxa_videobuf_ops = {
	.buf_setup      = pxa_videobuf_setup,
	.buf_prepare    = pxa_videobuf_prepare,
	.buf_queue      = pxa_videobuf_queue,
	.buf_release    = pxa_videobuf_release,
};

static void pxa_camera_init_videobuf(struct videobuf_queue *q,
			      struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;

	/* We must pass NULL as dev pointer, then all pci_* dma operations
	 * transform to normal dma_* ones. */
	videobuf_queue_sg_init(q, &pxa_videobuf_ops, NULL, &pcdev->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct pxa_buffer), icd);
}

static u32 mclk_get_divisor(struct pxa_camera_dev *pcdev)
{
	unsigned long mclk = pcdev->mclk;
	u32 div;
	unsigned long lcdclk;

	lcdclk = clk_get_rate(pcdev->clk);
	pcdev->ciclk = lcdclk;

	/* mclk <= ciclk / 4 (27.4.2) */
	if (mclk > lcdclk / 4) {
		mclk = lcdclk / 4;
		dev_warn(pcdev->dev, "Limiting master clock to %lu\n", mclk);
	}

	/* We verify mclk != 0, so if anyone breaks it, here comes their Oops */
	div = (lcdclk + 2 * mclk - 1) / (2 * mclk) - 1;

	/* If we're not supplying MCLK, leave it at 0 */
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		pcdev->mclk = lcdclk / (2 * (div + 1));

	dev_dbg(pcdev->dev, "LCD clock %luHz, target freq %luHz, "
		"divisor %u\n", lcdclk, mclk, div);

	return div;
}

static void recalculate_fifo_timeout(struct pxa_camera_dev *pcdev,
				     unsigned long pclk)
{
	/* We want a timeout > 1 pixel time, not ">=" */
	u32 ciclk_per_pixel = pcdev->ciclk / pclk + 1;

	__raw_writel(ciclk_per_pixel, pcdev->base + CITOR);
}

static void pxa_camera_activate(struct pxa_camera_dev *pcdev)
{
	struct pxacamera_platform_data *pdata = pcdev->pdata;
	u32 cicr4 = 0;

	dev_dbg(pcdev->dev, "Registered platform device at %p data %p\n",
		pcdev, pdata);

	if (pdata && pdata->init) {
		dev_dbg(pcdev->dev, "%s: Init gpios\n", __func__);
		pdata->init(pcdev->dev);
	}

	/* disable all interrupts */
	__raw_writel(0x3ff, pcdev->base + CICR0);

	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		cicr4 |= CICR4_PCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		cicr4 |= CICR4_MCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_PCP)
		cicr4 |= CICR4_PCP;
	if (pcdev->platform_flags & PXA_CAMERA_HSP)
		cicr4 |= CICR4_HSP;
	if (pcdev->platform_flags & PXA_CAMERA_VSP)
		cicr4 |= CICR4_VSP;

	__raw_writel(pcdev->mclk_divisor | cicr4, pcdev->base + CICR4);

	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		/* Initialise the timeout under the assumption pclk = mclk */
		recalculate_fifo_timeout(pcdev, pcdev->mclk);
	else
		/* "Safe default" - 13MHz */
		recalculate_fifo_timeout(pcdev, 13000000);

	clk_enable(pcdev->clk);
}

static void pxa_camera_deactivate(struct pxa_camera_dev *pcdev)
{
	clk_disable(pcdev->clk);
}

static irqreturn_t pxa_camera_irq(int irq, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	unsigned long status, cicr0;
	struct pxa_buffer *buf;
	struct videobuf_buffer *vb;

	status = __raw_readl(pcdev->base + CISR);
	dev_dbg(pcdev->dev, "Camera interrupt status 0x%lx\n", status);

	if (!status)
		return IRQ_NONE;

	__raw_writel(status, pcdev->base + CISR);

	if (status & CISR_EOF) {
		pcdev->active = list_first_entry(&pcdev->capture,
					   struct pxa_buffer, vb.queue);
		vb = &pcdev->active->vb;
		buf = container_of(vb, struct pxa_buffer, vb);
		pxa_videobuf_set_actdma(pcdev, buf);

		pxa_dma_start_channels(pcdev);

		cicr0 = __raw_readl(pcdev->base + CICR0) | CICR0_EOFM;
		__raw_writel(cicr0, pcdev->base + CICR0);
	}

	return IRQ_HANDLED;
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be only one camera on PXA quick capture interface
 * Called with .video_lock held
 */
static int pxa_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	int ret;

	if (pcdev->icd) {
		ret = -EBUSY;
		goto ebusy;
	}

	dev_info(&icd->dev, "PXA Camera driver attached to camera %d\n",
		 icd->devnum);

	pxa_camera_activate(pcdev);
	ret = icd->ops->init(icd);

	if (!ret)
		pcdev->icd = icd;

ebusy:
	return ret;
}

/* Called with .video_lock held */
static void pxa_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	dev_info(&icd->dev, "PXA Camera driver detached from camera %d\n",
		 icd->devnum);

	/* disable capture, disable interrupts */
	__raw_writel(0x3ff, pcdev->base + CICR0);

	/* Stop DMA engine */
	DCSR(pcdev->dma_chans[0]) = 0;
	DCSR(pcdev->dma_chans[1]) = 0;
	DCSR(pcdev->dma_chans[2]) = 0;

	icd->ops->release(icd);

	pxa_camera_deactivate(pcdev);

	pcdev->icd = NULL;
}

static int test_platform_param(struct pxa_camera_dev *pcdev,
			       unsigned char buswidth, unsigned long *flags)
{
	/*
	 * Platform specified synchronization and pixel clock polarities are
	 * only a recommendation and are only used during probing. The PXA270
	 * quick capture interface supports both.
	 */
	*flags = (pcdev->platform_flags & PXA_CAMERA_MASTER ?
		  SOCAM_MASTER : SOCAM_SLAVE) |
		SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_HSYNC_ACTIVE_LOW |
		SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_VSYNC_ACTIVE_LOW |
		SOCAM_DATA_ACTIVE_HIGH |
		SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_PCLK_SAMPLE_FALLING;

	/* If requested data width is supported by the platform, use it */
	switch (buswidth) {
	case 10:
		if (!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_10))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_10;
		break;
	case 9:
		if (!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_9))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_9;
		break;
	case 8:
		if (!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_8))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pxa_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	unsigned long dw, bpp, bus_flags, camera_flags, common_flags;
	u32 cicr0, cicr1, cicr2, cicr3, cicr4 = 0;
	int ret = test_platform_param(pcdev, icd->buswidth, &bus_flags);

	if (ret < 0)
		return ret;

	camera_flags = icd->ops->query_bus_param(icd);

	common_flags = soc_camera_bus_param_compatible(camera_flags, bus_flags);
	if (!common_flags)
		return -EINVAL;

	pcdev->channels = 1;

	/* Make choises, based on platform preferences */
	if ((common_flags & SOCAM_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & SOCAM_HSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & PXA_CAMERA_HSP)
			common_flags &= ~SOCAM_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~SOCAM_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & SOCAM_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & SOCAM_VSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & PXA_CAMERA_VSP)
			common_flags &= ~SOCAM_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~SOCAM_VSYNC_ACTIVE_LOW;
	}

	if ((common_flags & SOCAM_PCLK_SAMPLE_RISING) &&
	    (common_flags & SOCAM_PCLK_SAMPLE_FALLING)) {
		if (pcdev->platform_flags & PXA_CAMERA_PCP)
			common_flags &= ~SOCAM_PCLK_SAMPLE_RISING;
		else
			common_flags &= ~SOCAM_PCLK_SAMPLE_FALLING;
	}

	ret = icd->ops->set_bus_param(icd, common_flags);
	if (ret < 0)
		return ret;

	/* Datawidth is now guaranteed to be equal to one of the three values.
	 * We fix bit-per-pixel equal to data-width... */
	switch (common_flags & SOCAM_DATAWIDTH_MASK) {
	case SOCAM_DATAWIDTH_10:
		dw = 4;
		bpp = 0x40;
		break;
	case SOCAM_DATAWIDTH_9:
		dw = 3;
		bpp = 0x20;
		break;
	default:
		/* Actually it can only be 8 now,
		 * default is just to silence compiler warnings */
	case SOCAM_DATAWIDTH_8:
		dw = 2;
		bpp = 0;
	}

	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		cicr4 |= CICR4_PCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		cicr4 |= CICR4_MCLK_EN;
	if (common_flags & SOCAM_PCLK_SAMPLE_FALLING)
		cicr4 |= CICR4_PCP;
	if (common_flags & SOCAM_HSYNC_ACTIVE_LOW)
		cicr4 |= CICR4_HSP;
	if (common_flags & SOCAM_VSYNC_ACTIVE_LOW)
		cicr4 |= CICR4_VSP;

	cicr0 = __raw_readl(pcdev->base + CICR0);
	if (cicr0 & CICR0_ENB)
		__raw_writel(cicr0 & ~CICR0_ENB, pcdev->base + CICR0);

	cicr1 = CICR1_PPL_VAL(icd->width - 1) | bpp | dw;

	switch (pixfmt) {
	case V4L2_PIX_FMT_YUV422P:
		pcdev->channels = 3;
		cicr1 |= CICR1_YCBCR_F;
		/*
		 * Normally, pxa bus wants as input UYVY format. We allow all
		 * reorderings of the YUV422 format, as no processing is done,
		 * and the YUV stream is just passed through without any
		 * transformation. Note that UYVY is the only format that
		 * should be used if pxa framebuffer Overlay2 is used.
		 */
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		cicr1 |= CICR1_COLOR_SP_VAL(2);
		break;
	case V4L2_PIX_FMT_RGB555:
		cicr1 |= CICR1_RGB_BPP_VAL(1) | CICR1_RGBT_CONV_VAL(2) |
			CICR1_TBIT | CICR1_COLOR_SP_VAL(1);
		break;
	case V4L2_PIX_FMT_RGB565:
		cicr1 |= CICR1_COLOR_SP_VAL(1) | CICR1_RGB_BPP_VAL(2);
		break;
	}

	cicr2 = 0;
	cicr3 = CICR3_LPF_VAL(icd->height - 1) |
		CICR3_BFW_VAL(min((unsigned short)255, icd->y_skip_top));
	cicr4 |= pcdev->mclk_divisor;

	__raw_writel(cicr1, pcdev->base + CICR1);
	__raw_writel(cicr2, pcdev->base + CICR2);
	__raw_writel(cicr3, pcdev->base + CICR3);
	__raw_writel(cicr4, pcdev->base + CICR4);

	/* CIF interrupts are not used, only DMA */
	cicr0 = (cicr0 & CICR0_ENB) | (pcdev->platform_flags & PXA_CAMERA_MASTER ?
		CICR0_SIM_MP : (CICR0_SL_CAP_EN | CICR0_SIM_SP));
	cicr0 |= CICR0_DMAEN | CICR0_IRQ_MASK;
	__raw_writel(cicr0, pcdev->base + CICR0);

	return 0;
}

static int pxa_camera_try_bus_param(struct soc_camera_device *icd,
				    unsigned char buswidth)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	unsigned long bus_flags, camera_flags;
	int ret = test_platform_param(pcdev, buswidth, &bus_flags);

	if (ret < 0)
		return ret;

	camera_flags = icd->ops->query_bus_param(icd);

	return soc_camera_bus_param_compatible(camera_flags, bus_flags) ? 0 : -EINVAL;
}

static const struct soc_camera_data_format pxa_camera_formats[] = {
	{
		.name		= "Planar YUV422 16 bit",
		.depth		= 16,
		.fourcc		= V4L2_PIX_FMT_YUV422P,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

static bool buswidth_supported(struct soc_camera_device *icd, int depth)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;

	switch (depth) {
	case 8:
		return !!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_8);
	case 9:
		return !!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_9);
	case 10:
		return !!(pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_10);
	}
	return false;
}

static int required_buswidth(const struct soc_camera_data_format *fmt)
{
	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB555:
		return 8;
	default:
		return fmt->depth;
	}
}

static int pxa_camera_get_formats(struct soc_camera_device *icd, int idx,
				  struct soc_camera_format_xlate *xlate)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int formats = 0, buswidth, ret;

	buswidth = required_buswidth(icd->formats + idx);

	if (!buswidth_supported(icd, buswidth))
		return 0;

	ret = pxa_camera_try_bus_param(icd, buswidth);
	if (ret < 0)
		return 0;

	switch (icd->formats[idx].fourcc) {
	case V4L2_PIX_FMT_UYVY:
		formats++;
		if (xlate) {
			xlate->host_fmt = &pxa_camera_formats[0];
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = buswidth;
			xlate++;
			dev_dbg(&ici->dev, "Providing format %s using %s\n",
				pxa_camera_formats[0].name,
				icd->formats[idx].name);
		}
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB555:
		formats++;
		if (xlate) {
			xlate->host_fmt = icd->formats + idx;
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = buswidth;
			xlate++;
			dev_dbg(&ici->dev, "Providing format %s packed\n",
				icd->formats[idx].name);
		}
		break;
	default:
		/* Generic pass-through */
		formats++;
		if (xlate) {
			xlate->host_fmt = icd->formats + idx;
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = icd->formats[idx].depth;
			xlate++;
			dev_dbg(&ici->dev,
				"Providing format %s in pass-through mode\n",
				icd->formats[idx].name);
		}
	}

	return formats;
}

static int pxa_camera_set_crop(struct soc_camera_device *icd,
			       struct v4l2_rect *rect)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct soc_camera_sense sense = {
		.master_clock = pcdev->mclk,
		.pixel_clock_max = pcdev->ciclk / 4,
	};
	int ret;

	/* If PCLK is used to latch data from the sensor, check sense */
	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		icd->sense = &sense;

	ret = icd->ops->set_crop(icd, rect);

	icd->sense = NULL;

	if (ret < 0) {
		dev_warn(&ici->dev, "Failed to crop to %ux%u@%u:%u\n",
			 rect->width, rect->height, rect->left, rect->top);
	} else if (sense.flags & SOCAM_SENSE_PCLK_CHANGED) {
		if (sense.pixel_clock > sense.pixel_clock_max) {
			dev_err(&ici->dev,
				"pixel clock %lu set by the camera too high!",
				sense.pixel_clock);
			return -EIO;
		}
		recalculate_fifo_timeout(pcdev, sense.pixel_clock);
	}

	return ret;
}

static int pxa_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	const struct soc_camera_data_format *cam_fmt = NULL;
	const struct soc_camera_format_xlate *xlate = NULL;
	struct soc_camera_sense sense = {
		.master_clock = pcdev->mclk,
		.pixel_clock_max = pcdev->ciclk / 4,
	};
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_format cam_f = *f;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(&ici->dev, "Format %x not found\n", pix->pixelformat);
		return -EINVAL;
	}

	cam_fmt = xlate->cam_fmt;

	/* If PCLK is used to latch data from the sensor, check sense */
	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		icd->sense = &sense;

	cam_f.fmt.pix.pixelformat = cam_fmt->fourcc;
	ret = icd->ops->set_fmt(icd, &cam_f);

	icd->sense = NULL;

	if (ret < 0) {
		dev_warn(&ici->dev, "Failed to configure for format %x\n",
			 pix->pixelformat);
	} else if (sense.flags & SOCAM_SENSE_PCLK_CHANGED) {
		if (sense.pixel_clock > sense.pixel_clock_max) {
			dev_err(&ici->dev,
				"pixel clock %lu set by the camera too high!",
				sense.pixel_clock);
			return -EIO;
		}
		recalculate_fifo_timeout(pcdev, sense.pixel_clock);
	}

	if (!ret) {
		icd->buswidth = xlate->buswidth;
		icd->current_fmt = xlate->host_fmt;
	}

	return ret;
}

static int pxa_camera_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	__u32 pixfmt = pix->pixelformat;
	enum v4l2_field field;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(&ici->dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* limit to pxa hardware capabilities */
	if (pix->height < 32)
		pix->height = 32;
	if (pix->height > 2048)
		pix->height = 2048;
	if (pix->width < 48)
		pix->width = 48;
	if (pix->width > 2048)
		pix->width = 2048;
	pix->width &= ~0x01;

	/*
	 * YUV422P planar format requires images size to be a 16 bytes
	 * multiple. If not, zeros will be inserted between Y and U planes, and
	 * U and V planes, and YUV422P standard would be violated.
	 */
	if (xlate->host_fmt->fourcc == V4L2_PIX_FMT_YUV422P) {
		if (!IS_ALIGNED(pix->width * pix->height, YUV422P_SIZE_ALIGN))
			pix->height = ALIGN(pix->height, YUV422P_X_Y_ALIGN);
		if (!IS_ALIGNED(pix->width * pix->height, YUV422P_SIZE_ALIGN))
			pix->width = ALIGN(pix->width, YUV422P_X_Y_ALIGN);
	}

	pix->bytesperline = pix->width *
		DIV_ROUND_UP(xlate->host_fmt->depth, 8);
	pix->sizeimage = pix->height * pix->bytesperline;

	/* camera has to see its format, but the user the original one */
	pix->pixelformat = xlate->cam_fmt->fourcc;
	/* limit to sensor capabilities */
	ret = icd->ops->try_fmt(icd, f);
	pix->pixelformat = xlate->host_fmt->fourcc;

	field = pix->field;

	if (field == V4L2_FIELD_ANY) {
		pix->field = V4L2_FIELD_NONE;
	} else if (field != V4L2_FIELD_NONE) {
		dev_err(&icd->dev, "Field type %d unsupported.\n", field);
		return -EINVAL;
	}

	return ret;
}

static int pxa_camera_reqbufs(struct soc_camera_file *icf,
			      struct v4l2_requestbuffers *p)
{
	int i;

	/* This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered */
	for (i = 0; i < p->count; i++) {
		struct pxa_buffer *buf = container_of(icf->vb_vidq.bufs[i],
						      struct pxa_buffer, vb);
		buf->inwork = 0;
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int pxa_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_file *icf = file->private_data;
	struct pxa_buffer *buf;

	buf = list_entry(icf->vb_vidq.stream.next, struct pxa_buffer,
			 vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}

static int pxa_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, pxa_cam_driver_description, sizeof(cap->card));
	cap->version = PXA_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int pxa_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	int i = 0, ret = 0;

	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR0);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR1);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR2);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR3);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR4);

	if ((pcdev->icd) && (pcdev->icd->ops->suspend))
		ret = pcdev->icd->ops->suspend(pcdev->icd, state);

	return ret;
}

static int pxa_camera_resume(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	int i = 0, ret = 0;

	DRCMR(68) = pcdev->dma_chans[0] | DRCMR_MAPVLD;
	DRCMR(69) = pcdev->dma_chans[1] | DRCMR_MAPVLD;
	DRCMR(70) = pcdev->dma_chans[2] | DRCMR_MAPVLD;

	__raw_writel(pcdev->save_cicr[i++] & ~CICR0_ENB, pcdev->base + CICR0);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR1);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR2);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR3);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR4);

	if ((pcdev->icd) && (pcdev->icd->ops->resume))
		ret = pcdev->icd->ops->resume(pcdev->icd);

	/* Restart frame capture if active buffer exists */
	if (!ret && pcdev->active)
		pxa_camera_start_capture(pcdev);

	return ret;
}

static struct soc_camera_host_ops pxa_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= pxa_camera_add_device,
	.remove		= pxa_camera_remove_device,
	.suspend	= pxa_camera_suspend,
	.resume		= pxa_camera_resume,
	.set_crop	= pxa_camera_set_crop,
	.get_formats	= pxa_camera_get_formats,
	.set_fmt	= pxa_camera_set_fmt,
	.try_fmt	= pxa_camera_try_fmt,
	.init_videobuf	= pxa_camera_init_videobuf,
	.reqbufs	= pxa_camera_reqbufs,
	.poll		= pxa_camera_poll,
	.querycap	= pxa_camera_querycap,
	.set_bus_param	= pxa_camera_set_bus_param,
};

/* Should be allocated dynamically too, but we have only one. */
static struct soc_camera_host pxa_soc_camera_host = {
	.drv_name		= PXA_CAM_DRV_NAME,
	.ops			= &pxa_soc_camera_host_ops,
};

static int pxa_camera_probe(struct platform_device *pdev)
{
	struct pxa_camera_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	pcdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk)) {
		err = PTR_ERR(pcdev->clk);
		goto exit_kfree;
	}

	dev_set_drvdata(&pdev->dev, pcdev);
	pcdev->res = res;

	pcdev->pdata = pdev->dev.platform_data;
	pcdev->platform_flags = pcdev->pdata->flags;
	if (!(pcdev->platform_flags & (PXA_CAMERA_DATAWIDTH_8 |
			PXA_CAMERA_DATAWIDTH_9 | PXA_CAMERA_DATAWIDTH_10))) {
		/* Platform hasn't set available data widths. This is bad.
		 * Warn and use a default. */
		dev_warn(&pdev->dev, "WARNING! Platform hasn't set available "
			 "data widths, using default 10 bit\n");
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_10;
	}
	pcdev->mclk = pcdev->pdata->mclk_10khz * 10000;
	if (!pcdev->mclk) {
		dev_warn(&pdev->dev,
			 "mclk == 0! Please, fix your platform data. "
			 "Using default 20MHz\n");
		pcdev->mclk = 20000000;
	}

	pcdev->dev = &pdev->dev;
	pcdev->mclk_divisor = mclk_get_divisor(pcdev);

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the regions.
	 */
	if (!request_mem_region(res->start, res->end - res->start + 1,
				PXA_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto exit_clk;
	}

	base = ioremap(res->start, res->end - res->start + 1);
	if (!base) {
		err = -ENOMEM;
		goto exit_release;
	}
	pcdev->irq = irq;
	pcdev->base = base;

	/* request dma */
	err = pxa_request_dma("CI_Y", DMA_PRIO_HIGH,
			      pxa_camera_dma_irq_y, pcdev);
	if (err < 0) {
		dev_err(pcdev->dev, "Can't request DMA for Y\n");
		goto exit_iounmap;
	}
	pcdev->dma_chans[0] = err;
	dev_dbg(pcdev->dev, "got DMA channel %d\n", pcdev->dma_chans[0]);

	err = pxa_request_dma("CI_U", DMA_PRIO_HIGH,
			      pxa_camera_dma_irq_u, pcdev);
	if (err < 0) {
		dev_err(pcdev->dev, "Can't request DMA for U\n");
		goto exit_free_dma_y;
	}
	pcdev->dma_chans[1] = err;
	dev_dbg(pcdev->dev, "got DMA channel (U) %d\n", pcdev->dma_chans[1]);

	err = pxa_request_dma("CI_V", DMA_PRIO_HIGH,
			      pxa_camera_dma_irq_v, pcdev);
	if (err < 0) {
		dev_err(pcdev->dev, "Can't request DMA for V\n");
		goto exit_free_dma_u;
	}
	pcdev->dma_chans[2] = err;
	dev_dbg(pcdev->dev, "got DMA channel (V) %d\n", pcdev->dma_chans[2]);

	DRCMR(68) = pcdev->dma_chans[0] | DRCMR_MAPVLD;
	DRCMR(69) = pcdev->dma_chans[1] | DRCMR_MAPVLD;
	DRCMR(70) = pcdev->dma_chans[2] | DRCMR_MAPVLD;

	/* request irq */
	err = request_irq(pcdev->irq, pxa_camera_irq, 0, PXA_CAM_DRV_NAME,
			  pcdev);
	if (err) {
		dev_err(pcdev->dev, "Camera interrupt register failed \n");
		goto exit_free_dma;
	}

	pxa_soc_camera_host.priv	= pcdev;
	pxa_soc_camera_host.dev.parent	= &pdev->dev;
	pxa_soc_camera_host.nr		= pdev->id;
	err = soc_camera_host_register(&pxa_soc_camera_host);
	if (err)
		goto exit_free_irq;

	return 0;

exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_free_dma:
	pxa_free_dma(pcdev->dma_chans[2]);
exit_free_dma_u:
	pxa_free_dma(pcdev->dma_chans[1]);
exit_free_dma_y:
	pxa_free_dma(pcdev->dma_chans[0]);
exit_iounmap:
	iounmap(base);
exit_release:
	release_mem_region(res->start, res->end - res->start + 1);
exit_clk:
	clk_put(pcdev->clk);
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit pxa_camera_remove(struct platform_device *pdev)
{
	struct pxa_camera_dev *pcdev = platform_get_drvdata(pdev);
	struct resource *res;

	clk_put(pcdev->clk);

	pxa_free_dma(pcdev->dma_chans[0]);
	pxa_free_dma(pcdev->dma_chans[1]);
	pxa_free_dma(pcdev->dma_chans[2]);
	free_irq(pcdev->irq, pcdev);

	soc_camera_host_unregister(&pxa_soc_camera_host);

	iounmap(pcdev->base);

	res = pcdev->res;
	release_mem_region(res->start, res->end - res->start + 1);

	kfree(pcdev);

	dev_info(&pdev->dev, "PXA Camera driver unloaded\n");

	return 0;
}

static struct platform_driver pxa_camera_driver = {
	.driver 	= {
		.name	= PXA_CAM_DRV_NAME,
	},
	.probe		= pxa_camera_probe,
	.remove		= __exit_p(pxa_camera_remove),
};


static int __devinit pxa_camera_init(void)
{
	return platform_driver_register(&pxa_camera_driver);
}

static void __exit pxa_camera_exit(void)
{
	platform_driver_unregister(&pxa_camera_driver);
}

module_init(pxa_camera_init);
module_exit(pxa_camera_exit);

MODULE_DESCRIPTION("PXA27x SoC Camera Host driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
