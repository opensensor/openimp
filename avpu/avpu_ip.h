#pragma once

#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "avpu_ioctl.h"
#include "avpu_alloc.h"

#define AVPU_NR_DEVS 4

#if defined(CONFIG_SOC_T31) || defined(CONFIG_SOC_T40)
#define AVPU_BASE_OFFSET 0x8000
#elif defined(CONFIG_SOC_T41)
#define AVPU_BASE_OFFSET 0x0000
#endif

#define AXI_ADDR_OFFSET_IP (AVPU_BASE_OFFSET + 0x1208)
#define AVPU_INTERRUPT_MASK (AVPU_BASE_OFFSET + 0x14)
#define AVPU_INTERRUPT (AVPU_BASE_OFFSET + 0x18)

#define avpu_writel(val, reg) iowrite32(val, codec->regs + reg)
#define avpu_readl(reg) ioread32(codec->regs + reg)

#define avpu_dbg(format, ...) \
	dev_dbg(codec->device, format, ## __VA_ARGS__)

#define avpu_info(format, ...) \
	dev_info(codec->device, format, ## __VA_ARGS__)

#define avpu_err(format, ...) \
	dev_err(codec->device, format, ## __VA_ARGS__)

struct avpu_codec_desc;
struct dma_buf_info {
	struct avpu_dma_buffer *buffer;
	struct avpu_codec_desc *codec;
};

/* Interrupt sources the hard IRQ handler forwards to userspace. */
#define AVPU_IRQ_SOURCES 20
/* Pending interrupt indices, delivered one per AL_CMD_IP_WAIT_IRQ. Must be a
 * power of two. A frame raises a handful of interrupts and the waiter drains
 * them immediately, so the queue only fills when nobody is waiting. */
#define AVPU_IRQ_QUEUE_LEN 256

struct avpu_codec_desc {
	struct device *device;
	void __iomem *regs;             /* Base addr for regs */
	unsigned long regs_size;        /* end addr for regs */
	struct cdev cdev;
	/* one for one mapping in the no mcu case */
	struct avpu_codec_chan *chan;
	/* i_lock protects chan, the irq queue and irq_dropped */
	spinlock_t i_lock;
	u8 irq_queue[AVPU_IRQ_QUEUE_LEN];
	unsigned int irq_head;
	unsigned int irq_tail;
	unsigned int irq_dropped;
	int irq;                        /* -1 when running without irq */
	int minor;
	struct clk          *clk;
	struct clk          *clk_mux;
	struct clk          *clk_gate;
#ifdef CONFIG_SOC_T41
	struct clk          *clk_gate_ivdc;
#endif
	struct clk          *ahb1_gate;
};

struct avpu_dma_buf_mmap {
	struct list_head list;
	struct avpu_dma_buffer *buf;
	int buf_id;
};

struct avpu_codec_chan {
	wait_queue_head_t irq_queue;
	int unblock;
	spinlock_t lock;
	struct list_head mem;
	int num_bufs;
	struct avpu_codec_desc *codec;
};

int avpu_codec_bind_channel(struct avpu_codec_chan *chan,
			    struct inode *inode);
void avpu_codec_unbind_channel(struct avpu_codec_chan *chan);
int avpu_codec_read_register(struct avpu_codec_chan *chan,
			     struct avpu_reg *reg);
void avpu_codec_write_register(struct avpu_codec_chan *chan,
			       struct avpu_reg *reg);
irqreturn_t avpu_hardirq_handler(int irq, void *data);
void avpu_irq_queue_reset(struct avpu_codec_desc *codec);
int avpu_irq_queue_pop(struct avpu_codec_desc *codec, u32 *irq_idx);

extern bool avpu_trace;

#define avpu_trace_log(format, ...)				\
	do {							\
		if (unlikely(avpu_trace))			\
			pr_info("[AVPU] " format, ## __VA_ARGS__); \
	} while (0)
