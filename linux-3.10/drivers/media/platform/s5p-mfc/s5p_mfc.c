/*
 * Samsung S5P Multi Format Codec v 5.1
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-event.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <media/videobuf2-core.h>
#include "s5p_mfc_common.h"
#include "s5p_mfc_ctrl.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_dec.h"
#include "s5p_mfc_enc.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_opr.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_pm.h"

#define S5P_MFC_NAME		"s5p-mfc"
#define S5P_MFC_DEC_NAME	"s5p-mfc-dec"
#define S5P_MFC_ENC_NAME	"s5p-mfc-enc"

int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level - higher value produces more verbose messages");

/* Helper functions for interrupt processing */

/* Remove from hw execution round robin */
void clear_work_bit(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	spin_lock(&dev->condlock);
	__clear_bit(ctx->num, &dev->ctx_work_bits);
	spin_unlock(&dev->condlock);
}

/* Add to hw execution round robin */
void set_work_bit(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	spin_lock(&dev->condlock);
	__set_bit(ctx->num, &dev->ctx_work_bits);
	spin_unlock(&dev->condlock);
}

/* Remove from hw execution round robin */
void clear_work_bit_irqsave(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->condlock, flags);
	__clear_bit(ctx->num, &dev->ctx_work_bits);
	spin_unlock_irqrestore(&dev->condlock, flags);
}

/* Add to hw execution round robin */
void set_work_bit_irqsave(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->condlock, flags);
	__set_bit(ctx->num, &dev->ctx_work_bits);
	spin_unlock_irqrestore(&dev->condlock, flags);
}

/* Wake up context wait_queue */
static void wake_up_ctx(struct s5p_mfc_ctx *ctx, unsigned int reason,
			unsigned int err)
{
	ctx->int_cond = 1;
	ctx->int_type = reason;
	ctx->int_err = err;
	wake_up(&ctx->queue);
}

/* Wake up device wait_queue */
static void wake_up_dev(struct s5p_mfc_dev *dev, unsigned int reason,
			unsigned int err)
{
	dev->int_cond = 1;
	dev->int_type = reason;
	dev->int_err = err;
	wake_up(&dev->queue);
}

static void s5p_mfc_watchdog(unsigned long arg)
{
	struct s5p_mfc_dev *dev = (struct s5p_mfc_dev *)arg;

	if (test_bit(0, &dev->hw_lock))
		atomic_inc(&dev->watchdog_cnt);
	if (atomic_read(&dev->watchdog_cnt) >= MFC_WATCHDOG_CNT) {
		/* This means that hw is busy and no interrupts were
		 * generated by hw for the Nth time of running this
		 * watchdog timer. This usually means a serious hw
		 * error. Now it is time to kill all instances and
		 * reset the MFC. */
		mfc_err("Time out during waiting for HW\n");
		queue_work(dev->watchdog_workqueue, &dev->watchdog_work);
	}
	dev->watchdog_timer.expires = jiffies +
					msecs_to_jiffies(MFC_WATCHDOG_INTERVAL);
	add_timer(&dev->watchdog_timer);
}

static void s5p_mfc_watchdog_worker(struct work_struct *work)
{
	struct s5p_mfc_dev *dev;
	struct s5p_mfc_ctx *ctx;
	unsigned long flags;
	int mutex_locked;
	int i, ret;

	dev = container_of(work, struct s5p_mfc_dev, watchdog_work);

	mfc_err("Driver timeout error handling\n");
	/* Lock the mutex that protects open and release.
	 * This is necessary as they may load and unload firmware. */
	mutex_locked = mutex_trylock(&dev->mfc_mutex);
	if (!mutex_locked)
		mfc_err("Error: some instance may be closing/opening\n");
	spin_lock_irqsave(&dev->irqlock, flags);

	s5p_mfc_clock_off();

	for (i = 0; i < MFC_NUM_CONTEXTS; i++) {
		ctx = dev->ctx[i];
		if (!ctx)
			continue;
		ctx->state = MFCINST_ERROR;
		s5p_mfc_hw_call(dev->mfc_ops, cleanup_queue, &ctx->dst_queue,
				&ctx->vq_dst);
		s5p_mfc_hw_call(dev->mfc_ops, cleanup_queue, &ctx->src_queue,
				&ctx->vq_src);
		clear_work_bit(ctx);
		wake_up_ctx(ctx, S5P_MFC_R2H_CMD_ERR_RET, 0);
	}
	clear_bit(0, &dev->hw_lock);
	spin_unlock_irqrestore(&dev->irqlock, flags);
	/* Double check if there is at least one instance running.
	 * If no instance is in memory then no firmware should be present */
	if (dev->num_inst > 0) {
		ret = s5p_mfc_reload_firmware(dev);
		if (ret) {
			mfc_err("Failed to reload FW\n");
			goto unlock;
		}
		s5p_mfc_clock_on();
		ret = s5p_mfc_init_hw(dev);
		if (ret)
			mfc_err("Failed to reinit FW\n");
	}
unlock:
	if (mutex_locked)
		mutex_unlock(&dev->mfc_mutex);
}

static void s5p_mfc_clear_int_flags(struct s5p_mfc_dev *dev)
{
	mfc_write(dev, 0, S5P_FIMV_RISC_HOST_INT);
	mfc_write(dev, 0, S5P_FIMV_RISC2HOST_CMD);
	mfc_write(dev, 0xffff, S5P_FIMV_SI_RTN_CHID);
}

static void s5p_mfc_handle_frame_all_extracted(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_buf *dst_buf;
	struct s5p_mfc_dev *dev = ctx->dev;

	ctx->state = MFCINST_FINISHED;
	ctx->sequence++;
	while (!list_empty(&ctx->dst_queue)) {
		dst_buf = list_entry(ctx->dst_queue.next,
				     struct s5p_mfc_buf, list);
		mfc_debug(2, "Cleaning up buffer: %d\n",
					  dst_buf->b->v4l2_buf.index);
		vb2_set_plane_payload(dst_buf->b, 0, 0);
		vb2_set_plane_payload(dst_buf->b, 1, 0);
		list_del(&dst_buf->list);
		ctx->dst_queue_cnt--;
		dst_buf->b->v4l2_buf.sequence = (ctx->sequence++);

		if (s5p_mfc_hw_call(dev->mfc_ops, get_pic_type_top, ctx) ==
			s5p_mfc_hw_call(dev->mfc_ops, get_pic_type_bot, ctx))
			dst_buf->b->v4l2_buf.field = V4L2_FIELD_NONE;
		else
			dst_buf->b->v4l2_buf.field = V4L2_FIELD_INTERLACED;

		ctx->dec_dst_flag &= ~(1 << dst_buf->b->v4l2_buf.index);
		vb2_buffer_done(dst_buf->b, VB2_BUF_STATE_DONE);
	}
}

