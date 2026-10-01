#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "avpu_ip.h"

/* Caller holds codec->i_lock. */
static void avpu_irq_queue_reset_locked(struct avpu_codec_desc *codec)
{
	while (codec->irq_head != codec->irq_tail) {
		avpu_err("Previous channel lost irq:%x\n",
			 codec->irq_queue[codec->irq_tail]);
		codec->irq_tail = (codec->irq_tail + 1) & (AVPU_IRQ_QUEUE_LEN - 1);
	}
	if (codec->irq_dropped) {
		avpu_err("Previous channel dropped %u irqs on queue overflow\n",
			 codec->irq_dropped);
		codec->irq_dropped = 0;
	}
}

void avpu_irq_queue_reset(struct avpu_codec_desc *codec)
{
	unsigned long flags;

	spin_lock_irqsave(&codec->i_lock, flags);
	codec->irq_head = 0;
	codec->irq_tail = 0;
	codec->irq_dropped = 0;
	spin_unlock_irqrestore(&codec->i_lock, flags);
}

/* Returns 1 and stores the oldest pending interrupt index, or 0 when the
 * queue is empty. */
int avpu_irq_queue_pop(struct avpu_codec_desc *codec, u32 *irq_idx)
{
	unsigned long flags;
	unsigned int dropped = 0;
	int ret = 0;

	spin_lock_irqsave(&codec->i_lock, flags);
	if (codec->irq_head != codec->irq_tail) {
		*irq_idx = codec->irq_queue[codec->irq_tail];
		codec->irq_tail = (codec->irq_tail + 1) & (AVPU_IRQ_QUEUE_LEN - 1);
		ret = 1;
	}
	dropped = codec->irq_dropped;
	codec->irq_dropped = 0;
	spin_unlock_irqrestore(&codec->i_lock, flags);

	if (dropped)
		avpu_err("irq queue overflow, %u interrupts dropped\n", dropped);

	return ret;
}

int avpu_codec_bind_channel(struct avpu_codec_chan *chan,
			    struct inode *inode)
{
	struct avpu_codec_desc *codec;
	unsigned long flags;
	int ret = 0;

	codec = container_of(inode->i_cdev, struct avpu_codec_desc, cdev);

	spin_lock_irqsave(&codec->i_lock, flags);

	chan->codec = codec;
	/* No mcu, there is a one for one mapping */
	if (codec->chan != NULL) {
		ret = -ENODEV;
		goto unlock;
	}

	/* Only a bound channel holds a clock reference: a refused open()
	 * never reaches release() and could not drop it again. */
	clk_enable(codec->clk);
	clk_enable(codec->ahb1_gate);
	clk_enable(codec->clk_gate);

	avpu_irq_queue_reset_locked(codec);

	codec->chan = chan;

unlock:
	spin_unlock_irqrestore(&codec->i_lock, flags);
	return ret;

}

void avpu_codec_unbind_channel(struct avpu_codec_chan *chan)
{
	struct avpu_codec_desc *codec;
	unsigned long flags;

	codec = chan->codec;
	spin_lock_irqsave(&codec->i_lock, flags);

	clk_disable(codec->clk);
	clk_disable(codec->clk_gate);
	clk_disable(codec->ahb1_gate);

	codec->chan = NULL;

	spin_unlock_irqrestore(&codec->i_lock, flags);
}

int avpu_codec_read_register(struct avpu_codec_chan *chan,
			     struct avpu_reg *reg)
{
	struct avpu_codec_desc *codec = chan->codec;

	if (!chan->codec->regs) {
		avpu_err("Registers not mapped\n");
		return -EINVAL;
	}
	reg->value = ioread32(chan->codec->regs + reg->id);

	return 0;
}

void avpu_codec_write_register(struct avpu_codec_chan *chan,
			       struct avpu_reg *reg)
{
	struct avpu_codec_desc *codec = chan->codec;

	if (!chan->codec->regs) {
		avpu_err("Registers not mapped\n");
		return;
	}
	iowrite32(reg->value, chan->codec->regs + reg->id);

}

irqreturn_t avpu_hardirq_handler(int irq, void *data)
{
	struct avpu_codec_desc *codec = (struct avpu_codec_desc *)data;
	u32 unmasked_irq_bitfield, irq_bitfield, pending;
	u32 mask;
	unsigned int next;
	unsigned long flags;

	mask = ioread32(codec->regs + AVPU_INTERRUPT_MASK);
	unmasked_irq_bitfield = ioread32(codec->regs + AVPU_INTERRUPT);
	irq_bitfield = unmasked_irq_bitfield & mask;
	if (irq_bitfield == 0) {
		return IRQ_NONE;
	}
	avpu_trace_log("IRQ mask=0x%08x pending=0x%08x active=0x%08x\n",
		       mask, unmasked_irq_bitfield, irq_bitfield);
	iowrite32(unmasked_irq_bitfield, codec->regs + AVPU_INTERRUPT);
	ioread32(codec->regs + AVPU_INTERRUPT);

	pending = irq_bitfield & ((1U << AVPU_IRQ_SOURCES) - 1);

	spin_lock_irqsave(&codec->i_lock, flags);
	while (pending) {
		u32 i = __ffs(pending);

		pending &= pending - 1;
		next = (codec->irq_head + 1) & (AVPU_IRQ_QUEUE_LEN - 1);
		if (next == codec->irq_tail) {
			/* Reported from process context by the next waiter. */
			codec->irq_dropped++;
			continue;
		}
		codec->irq_queue[codec->irq_head] = i;
		codec->irq_head = next;
	}
	if (codec->chan)
		wake_up_interruptible(&codec->chan->irq_queue);
	spin_unlock_irqrestore(&codec->i_lock, flags);

	/* The interrupt was acknowledged above, so it is ours even if the
	 * queue overflowed. */
	return IRQ_HANDLED;
}