static void s5p_mfc_handle_frame_copy_time(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf  *dst_buf, *src_buf;
	size_t dec_y_addr;
	unsigned int frame_type;

	dec_y_addr = s5p_mfc_hw_call(dev->mfc_ops, get_dec_y_adr, dev);
	frame_type = s5p_mfc_hw_call(dev->mfc_ops, get_dec_frame_type, dev);

	/* Copy timestamp / timecode from decoded src to dst and set
	   appropriate flags */
	src_buf = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	list_for_each_entry(dst_buf, &ctx->dst_queue, list) {
		if (vb2_dma_contig_plane_dma_addr(dst_buf->b, 0) == dec_y_addr) {
			dst_buf->b->v4l2_buf.timecode =
						src_buf->b->v4l2_buf.timecode;
			dst_buf->b->v4l2_buf.timestamp =
						src_buf->b->v4l2_buf.timestamp;
			dst_buf->b->v4l2_buf.flags &=
				~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
			dst_buf->b->v4l2_buf.flags |=
				src_buf->b->v4l2_buf.flags
				& V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
			switch (frame_type) {
			case S5P_FIMV_DECODE_FRAME_I_FRAME:
				dst_buf->b->v4l2_buf.flags |=
						V4L2_BUF_FLAG_KEYFRAME;
				break;
			case S5P_FIMV_DECODE_FRAME_P_FRAME:
				dst_buf->b->v4l2_buf.flags |=
						V4L2_BUF_FLAG_PFRAME;
				break;
			case S5P_FIMV_DECODE_FRAME_B_FRAME:
				dst_buf->b->v4l2_buf.flags |=
						V4L2_BUF_FLAG_BFRAME;
				break;
			}
			break;
		}
	}
}

static void s5p_mfc_handle_frame_new(struct s5p_mfc_ctx *ctx, unsigned int err)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf  *dst_buf;
	size_t dspl_y_addr;
	unsigned int frame_type;

	dspl_y_addr = s5p_mfc_hw_call(dev->mfc_ops, get_dspl_y_adr, dev);
	frame_type = s5p_mfc_hw_call(dev->mfc_ops, get_disp_frame_type, ctx);

	/* If frame is same as previous then skip and do not dequeue */
	if (frame_type == S5P_FIMV_DECODE_FRAME_SKIPPED) {
		if (!ctx->after_packed_pb)
			ctx->sequence++;
		ctx->after_packed_pb = 0;
		return;
	}
	ctx->sequence++;
	/* The MFC returns address of the buffer, now we have to
	 * check which videobuf does it correspond to */
	list_for_each_entry(dst_buf, &ctx->dst_queue, list) {
		/* Check if this is the buffer we're looking for */
		if (vb2_dma_contig_plane_dma_addr(dst_buf->b, 0) == dspl_y_addr) {
			list_del(&dst_buf->list);
			ctx->dst_queue_cnt--;
			dst_buf->b->v4l2_buf.sequence = ctx->sequence;
			if (s5p_mfc_hw_call(dev->mfc_ops,
					get_pic_type_top, ctx) ==
				s5p_mfc_hw_call(dev->mfc_ops,
					get_pic_type_bot, ctx))
				dst_buf->b->v4l2_buf.field = V4L2_FIELD_NONE;
			else
				dst_buf->b->v4l2_buf.field =
							V4L2_FIELD_INTERLACED;
			vb2_set_plane_payload(dst_buf->b, 0, ctx->luma_size);
			vb2_set_plane_payload(dst_buf->b, 1, ctx->chroma_size);
			clear_bit(dst_buf->b->v4l2_buf.index,
							&ctx->dec_dst_flag);

			vb2_buffer_done(dst_buf->b,
				err ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);

			break;
		}
	}
}

/* Handle frame decoding interrupt */
static void s5p_mfc_handle_frame(struct s5p_mfc_ctx *ctx,
					unsigned int reason, unsigned int err)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int dst_frame_status;
	struct s5p_mfc_buf *src_buf;
	unsigned long flags;
	unsigned int res_change;

	dst_frame_status = s5p_mfc_hw_call(dev->mfc_ops, get_dspl_status, dev)
				& S5P_FIMV_DEC_STATUS_DECODING_STATUS_MASK;
	res_change = (s5p_mfc_hw_call(dev->mfc_ops, get_dspl_status, dev)
				& S5P_FIMV_DEC_STATUS_RESOLUTION_MASK)
				>> S5P_FIMV_DEC_STATUS_RESOLUTION_SHIFT;
	mfc_debug(2, "Frame Status: %x\n", dst_frame_status);
	if (ctx->state == MFCINST_RES_CHANGE_INIT)
		ctx->state = MFCINST_RES_CHANGE_FLUSH;
	if (res_change == S5P_FIMV_RES_INCREASE ||
		res_change == S5P_FIMV_RES_DECREASE) {
		ctx->state = MFCINST_RES_CHANGE_INIT;
		s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
		wake_up_ctx(ctx, reason, err);
		if (test_and_clear_bit(0, &dev->hw_lock) == 0)
			BUG();
		s5p_mfc_clock_off();
		s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
		return;
	}
	if (ctx->dpb_flush_flag)
		ctx->dpb_flush_flag = 0;

	spin_lock_irqsave(&dev->irqlock, flags);
	/* All frames remaining in the buffer have been extracted  */
	if (dst_frame_status == S5P_FIMV_DEC_STATUS_DECODING_EMPTY) {
		if (ctx->state == MFCINST_RES_CHANGE_FLUSH) {
			s5p_mfc_handle_frame_all_extracted(ctx);
			ctx->state = MFCINST_RES_CHANGE_END;
			goto leave_handle_frame;
		} else {
			s5p_mfc_handle_frame_all_extracted(ctx);
		}
	}

	if (dst_frame_status == S5P_FIMV_DEC_STATUS_DECODING_DISPLAY ||
		dst_frame_status == S5P_FIMV_DEC_STATUS_DECODING_ONLY)
		s5p_mfc_handle_frame_copy_time(ctx);

	/* A frame has been decoded and is in the buffer  */
	if (dst_frame_status == S5P_FIMV_DEC_STATUS_DISPLAY_ONLY ||
	    dst_frame_status == S5P_FIMV_DEC_STATUS_DECODING_DISPLAY) {
		s5p_mfc_handle_frame_new(ctx, err);
	} else {
		mfc_debug(2, "No frame decode\n");
	}
	/* Mark source buffer as complete */
	if (dst_frame_status != S5P_FIMV_DEC_STATUS_DISPLAY_ONLY
		&& !list_empty(&ctx->src_queue)) {
		src_buf = list_entry(ctx->src_queue.next, struct s5p_mfc_buf,
								list);
		ctx->consumed_stream += s5p_mfc_hw_call(dev->mfc_ops,
						get_consumed_stream, dev);
		if (ctx->codec_mode != S5P_MFC_CODEC_H264_DEC &&
			ctx->consumed_stream + STUFF_BYTE <
			src_buf->b->v4l2_planes[0].bytesused) {
			/* Run MFC again on the same buffer */
			mfc_debug(2, "Running again the same buffer\n");
			ctx->after_packed_pb = 1;
		} else {
			mfc_debug(2, "MFC needs next buffer\n");
			ctx->consumed_stream = 0;
			if (src_buf->flags & MFC_BUF_FLAG_EOS)
				ctx->state = MFCINST_FINISHING;
			list_del(&src_buf->list);
			ctx->src_queue_cnt--;
			if (s5p_mfc_hw_call(dev->mfc_ops, err_dec, err) > 0)
				vb2_buffer_done(src_buf->b, VB2_BUF_STATE_ERROR);
			else
				vb2_buffer_done(src_buf->b, VB2_BUF_STATE_DONE);
		}
	}
leave_handle_frame:
	spin_unlock_irqrestore(&dev->irqlock, flags);
	if ((ctx->src_queue_cnt == 0 && ctx->state != MFCINST_FINISHING)
				    || ctx->dst_queue_cnt < ctx->pb_count)
		clear_work_bit(ctx);
	s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	wake_up_ctx(ctx, reason, err);
	if (test_and_clear_bit(0, &dev->hw_lock) == 0)
		BUG();
	s5p_mfc_clock_off();
	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
}

/* Error handling for interrupt */
static void s5p_mfc_handle_error(struct s5p_mfc_dev *dev,
		struct s5p_mfc_ctx *ctx, unsigned int reason, unsigned int err)
{
	unsigned long flags;

	mfc_err("Interrupt Error: %08x\n", err);

	if (ctx != NULL) {
		/* Error recovery is dependent on the state of context */
		switch (ctx->state) {
		case MFCINST_RES_CHANGE_INIT:
		case MFCINST_RES_CHANGE_FLUSH:
		case MFCINST_RES_CHANGE_END:
		case MFCINST_FINISHING:
		case MFCINST_FINISHED:
		case MFCINST_RUNNING:
			/* It is highly probable that an error occurred
			 * while decoding a frame */
			clear_work_bit(ctx);
			ctx->state = MFCINST_ERROR;
			/* Mark all dst buffers as having an error */
			spin_lock_irqsave(&dev->irqlock, flags);
			s5p_mfc_hw_call(dev->mfc_ops, cleanup_queue,
						&ctx->dst_queue, &ctx->vq_dst);
			/* Mark all src buffers as having an error */
			s5p_mfc_hw_call(dev->mfc_ops, cleanup_queue,
						&ctx->src_queue, &ctx->vq_src);
			spin_unlock_irqrestore(&dev->irqlock, flags);
			wake_up_ctx(ctx, reason, err);
			break;
		default:
			clear_work_bit(ctx);
			ctx->state = MFCINST_ERROR;
			wake_up_ctx(ctx, reason, err);
			break;
		}
	}
	if (test_and_clear_bit(0, &dev->hw_lock) == 0)
		BUG();
	s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	s5p_mfc_clock_off();
	wake_up_dev(dev, reason, err);
	return;
}

/* Header parsing interrupt handling */
static void s5p_mfc_handle_seq_done(struct s5p_mfc_ctx *ctx,
				 unsigned int reason, unsigned int err)
{
	unsigned int status;
	struct s5p_mfc_dev *dev;

	if (ctx == NULL)
		return;
	dev = ctx->dev;
	if (ctx->c_ops->post_seq_start) {
		if (ctx->c_ops->post_seq_start(ctx))
			mfc_err("post_seq_start() failed\n");
	} else {
		ctx->img_width = s5p_mfc_hw_call(dev->mfc_ops, get_img_width,
				dev);
		ctx->img_height = s5p_mfc_hw_call(dev->mfc_ops, get_img_height,
				dev);

		status = s5p_mfc_hw_call(dev->mfc_ops, get_dspl_status, dev)
					& S5P_FIMV_DEC_STATUS_INTERLACE_MASK;
		ctx->interlace = status == S5P_FIMV_DEC_STATUS_INTERLACE;

		s5p_mfc_hw_call(dev->mfc_ops, dec_calc_dpb_size, ctx);

		ctx->pb_count = s5p_mfc_hw_call(dev->mfc_ops, get_dpb_count,
				dev);
		ctx->mv_count = s5p_mfc_hw_call(dev->mfc_ops, get_mv_count,
				dev);
		if (ctx->img_width == 0 || ctx->img_height == 0)
			ctx->state = MFCINST_ERROR;
		else
			ctx->state = MFCINST_HEAD_PARSED;

		if ((ctx->codec_mode == S5P_MFC_CODEC_H264_DEC ||
			ctx->codec_mode == S5P_MFC_CODEC_H264_MVC_DEC) &&
				!list_empty(&ctx->src_queue)) {
			struct s5p_mfc_buf *src_buf;
			src_buf = list_entry(ctx->src_queue.next,
					struct s5p_mfc_buf, list);
			if (s5p_mfc_hw_call(dev->mfc_ops, get_consumed_stream,
						dev) <
					src_buf->b->v4l2_planes[0].bytesused)
				ctx->head_processed = 0;
			else
				ctx->head_processed = 1;
		} else {
			ctx->head_processed = 1;
		}
	}
	s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	clear_work_bit(ctx);
	if (test_and_clear_bit(0, &dev->hw_lock) == 0)
		BUG();
	s5p_mfc_clock_off();
	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
	wake_up_ctx(ctx, reason, err);
}

/* Header parsing interrupt handling */
static void s5p_mfc_handle_init_buffers(struct s5p_mfc_ctx *ctx,
				 unsigned int reason, unsigned int err)
{
	struct s5p_mfc_buf *src_buf;
	struct s5p_mfc_dev *dev;
	unsigned long flags;

	if (ctx == NULL)
		return;
	dev = ctx->dev;
	s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	ctx->int_type = reason;
	ctx->int_err = err;
	ctx->int_cond = 1;
	clear_work_bit(ctx);
	if (err == 0) {
		ctx->state = MFCINST_RUNNING;
		if (!ctx->dpb_flush_flag && ctx->head_processed) {
			spin_lock_irqsave(&dev->irqlock, flags);
			if (!list_empty(&ctx->src_queue)) {
				src_buf = list_entry(ctx->src_queue.next,
					     struct s5p_mfc_buf, list);
				list_del(&src_buf->list);
				ctx->src_queue_cnt--;
				vb2_buffer_done(src_buf->b,
						VB2_BUF_STATE_DONE);
			}
			spin_unlock_irqrestore(&dev->irqlock, flags);
		} else {
			ctx->dpb_flush_flag = 0;
		}
		if (test_and_clear_bit(0, &dev->hw_lock) == 0)
			BUG();

		s5p_mfc_clock_off();

		wake_up(&ctx->queue);
		s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
	} else {
		if (test_and_clear_bit(0, &dev->hw_lock) == 0)
			BUG();

		s5p_mfc_clock_off();

		wake_up(&ctx->queue);
	}
}

static void s5p_mfc_handle_stream_complete(struct s5p_mfc_ctx *ctx,
				 unsigned int reason, unsigned int err)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *mb_entry;

	mfc_debug(2, "Stream completed\n");

	s5p_mfc_clear_int_flags(dev);
	ctx->int_type = reason;
	ctx->int_err = err;
	ctx->state = MFCINST_FINISHED;

	spin_lock(&dev->irqlock);
	if (!list_empty(&ctx->dst_queue)) {
		mb_entry = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf,
									list);
		list_del(&mb_entry->list);
		ctx->dst_queue_cnt--;
		vb2_set_plane_payload(mb_entry->b, 0, 0);
		vb2_buffer_done(mb_entry->b, VB2_BUF_STATE_DONE);
	}
	spin_unlock(&dev->irqlock);

	clear_work_bit(ctx);

	WARN_ON(test_and_clear_bit(0, &dev->hw_lock) == 0);

	s5p_mfc_clock_off();
	wake_up(&ctx->queue);
	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
}

/* Interrupt processing */
static irqreturn_t s5p_mfc_irq(int irq, void *priv)
{
	struct s5p_mfc_dev *dev = priv;
	struct s5p_mfc_ctx *ctx;
	unsigned int reason;
	unsigned int err;

	mfc_debug_enter();
	/* Reset the timeout watchdog */
	atomic_set(&dev->watchdog_cnt, 0);
	ctx = dev->ctx[dev->curr_ctx];
	/* Get the reason of interrupt and the error code */
	reason = s5p_mfc_hw_call(dev->mfc_ops, get_int_reason, dev);
	err = s5p_mfc_hw_call(dev->mfc_ops, get_int_err, dev);
	mfc_debug(1, "Int reason: %d (err: %08x)\n", reason, err);
	switch (reason) {
	case S5P_MFC_R2H_CMD_ERR_RET:
		/* An error has occurred */
		if (ctx->state == MFCINST_RUNNING &&
			(s5p_mfc_hw_call(dev->mfc_ops, err_dec, err) >=
				dev->warn_start ||
				err == S5P_FIMV_ERR_INCOMPLETE_FRAME))
			s5p_mfc_handle_frame(ctx, reason, err);
		else
			s5p_mfc_handle_error(dev, ctx, reason, err);
		clear_bit(0, &dev->enter_suspend);
		break;

	case S5P_MFC_R2H_CMD_SLICE_DONE_RET:
	case S5P_MFC_R2H_CMD_FIELD_DONE_RET:
	case S5P_MFC_R2H_CMD_FRAME_DONE_RET:
		if (ctx->c_ops->post_frame_start) {
			if (ctx->c_ops->post_frame_start(ctx))
				mfc_err("post_frame_start() failed\n");
			s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
			wake_up_ctx(ctx, reason, err);
			if (test_and_clear_bit(0, &dev->hw_lock) == 0)
				BUG();
			s5p_mfc_clock_off();
			s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
		} else {
			s5p_mfc_handle_frame(ctx, reason, err);
		}
		break;

	case S5P_MFC_R2H_CMD_SEQ_DONE_RET:
		s5p_mfc_handle_seq_done(ctx, reason, err);
		break;

	case S5P_MFC_R2H_CMD_OPEN_INSTANCE_RET:
		ctx->inst_no = s5p_mfc_hw_call(dev->mfc_ops, get_inst_no, dev);
		ctx->state = MFCINST_GOT_INST;
		clear_work_bit(ctx);
		wake_up(&ctx->queue);
		goto irq_cleanup_hw;

	case S5P_MFC_R2H_CMD_CLOSE_INSTANCE_RET:
		clear_work_bit(ctx);
		ctx->state = MFCINST_FREE;
		wake_up(&ctx->queue);
		goto irq_cleanup_hw;

	case S5P_MFC_R2H_CMD_SYS_INIT_RET:
	case S5P_MFC_R2H_CMD_FW_STATUS_RET:
	case S5P_MFC_R2H_CMD_SLEEP_RET:
	case S5P_MFC_R2H_CMD_WAKEUP_RET:
		if (ctx)
			clear_work_bit(ctx);
		s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
		wake_up_dev(dev, reason, err);
		clear_bit(0, &dev->hw_lock);
		clear_bit(0, &dev->enter_suspend);
		break;

	case S5P_MFC_R2H_CMD_INIT_BUFFERS_RET:
		s5p_mfc_handle_init_buffers(ctx, reason, err);
		break;

	case S5P_MFC_R2H_CMD_COMPLETE_SEQ_RET:
		s5p_mfc_handle_stream_complete(ctx, reason, err);
		break;

	case S5P_MFC_R2H_CMD_DPB_FLUSH_RET:
		clear_work_bit(ctx);
		ctx->state = MFCINST_RUNNING;
		wake_up(&ctx->queue);
		goto irq_cleanup_hw;

	default:
		mfc_debug(2, "Unknown int reason\n");
		s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	}
	mfc_debug_leave();
	return IRQ_HANDLED;
irq_cleanup_hw:
	s5p_mfc_hw_call(dev->mfc_ops, clear_int_flags, dev);
	ctx->int_type = reason;
	ctx->int_err = err;
	ctx->int_cond = 1;
	if (test_and_clear_bit(0, &dev->hw_lock) == 0)
		mfc_err("Failed to unlock hw\n");

	s5p_mfc_clock_off();

	s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
	mfc_debug(2, "Exit via irq_cleanup_hw\n");
	return IRQ_HANDLED;
}

/* Open an MFC node */
static int s5p_mfc_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct s5p_mfc_dev *dev = video_drvdata(file);
	struct s5p_mfc_ctx *ctx = NULL;
	struct vb2_queue *q;
	int ret = 0;

	mfc_debug_enter();
	if (mutex_lock_interruptible(&dev->mfc_mutex))
		return -ERESTARTSYS;
	dev->num_inst++;	/* It is guarded by mfc_mutex in vfd */
	/* Allocate memory for context */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mfc_err("Not enough memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	v4l2_fh_init(&ctx->fh, vdev);
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->dev = dev;
	INIT_LIST_HEAD(&ctx->src_queue);
	INIT_LIST_HEAD(&ctx->dst_queue);
	ctx->src_queue_cnt = 0;
	ctx->dst_queue_cnt = 0;
	/* Get context number */
	ctx->num = 0;
	while (dev->ctx[ctx->num]) {
		ctx->num++;
		if (ctx->num >= MFC_NUM_CONTEXTS) {
			mfc_err("Too many open contexts\n");
			ret = -EBUSY;
			goto err_no_ctx;
		}
	}
	/* Mark context as idle */
	clear_work_bit_irqsave(ctx);
	dev->ctx[ctx->num] = ctx;
	if (vdev == dev->vfd_dec) {
		ctx->type = MFCINST_DECODER;
		ctx->c_ops = get_dec_codec_ops();
		s5p_mfc_dec_init(ctx);
		/* Setup ctrl handler */
		ret = s5p_mfc_dec_ctrls_setup(ctx);
		if (ret) {
			mfc_err("Failed to setup mfc controls\n");
			goto err_ctrls_setup;
		}
	} else if (vdev == dev->vfd_enc) {
		ctx->type = MFCINST_ENCODER;
		ctx->c_ops = get_enc_codec_ops();
		/* only for encoder */
		INIT_LIST_HEAD(&ctx->ref_queue);
		ctx->ref_queue_cnt = 0;
		s5p_mfc_enc_init(ctx);
		/* Setup ctrl handler */
		ret = s5p_mfc_enc_ctrls_setup(ctx);
		if (ret) {
			mfc_err("Failed to setup mfc controls\n");
			goto err_ctrls_setup;
		}
	} else {
		ret = -ENOENT;
		goto err_bad_node;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	ctx->inst_no = -1;
	/* Load firmware if this is the first instance */
	if (dev->num_inst == 1) {
		dev->watchdog_timer.expires = jiffies +
					msecs_to_jiffies(MFC_WATCHDOG_INTERVAL);
		add_timer(&dev->watchdog_timer);
		ret = s5p_mfc_power_on();
		if (ret < 0) {
			mfc_err("power on failed\n");
			goto err_pwr_enable;
		}
		s5p_mfc_clock_on();
		ret = s5p_mfc_load_firmware(dev);
		if (ret) {
			s5p_mfc_clock_off();
			goto err_load_fw;
		}
		/* Init the FW */
		ret = s5p_mfc_init_hw(dev);
		s5p_mfc_clock_off();
		if (ret)
			goto err_init_hw;
	}
	/* Init videobuf2 queue for CAPTURE */
	q = &ctx->vq_dst;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->drv_priv = &ctx->fh;
	if (vdev == dev->vfd_dec) {
		q->io_modes = VB2_MMAP;
#ifdef CONFIG_EXYNOS_IOMMU
		q->io_modes = q->io_modes | VB2_USERPTR | VB2_DMABUF;
#endif
		q->ops = get_dec_queue_ops();
	} else if (vdev == dev->vfd_enc) {
		q->io_modes = VB2_MMAP | VB2_USERPTR;
#ifdef CONFIG_EXYNOS_IOMMU
		q->io_modes = q->io_modes | VB2_DMABUF;
#endif
		q->ops = get_enc_queue_ops();
	} else {
		ret = -ENOENT;
		goto err_queue_init;
	}
	q->mem_ops = (struct vb2_mem_ops *)&vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	ret = vb2_queue_init(q);
	if (ret) {
		mfc_err("Failed to initialize videobuf2 queue(capture)\n");
		goto err_queue_init;
	}
	/* Init videobuf2 queue for OUTPUT */
	q = &ctx->vq_src;
	q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	q->io_modes = VB2_MMAP;
	q->drv_priv = &ctx->fh;
	if (vdev == dev->vfd_dec) {
		q->io_modes = VB2_MMAP;
#ifdef CONFIG_EXYNOS_IOMMU
		q->io_modes = q->io_modes | VB2_USERPTR | VB2_DMABUF;
#endif
		q->ops = get_dec_queue_ops();
	} else if (vdev == dev->vfd_enc) {
		q->io_modes = VB2_MMAP | VB2_USERPTR;
#ifdef CONFIG_EXYNOS_IOMMU
		q->io_modes = q->io_modes | VB2_DMABUF;
#endif
		q->ops = get_enc_queue_ops();
	} else {
		ret = -ENOENT;
		goto err_queue_init;
	}
	q->mem_ops = (struct vb2_mem_ops *)&vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	ret = vb2_queue_init(q);
	if (ret) {
		mfc_err("Failed to initialize videobuf2 queue(output)\n");
		goto err_queue_init;
	}
	init_waitqueue_head(&ctx->queue);
	mutex_unlock(&dev->mfc_mutex);
	mfc_debug_leave();
	return ret;
	/* Deinit when failure occurred */
err_queue_init:
	if (dev->num_inst == 1)
		s5p_mfc_deinit_hw(dev);
err_init_hw:
err_load_fw:
err_pwr_enable:
	if (dev->num_inst == 1) {
		if (s5p_mfc_power_off() < 0)
			mfc_err("power off failed\n");
		del_timer_sync(&dev->watchdog_timer);
	}
err_ctrls_setup:
	s5p_mfc_dec_ctrls_delete(ctx);
err_bad_node:
	dev->ctx[ctx->num] = NULL;
err_no_ctx:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
err_alloc:
	dev->num_inst--;
	mutex_unlock(&dev->mfc_mutex);
	mfc_debug_leave();
	return ret;
}

/* Release MFC context */
static int s5p_mfc_release(struct file *file)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(file->private_data);
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_debug_enter();
	mutex_lock(&dev->mfc_mutex);
	s5p_mfc_clock_on();
	vb2_queue_release(&ctx->vq_src);
	vb2_queue_release(&ctx->vq_dst);
	/* Mark context as idle */
	clear_work_bit_irqsave(ctx);
	/* If instance was initialised then
	 * return instance and free resources */
	if (ctx->inst_no != MFC_NO_INSTANCE_SET) {
		mfc_debug(2, "Has to free instance\n");
		ctx->state = MFCINST_RETURN_INST;
		set_work_bit_irqsave(ctx);
		s5p_mfc_clean_ctx_int_flags(ctx);
		s5p_mfc_hw_call(dev->mfc_ops, try_run, dev);
		/* Wait until instance is returned or timeout occurred */
		if (s5p_mfc_wait_for_done_ctx
		    (ctx, S5P_MFC_R2H_CMD_CLOSE_INSTANCE_RET, 0)) {
			s5p_mfc_clock_off();
			mfc_err("Err returning instance\n");
		}
		mfc_debug(2, "After free instance\n");
		/* Free resources */
		s5p_mfc_hw_call(dev->mfc_ops, release_codec_buffers, ctx);
		s5p_mfc_hw_call(dev->mfc_ops, release_instance_buffer, ctx);
		if (ctx->type == MFCINST_DECODER)
			s5p_mfc_hw_call(dev->mfc_ops, release_dec_desc_buffer,
					ctx);

		ctx->inst_no = MFC_NO_INSTANCE_SET;
	}
	/* hardware locking scheme */
	if (dev->curr_ctx == ctx->num)
		clear_bit(0, &dev->hw_lock);
	dev->num_inst--;
	if (dev->num_inst == 0) {
		mfc_debug(2, "Last instance\n");
		s5p_mfc_deinit_hw(dev);
		del_timer_sync(&dev->watchdog_timer);
		if (s5p_mfc_power_off() < 0)
			mfc_err("Power off failed\n");
	}
	mfc_debug(2, "Shutting down clock\n");
	s5p_mfc_clock_off();
	dev->ctx[ctx->num] = NULL;
	s5p_mfc_dec_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mfc_debug_leave();
	mutex_unlock(&dev->mfc_mutex);
	return 0;
}

/* Poll */
static unsigned int s5p_mfc_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(file->private_data);
	struct s5p_mfc_dev *dev = ctx->dev;
	struct vb2_queue *src_q, *dst_q;
	struct vb2_buffer *src_vb = NULL, *dst_vb = NULL;
	unsigned int rc = 0;
	unsigned long flags;

	mutex_lock(&dev->mfc_mutex);
	src_q = &ctx->vq_src;
	dst_q = &ctx->vq_dst;
	/*
	 * There has to be at least one buffer queued on each queued_list, which
	 * means either in driver already or waiting for driver to claim it
	 * and start processing.
	 */
	if ((!src_q->streaming || list_empty(&src_q->queued_list))
		&& (!dst_q->streaming || list_empty(&dst_q->queued_list))) {
		rc = POLLERR;
		goto end;
	}
	mutex_unlock(&dev->mfc_mutex);
	poll_wait(file, &ctx->fh.wait, wait);
	poll_wait(file, &src_q->done_wq, wait);
	poll_wait(file, &dst_q->done_wq, wait);
	mutex_lock(&dev->mfc_mutex);
	if (v4l2_event_pending(&ctx->fh))
		rc |= POLLPRI;
	spin_lock_irqsave(&src_q->done_lock, flags);
	if (!list_empty(&src_q->done_list))
		src_vb = list_first_entry(&src_q->done_list, struct vb2_buffer,
								done_entry);
	if (src_vb && (src_vb->state == VB2_BUF_STATE_DONE
				|| src_vb->state == VB2_BUF_STATE_ERROR))
		rc |= POLLOUT | POLLWRNORM;
	spin_unlock_irqrestore(&src_q->done_lock, flags);
	spin_lock_irqsave(&dst_q->done_lock, flags);
	if (!list_empty(&dst_q->done_list))
		dst_vb = list_first_entry(&dst_q->done_list, struct vb2_buffer,
								done_entry);
	if (dst_vb && (dst_vb->state == VB2_BUF_STATE_DONE
				|| dst_vb->state == VB2_BUF_STATE_ERROR))
		rc |= POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&dst_q->done_lock, flags);
end:
	mutex_unlock(&dev->mfc_mutex);
	return rc;
}

/* Mmap */
static int s5p_mfc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(file->private_data);
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int ret;

	if (mutex_lock_interruptible(&dev->mfc_mutex))
		return -ERESTARTSYS;
	if (offset < DST_QUEUE_OFF_BASE) {
		mfc_debug(2, "mmaping source\n");
		ret = vb2_mmap(&ctx->vq_src, vma);
	} else {		/* capture */
		mfc_debug(2, "mmaping destination\n");
		vma->vm_pgoff -= (DST_QUEUE_OFF_BASE >> PAGE_SHIFT);
		ret = vb2_mmap(&ctx->vq_dst, vma);
	}
	mutex_unlock(&dev->mfc_mutex);
	return ret;
}

/* v4l2 ops */
static const struct v4l2_file_operations s5p_mfc_fops = {
	.owner = THIS_MODULE,
	.open = s5p_mfc_open,
	.release = s5p_mfc_release,
	.poll = s5p_mfc_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = s5p_mfc_mmap,
};

static const char *s5p_mfc_children_names[] = {"s5p-mfc-l", "s5p-mfc-r"};
enum { MFC_PORT_L, MFC_PORT_R};

static int match_child(struct device *dev, void *data)
{
	const __be32 *reg = of_get_property(dev->of_node, "reg", NULL);
	int num = (int)data;

	return (strcmp(dev_name(dev), s5p_mfc_children_names[num]) == 0 ||
		(reg && be32_to_cpup(reg) == num));
}

static void *mfc_get_drv_data(struct platform_device *pdev);

/* MFC probe function */
static int s5p_mfc_probe(struct platform_device *pdev)
{
	struct s5p_mfc_dev *dev;
	struct video_device *vfd;
	struct resource *res;
	int ret;

	pr_debug("%s++\n", __func__);
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "Not enough memory for MFC device\n");
		return -ENOMEM;
	}

	spin_lock_init(&dev->irqlock);
	spin_lock_init(&dev->condlock);
	dev->plat_dev = pdev;
	dev->variant = mfc_get_drv_data(pdev);

	ret = s5p_mfc_init_pm(dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get mfc clock source\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev->regs_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs_base))
		return PTR_ERR(dev->regs_base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		ret = -ENOENT;
		goto err_res;
	}
	dev->irq = res->start;
	ret = devm_request_irq(&pdev->dev, dev->irq, s5p_mfc_irq,
					IRQF_DISABLED, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to install irq (%d)\n", ret);
		goto err_res;
	}

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		goto err_res;

	dev->mem_dev_l = device_find_child(&dev->plat_dev->dev,
					   (void *)MFC_PORT_L, match_child);
	if (!dev->mem_dev_l) {
		mfc_err("Mem child (L) device get failed\n");
		ret = -ENODEV;
		goto err_res;
	}

	dev->mem_dev_r = device_find_child(&dev->plat_dev->dev,
					   (void *)MFC_PORT_R, match_child);
	if (!dev->mem_dev_r) {
		mfc_err("Mem child (R) device get failed\n");
		ret = -ENODEV;
		goto err_res;
	}

	dev->alloc_ctx[0] = vb2_dma_contig_init_ctx(dev->mem_dev_l);
	if (IS_ERR(dev->alloc_ctx[0])) {
		ret = PTR_ERR(dev->alloc_ctx[0]);
		goto err_res;
	}
	dev->alloc_ctx[1] = vb2_dma_contig_init_ctx(dev->mem_dev_r);
	if (IS_ERR(dev->alloc_ctx[1])) {
		ret = PTR_ERR(dev->alloc_ctx[1]);
		goto err_mem_init_ctx_1;
	}

	mutex_init(&dev->mfc_mutex);

	ret = s5p_mfc_alloc_firmware(dev);
	if (ret)
		goto err_alloc_fw;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto err_v4l2_dev_reg;
	init_waitqueue_head(&dev->queue);

	/* decoder */
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_dec_alloc;
	}
	vfd->fops	= &s5p_mfc_fops,
	vfd->ioctl_ops	= get_dec_v4l2_ioctl_ops();
	vfd->release	= video_device_release,
	vfd->lock	= &dev->mfc_mutex;
	vfd->v4l2_dev	= &dev->v4l2_dev;
	vfd->vfl_dir	= VFL_DIR_M2M;
	snprintf(vfd->name, sizeof(vfd->name), "%s", S5P_MFC_DEC_NAME);
	dev->vfd_dec	= vfd;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		video_device_release(vfd);
		goto err_dec_alloc;
	}
	v4l2_info(&dev->v4l2_dev,
		  "decoder registered as /dev/video%d\n", vfd->num);
	video_set_drvdata(vfd, dev);

	/* encoder */
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_enc_alloc;
	}
	vfd->fops	= &s5p_mfc_fops,
	vfd->ioctl_ops	= get_enc_v4l2_ioctl_ops();
	vfd->release	= video_device_release,
	vfd->lock	= &dev->mfc_mutex;
	vfd->v4l2_dev	= &dev->v4l2_dev;
	vfd->vfl_dir	= VFL_DIR_M2M;
	snprintf(vfd->name, sizeof(vfd->name), "%s", S5P_MFC_ENC_NAME);
	dev->vfd_enc	= vfd;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		video_device_release(vfd);
		goto err_enc_alloc;
	}
	v4l2_info(&dev->v4l2_dev,
		  "encoder registered as /dev/video%d\n", vfd->num);
	video_set_drvdata(vfd, dev);
	platform_set_drvdata(pdev, dev);

	dev->hw_lock = 0;
	dev->watchdog_workqueue = create_singlethread_workqueue(S5P_MFC_NAME);
	INIT_WORK(&dev->watchdog_work, s5p_mfc_watchdog_worker);
	atomic_set(&dev->watchdog_cnt, 0);
	init_timer(&dev->watchdog_timer);
	dev->watchdog_timer.data = (unsigned long)dev;
	dev->watchdog_timer.function = s5p_mfc_watchdog;

	/* Initialize HW ops and commands based on MFC version */
	s5p_mfc_init_hw_ops(dev);
	s5p_mfc_init_hw_cmds(dev);

	pr_debug("%s--\n", __func__);
	return 0;

/* Deinit MFC if probe had failed */
err_enc_alloc:
	video_unregister_device(dev->vfd_dec);
err_dec_alloc:
	v4l2_device_unregister(&dev->v4l2_dev);
err_v4l2_dev_reg:
	s5p_mfc_release_firmware(dev);
err_alloc_fw:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx[1]);
err_mem_init_ctx_1:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx[0]);
err_res:
	s5p_mfc_final_pm(dev);

	pr_debug("%s-- with error\n", __func__);
	return ret;

}

/* Remove the driver */
static int s5p_mfc_remove(struct platform_device *pdev)
{
	struct s5p_mfc_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing %s\n", pdev->name);

	del_timer_sync(&dev->watchdog_timer);
	flush_workqueue(dev->watchdog_workqueue);
	destroy_workqueue(dev->watchdog_workqueue);

	video_unregister_device(dev->vfd_enc);
	video_unregister_device(dev->vfd_dec);
	v4l2_device_unregister(&dev->v4l2_dev);
	s5p_mfc_release_firmware(dev);
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx[0]);
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx[1]);
	if (pdev->dev.of_node) {
		put_device(dev->mem_dev_l);
		put_device(dev->mem_dev_r);
	}

	s5p_mfc_final_pm(dev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int s5p_mfc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_mfc_dev *m_dev = platform_get_drvdata(pdev);
	int ret;

	if (m_dev->num_inst == 0)
		return 0;

	if (test_and_set_bit(0, &m_dev->enter_suspend) != 0) {
		mfc_err("Error: going to suspend for a second time\n");
		return -EIO;
	}

	/* Check if we're processing then wait if it necessary. */
	while (test_and_set_bit(0, &m_dev->hw_lock) != 0) {
		/* Try and lock the HW */
		/* Wait on the interrupt waitqueue */
		ret = wait_event_interruptible_timeout(m_dev->queue,
			m_dev->int_cond || m_dev->ctx[m_dev->curr_ctx]->int_cond,
			msecs_to_jiffies(MFC_INT_TIMEOUT));

		if (ret == 0) {
			mfc_err("Waiting for hardware to finish timed out\n");
			return -EIO;
		}
	}

	return s5p_mfc_sleep(m_dev);
}

static int s5p_mfc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_mfc_dev *m_dev = platform_get_drvdata(pdev);

	if (m_dev->num_inst == 0)
		return 0;
	return s5p_mfc_wakeup(m_dev);
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int s5p_mfc_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_mfc_dev *m_dev = platform_get_drvdata(pdev);

	atomic_set(&m_dev->pm.power, 0);
	return 0;
}

static int s5p_mfc_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_mfc_dev *m_dev = platform_get_drvdata(pdev);
	int pre_power;

	if (!m_dev->alloc_ctx)
		return 0;
	pre_power = atomic_read(&m_dev->pm.power);
	atomic_set(&m_dev->pm.power, 1);
	return 0;
}
#endif

/* Power management */
static const struct dev_pm_ops s5p_mfc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(s5p_mfc_suspend, s5p_mfc_resume)
	SET_RUNTIME_PM_OPS(s5p_mfc_runtime_suspend, s5p_mfc_runtime_resume,
			   NULL)
};

struct s5p_mfc_buf_size_v5 mfc_buf_size_v5 = {
	.h264_ctx	= MFC_H264_CTX_BUF_SIZE,
	.non_h264_ctx	= MFC_CTX_BUF_SIZE,
	.dsc		= DESC_BUF_SIZE,
	.shm		= SHARED_BUF_SIZE,
};

struct s5p_mfc_buf_size buf_size_v5 = {
	.fw	= MAX_FW_SIZE,
	.cpb	= MAX_CPB_SIZE,
	.priv	= &mfc_buf_size_v5,
};

struct s5p_mfc_buf_align mfc_buf_align_v5 = {
	.base = MFC_BASE_ALIGN_ORDER,
};

static struct s5p_mfc_variant mfc_drvdata_v5 = {
	.version	= MFC_VERSION,
	.port_num	= MFC_NUM_PORTS,
	.buf_size	= &buf_size_v5,
	.buf_align	= &mfc_buf_align_v5,
	.fw_name	= "s5p-mfc.fw",
};

struct s5p_mfc_buf_size_v6 mfc_buf_size_v6 = {
	.dev_ctx	= MFC_CTX_BUF_SIZE_V6,
	.h264_dec_ctx	= MFC_H264_DEC_CTX_BUF_SIZE_V6,
	.other_dec_ctx	= MFC_OTHER_DEC_CTX_BUF_SIZE_V6,
	.h264_enc_ctx	= MFC_H264_ENC_CTX_BUF_SIZE_V6,
	.other_enc_ctx	= MFC_OTHER_ENC_CTX_BUF_SIZE_V6,
};

struct s5p_mfc_buf_size buf_size_v6 = {
	.fw	= MAX_FW_SIZE_V6,
	.cpb	= MAX_CPB_SIZE_V6,
	.priv	= &mfc_buf_size_v6,
};

struct s5p_mfc_buf_align mfc_buf_align_v6 = {
	.base = 0,
};

static struct s5p_mfc_variant mfc_drvdata_v6 = {
	.version	= MFC_VERSION_V6,
	.port_num	= MFC_NUM_PORTS_V6,
	.buf_size	= &buf_size_v6,
	.buf_align	= &mfc_buf_align_v6,
	.fw_name        = "s5p-mfc-v6.fw",
};

static struct platform_device_id mfc_driver_ids[] = {
	{
		.name = "s5p-mfc",
		.driver_data = (unsigned long)&mfc_drvdata_v5,
	}, {
		.name = "s5p-mfc-v5",
		.driver_data = (unsigned long)&mfc_drvdata_v5,
	}, {
		.name = "s5p-mfc-v6",
		.driver_data = (unsigned long)&mfc_drvdata_v6,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, mfc_driver_ids);

static const struct of_device_id exynos_mfc_match[] = {
	{
		.compatible = "samsung,mfc-v5",
		.data = &mfc_drvdata_v5,
	}, {
		.compatible = "samsung,mfc-v6",
		.data = &mfc_drvdata_v6,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_mfc_match);

static void *mfc_get_drv_data(struct platform_device *pdev)
{
	struct s5p_mfc_variant *driver_data = NULL;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(of_match_ptr(exynos_mfc_match),
				pdev->dev.of_node);
		if (match)
			driver_data = (struct s5p_mfc_variant *)match->data;
	} else {
		driver_data = (struct s5p_mfc_variant *)
			platform_get_device_id(pdev)->driver_data;
	}
	return driver_data;
}

static struct platform_driver s5p_mfc_driver = {
	.probe		= s5p_mfc_probe,
	.remove		= s5p_mfc_remove,
	.id_table	= mfc_driver_ids,
	.driver	= {
		.name	= S5P_MFC_NAME,
		.owner	= THIS_MODULE,
		.pm	= &s5p_mfc_pm_ops,
		.of_match_table = exynos_mfc_match,
	},
};

module_platform_driver(s5p_mfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_DESCRIPTION("Samsung S5P Multi Format Codec V4L2 driver");

