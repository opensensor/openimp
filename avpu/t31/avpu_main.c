#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fcntl.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/resource.h>
#include <linux/version.h>

#include "avpu_ioctl.h"
#include "avpu_alloc_ioctl.h"
#include "avpu_ip.h"

#define DEV_NAME "avpu"

#define AVPU_DRIVER_VERSION "H20220825a"

int avpu_codec_major;
int avpu_codec_nr_devs = AVPU_NR_DEVS;
module_param(avpu_codec_nr_devs, int, S_IRUGO);
static char *clk_name = "mpll";
module_param(clk_name, charp, S_IRUGO);
MODULE_PARM_DESC(clk_name, "chose parent clk");
static int avpu_clk = 550000000;
module_param(avpu_clk, int, S_IRUGO);
MODULE_PARM_DESC(avpu_clk, "avpu clock freq");
bool avpu_trace;
module_param(avpu_trace, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(avpu_trace, "log every register access, ioctl and irq");
static struct class *module_class;

struct flush_cache_info {
	unsigned int	addr;
	unsigned int	len;
#define WBACK		DMA_TO_DEVICE
#define INV		DMA_FROM_DEVICE
#define WBACK_INV	DMA_BIDIRECTIONAL
	unsigned int	dir;
};

static void jz_avpu_release(struct device *dev)
{
    return;
}

#if defined(CONFIG_SOC_T31) || defined(CONFIG_SOC_T40)
#define AVPU_IOBASE    0x13200000
#elif defined(CONFIG_SOC_T41)
#define AVPU_IOBASE    0x13100000
#endif

#define AVPU_IOBASE_UNIT(ID)	(AVPU_IOBASE + 0x400000 * ID)
static u64 avpu_dmamask = ~(u64)0;
static struct resource jz_avpu_irq_resources[] = {			\
	[0] = {								\
		.start          = AVPU_IOBASE_UNIT(0),			\
		.end            = AVPU_IOBASE_UNIT(0) + 0x100000 - 1,	\
		.flags          = IORESOURCE_MEM,			\
	},								\
	[1] = {								\
		.start          = 32+30+8,		\
		.end            = 32+30+8,	\
		.flags          = IORESOURCE_IRQ,			\
	},
};

struct platform_device jz_avpu_irq_device = {					\
	.name = "avpu",							\
	.id = 0,								\
	.dev = {								\
		.dma_mask				= &avpu_dmamask,			\
		.coherent_dma_mask      = 0xffffffff,				\
		.release                = jz_avpu_release,				\
	},									\
	.num_resources  = ARRAY_SIZE(jz_avpu_irq_resources),			\
	.resource       = jz_avpu_irq_resources,				\
};

int channel_is_ready(struct avpu_codec_chan *chan)
{
	struct avpu_codec_desc *codec = chan->codec;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&codec->i_lock, flags);
	ret = chan->unblock || codec->irq_head != codec->irq_tail;
	spin_unlock_irqrestore(&codec->i_lock, flags);
	return ret;
}

static int avpu_codec_open(struct inode *inode, struct file *filp)
{
	struct avpu_codec_chan *chan;
	int ret;
	/* initialize channel */
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	chan = kzalloc(sizeof(struct avpu_codec_chan), GFP_KERNEL);
	if (!chan) {
		ret = -ENOMEM;
		goto fail;
	}

	INIT_LIST_HEAD(&chan->mem);
	spin_lock_init(&chan->lock);
	chan->num_bufs = 0;

	filp->private_data = chan;

	/* irq */
	init_waitqueue_head(&chan->irq_queue);

	ret = avpu_codec_bind_channel(chan, inode);
	if (ret)
		goto fail_codec_binding;
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	return 0;

fail_codec_binding:
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	kzfree(chan);
fail:
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	return ret;
}

static int avpu_codec_release(struct inode *inode, struct file *filp)
{
	struct avpu_codec_chan *chan = filp->private_data;
	struct device *dev = chan->codec->device;
	struct avpu_dma_buf_mmap *tmp;
	struct list_head *pos, *n;
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	avpu_codec_unbind_channel(chan);
	/* release() only runs once every mmap() of this file is gone, so the
	 * coherent buffers handed out by GET_DMA_MMAP are unreferenced now.
	 * A process that died mid-picture may still have the core writing
	 * stream and status data into them; one picture takes well below
	 * 100 ms, so let it drain before the memory can be reused. */
	if (!list_empty(&chan->mem))
		msleep(100);
	list_for_each_safe(pos, n, &chan->mem){
		tmp = list_entry(pos, struct avpu_dma_buf_mmap, list);
		list_del(pos);
		avpu_free_dma(dev, tmp->buf);
		kfree(tmp);
	}

	kfree(chan);
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	return 0;
}

static long avpu_codec_compat_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}

static struct avpu_dma_buffer *find_buf_by_id(struct avpu_codec_chan *chan, int desc_id)
{
	struct avpu_dma_buf_mmap *cur_buf_mmap;
	struct avpu_dma_buffer *buf = NULL;

	spin_lock(&chan->lock);
	list_for_each_entry(cur_buf_mmap, &chan->mem, list){
		if (cur_buf_mmap->buf_id == desc_id) {
			buf = cur_buf_mmap->buf;
			break;
		}
	}
	spin_unlock(&chan->lock);

	return buf;
}

static int avpu_dma_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct avpu_codec_chan *chan = filp->private_data;
	unsigned long start = vma->vm_start;
	unsigned long vsize = vma->vm_end - start;
	/* offset if already in page */
	int desc_id = vma->vm_pgoff;
	int ret = 0;
	struct avpu_dma_buffer *buf = find_buf_by_id(chan, desc_id);

	if (!buf)
		return -EINVAL;

	if (vsize > PAGE_ALIGN(buf->size))
		return -EINVAL;

	vma->vm_pgoff = 0;

	ret = dma_mmap_coherent(chan->codec->device, vma, buf->cpu_handle,
				buf->dma_handle, vsize);
	if (ret < 0) {
		pr_err("Remapping memory failed, error: %d\n", ret);
		return ret;
	}

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;

	return 0;
}

static int unblock_channel(struct avpu_codec_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->codec->i_lock, flags);
	chan->unblock = 1;
	spin_unlock_irqrestore(&chan->codec->i_lock, flags);
//	printk("--------------%s(%d)-----------\n", __func__, __LINE__);
	wake_up_interruptible(&chan->irq_queue);
	return 0;
}

static int wait_irq(struct avpu_codec_chan *chan, unsigned long arg)
{
	struct avpu_codec_desc *codec = chan->codec;
	u32 callback;
	int ret;

	for (;;) {
		ret = wait_event_interruptible(chan->irq_queue,
					       channel_is_ready(chan));
		if (ret == -ERESTARTSYS)
			return ret;
		if (chan->unblock) {
			avpu_dbg("Unblocking channel\n");
			return -EINTR;
		}
		/* Another waiter on this file may have taken the irq. */
		if (avpu_irq_queue_pop(codec, &callback))
			break;
	}

	if (copy_to_user((void __user *)arg, &callback, sizeof(__u32)))
		return -EFAULT;

	return 0;
}

static int check_reg_id(struct avpu_codec_desc *codec, unsigned int id)
{
	if (id % 4) {
		avpu_err("Unaligned register access: 0x%.4X\n", id);
		return -EINVAL;
	}

	if (id < AVPU_BASE_OFFSET || id > codec->regs_size - 3) {
		avpu_err("Out-of-range register access: 0x%.4X\n", id);
		return -EINVAL;
	}

	return 0;
}

static int read_reg(struct avpu_codec_chan *chan, unsigned long arg)
{
	struct avpu_reg reg;
	struct avpu_codec_desc *codec = chan->codec;
	int err;

	if (copy_from_user(&reg, (struct avpu_reg __user *)arg, sizeof(struct avpu_reg)))
		return -EFAULT;

	err = check_reg_id(codec, reg.id);
	if (err)
		return err;

	err = avpu_codec_read_register(chan, &reg);
	if (err)
		return err;

	if (copy_to_user((struct avpu_reg __user *)arg, &reg, sizeof(struct avpu_reg)))
		return -EFAULT;

	avpu_trace_log("RD 0x%04x = 0x%08x\n", reg.id, reg.value);

	return 0;
}

static int write_reg(struct avpu_codec_chan *chan, unsigned long arg)
{
	struct avpu_reg reg;
	struct avpu_codec_desc *codec = chan->codec;
	int err;

	if (copy_from_user(&reg, (struct avpu_reg __user *)arg, sizeof(struct avpu_reg)))
		return -EFAULT;
	avpu_trace_log("WR 0x%04x = 0x%08x\n", reg.id, reg.value);

	err = check_reg_id(codec, reg.id);
	if (err)
		return err;

	avpu_codec_write_register(chan, &reg);

	return 0;
}

static long jz_cmd_flush_cache(unsigned long arg)
{
	struct flush_cache_info info;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr, end;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info))) {
		return -EFAULT;
	}

	if (info.dir != WBACK && info.dir != INV && info.dir != WBACK_INV)
		return -EINVAL;
	if (!info.len)
		return 0;

	addr = info.addr;
	end = addr + info.len;
	if (end < addr || end > TASK_SIZE || !mm)
		return -EFAULT;

	/* dma_cache_sync() runs cache instructions on the user address. A hole
	 * or an inaccessible mapping in the range faults in kernel mode without
	 * a fixup and oopses, so only the accessible VMAs that run contiguously
	 * from the start address are flushed. A caller that rounds a short
	 * range up past the end of its buffer keeps the part that matters
	 * instead of having the whole flush refused. Only a start
	 * address outside any accessible mapping is an error. The lock is
	 * dropped before the cache operation because faulting in a page there
	 * takes mmap_sem again. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	mmap_read_lock(mm);
#else
	down_read(&mm->mmap_sem);
#endif
	while (addr < end) {
		vma = find_vma(mm, addr);
		if (!vma || vma->vm_start > addr ||
		    !(vma->vm_flags & (VM_READ | VM_WRITE)))
			break;
		addr = vma->vm_end;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif
	if (addr == info.addr)
		return -EFAULT;
	if (addr < end)
		info.len = addr - info.addr;

	dma_cache_sync(NULL, (void *)(unsigned long)info.addr, info.len,
		       info.dir);

	return 0;
}

static long avpu_codec_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	struct avpu_codec_chan *chan = filp->private_data;
	struct avpu_codec_desc *codec = chan->codec;

	switch (cmd) {
	case GET_DMA_MMAP:
		avpu_trace_log("ioctl GET_DMA_MMAP\n");
		return avpu_ioctl_get_dma_mmap(codec->device, chan, arg);
	case GET_DMA_FD:
		avpu_trace_log("ioctl GET_DMA_FD\n");
		return avpu_ioctl_get_dma_fd(codec->device, arg);
	case GET_DMA_PHY:
		avpu_trace_log("ioctl GET_DMA_PHY\n");
		return avpu_ioctl_get_dmabuf_dma_addr(codec->device, arg);
	case AL_CMD_UNBLOCK_CHANNEL:
		avpu_trace_log("ioctl UNBLOCK_CHANNEL\n");
		return unblock_channel(chan);
	case AL_CMD_IP_WAIT_IRQ:
		return wait_irq(chan, arg);
	case AL_CMD_IP_READ_REG:
		return read_reg(chan, arg);
	case AL_CMD_IP_WRITE_REG:
		return write_reg(chan, arg);
	case JZ_CMD_FLUSH_CACHE:
		return jz_cmd_flush_cache(arg);
	default:
		avpu_err("Unknown ioctl: 0x%.8X\n", cmd);
		return -EINVAL;
	}
}

const struct file_operations avpu_codec_fops = {
	.owner		= THIS_MODULE,
	.open		= avpu_codec_open,
	.release	= avpu_codec_release,
	.unlocked_ioctl = avpu_codec_ioctl,
	.compat_ioctl	= avpu_codec_compat_ioctl,
	.mmap		= avpu_dma_mmap,
};

void clean_up_avpu_codec_cdev(struct avpu_codec_desc *dev)
{
	cdev_del(&dev->cdev);
}

int setup_chrdev_region(void)
{
	dev_t dev = 0;
	int err;

	if (avpu_codec_major == 0) {
		err = alloc_chrdev_region(&dev, 0, avpu_codec_nr_devs, "avpu");
		avpu_codec_major = MAJOR(dev);

		if (err) {
			pr_alert("Allegro codec: can't get major %d\n",
				 avpu_codec_major);
			return err;
		}
	}
	return 0;
}

int avpu_setup_codec_cdev(struct avpu_codec_desc *codec, int minor,
			  const char *device_name)
{
	struct device *device;
	int err, devno =
		MKDEV(avpu_codec_major, minor);

	cdev_init(&codec->cdev, &avpu_codec_fops);
	codec->cdev.owner = THIS_MODULE;
	err = cdev_add(&codec->cdev, devno, 1);
	if (err) {
		avpu_err("Error %d adding avpu device number %d", err, minor);
		return err;
	}

	if (device_name != NULL) {
		device = device_create(module_class, NULL, devno, NULL,
				       device_name);
		if (IS_ERR(device)) {
			pr_err("device not created\n");
			clean_up_avpu_codec_cdev(codec);
			return PTR_ERR(device);
		}
	}

	return 0;
}

static void init_codec_desc(struct avpu_codec_desc *codec)
{
	spin_lock_init(&codec->i_lock);
	/* make chan requirement explicit */
	codec->chan = NULL;
	avpu_irq_queue_reset(codec);
}

/*
 * Per-SoC clock tree. AVPU_CLK_MUX is the clock that receives the clk_name
 * parent; without it the parent is set on the core clock itself.
 */
#if defined(CONFIG_SOC_T41) && defined(CONFIG_KERNEL_4_4_94)
#define AVPU_CLK_AHB1		"div_ispa"
#define AVPU_CLK_IVDC		"gate_ivdc"
#define AVPU_CLK_GATE		"gate_el200"
#define AVPU_CLK_MUX		"mux_el200"
#define AVPU_CLK_CORE		"div_el200"
#define AVPU_CLK_PREPARE	1
#elif defined(CONFIG_SOC_T41) && defined(CONFIG_KERNEL_3_10)
#define AVPU_CLK_AHB1		"cgu_ispa"
#define AVPU_CLK_IVDC		"ivdc"
#define AVPU_CLK_GATE		"avpu"
#define AVPU_CLK_CORE		"cgu_vpu"
#define AVPU_CLK_PREPARE	0
#elif defined(CONFIG_SOC_T40)
#define AVPU_CLK_AHB1		"gate_ahb1"
#define AVPU_CLK_GATE		"gate_el150"
#define AVPU_CLK_MUX		"mux_el150"
#define AVPU_CLK_CORE		"div_el150"
#define AVPU_CLK_PREPARE	1
#elif defined(CONFIG_SOC_T31)
#define AVPU_CLK_AHB1		"ahb1"
#define AVPU_CLK_GATE		"avpu"
#define AVPU_CLK_CORE		"cgu_vpu"
#define AVPU_CLK_PREPARE	0
#endif

#ifdef AVPU_CLK_CORE
static int avpu_clk_on(struct clk *clk)
{
#if AVPU_CLK_PREPARE
	return clk_prepare_enable(clk);
#else
	return clk_enable(clk);
#endif
}

static void avpu_clk_off(struct clk *clk)
{
#if AVPU_CLK_PREPARE
	clk_disable_unprepare(clk);
#else
	clk_disable(clk);
#endif
}

/* Clocks in enable order; returns the number of entries. */
static int avpu_clock_list(struct avpu_codec_desc *codec, struct clk **list)
{
	int n = 0;

#ifdef AVPU_CLK_IVDC
	list[n++] = codec->clk_gate_ivdc;
#endif
	list[n++] = codec->ahb1_gate;
	list[n++] = codec->clk_gate;
	list[n++] = codec->clk;
	return n;
}

static void avpu_put_clock(struct clk **clk)
{
	if (!IS_ERR_OR_NULL(*clk))
		clk_put(*clk);
	*clk = NULL;
}

static void avpu_put_clocks(struct avpu_codec_desc *codec)
{
	avpu_put_clock(&codec->clk);
	avpu_put_clock(&codec->clk_mux);
	avpu_put_clock(&codec->clk_gate);
#ifdef CONFIG_SOC_T41
	avpu_put_clock(&codec->clk_gate_ivdc);
#endif
	avpu_put_clock(&codec->ahb1_gate);
}

static int avpu_get_clock(struct avpu_codec_desc *codec, struct clk **clk,
			  const char *name)
{
	*clk = clk_get(codec->device, name);
	if (IS_ERR(*clk)) {
		int err = PTR_ERR(*clk);

		avpu_err("clk %s get failed: %d\n", name, err);
		*clk = NULL;
		return err;
	}
	return 0;
}

static void avpu_disable_clocks(struct avpu_codec_desc *codec)
{
	struct clk *list[4];
	int n = avpu_clock_list(codec, list);

	while (n--)
		avpu_clk_off(list[n]);
}

static int avpu_init_clocks(struct avpu_codec_desc *codec)
{
	struct clk *list[4], *parent, *parent_target;
	int n, i, err;

	err = avpu_get_clock(codec, &codec->ahb1_gate, AVPU_CLK_AHB1);
#ifdef AVPU_CLK_IVDC
	if (!err)
		err = avpu_get_clock(codec, &codec->clk_gate_ivdc, AVPU_CLK_IVDC);
#endif
	if (!err)
		err = avpu_get_clock(codec, &codec->clk_gate, AVPU_CLK_GATE);
#ifdef AVPU_CLK_MUX
	if (!err)
		err = avpu_get_clock(codec, &codec->clk_mux, AVPU_CLK_MUX);
#endif
	if (!err)
		err = avpu_get_clock(codec, &codec->clk, AVPU_CLK_CORE);
	if (err)
		goto out_put;

	parent_target = codec->clk_mux ? codec->clk_mux : codec->clk;
	parent = clk_get(NULL, clk_name);
	if (IS_ERR(parent)) {
		avpu_err("parent clk %s not found, keeping default parent\n",
			 clk_name);
	} else {
		err = clk_set_parent(parent_target, parent);
		if (err)
			avpu_err("clk_set_parent failed!!! parent name = %s\n",
				 clk_name);
		clk_put(parent);
	}

	err = clk_set_rate(codec->clk, avpu_clk);
	if (err)
		avpu_err("clk_set_rate(%d) failed: %d\n", avpu_clk, err);

	n = avpu_clock_list(codec, list);
	for (i = 0; i < n; i++) {
		err = avpu_clk_on(list[i]);
		if (err) {
			avpu_err("clk enable failed: %d\n", err);
			while (i--)
				avpu_clk_off(list[i]);
			goto out_put;
		}
	}

	return 0;

out_put:
	avpu_put_clocks(codec);
	return err;
}

static void avpu_deinit_clocks(struct avpu_codec_desc *codec)
{
	avpu_disable_clocks(codec);
	avpu_put_clocks(codec);
}
#else
static int avpu_init_clocks(struct avpu_codec_desc *codec)
{
	return 0;
}

static void avpu_deinit_clocks(struct avpu_codec_desc *codec)
{
}
#endif

int avpu_codec_probe(struct platform_device *pdev)
{
	int err, irq;
	static int current_minor;
	struct resource *res;
	struct avpu_codec_desc *codec
		= devm_kzalloc(&pdev->dev, sizeof(*codec), GFP_KERNEL);

	if (!codec)
		return -ENOMEM;

	codec->device = &pdev->dev;
	codec->irq = -1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		avpu_err("Can't get resource\n");
		return -ENODEV;
	}

	/* devm_ioremap_nocache() reports failure as NULL, not ERR_PTR. */
	codec->regs = devm_ioremap_nocache(&pdev->dev,
					   res->start, resource_size(res));
	if (!codec->regs) {
		avpu_err("Can't map registers\n");
		return -ENOMEM;
	}
	codec->regs_size = res->end - res->start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		avpu_info("No irq requested / Couldn't obtain request irq\n");

	err = avpu_init_clocks(codec);
	if (err)
		return err;

	init_codec_desc(codec);

	if (irq >= 0) {
		err = devm_request_irq(codec->device,
				       irq,
				       avpu_hardirq_handler,
				       IRQF_SHARED, dev_name(&pdev->dev), codec);
		if (err) {
			avpu_err("Failed to request IRQ #%d -> :%d\n",
				irq, err);
			goto out_clocks;
		}
		codec->irq = irq;
	}

	platform_set_drvdata(pdev, codec);

	err = avpu_setup_codec_cdev(codec, current_minor, DEV_NAME);
	if (err)
		goto out_irq;

	codec->minor = current_minor;
	++current_minor;
	printk("@@@@ avpu driver ok(version %s) @@@@@\n", AVPU_DRIVER_VERSION);

	return 0;

out_irq:
	/* The handler reads registers: release it before gating the clocks
	 * instead of leaving that to devres after probe returns. */
	if (codec->irq >= 0)
		devm_free_irq(codec->device, codec->irq, codec);
out_clocks:
	avpu_deinit_clocks(codec);
	return err;
}

int avpu_codec_remove(struct platform_device *pdev)
{
	struct avpu_codec_desc *codec = platform_get_drvdata(pdev);
	dev_t dev = MKDEV(avpu_codec_major, codec->minor);

	device_destroy(module_class, dev);
	clean_up_avpu_codec_cdev(codec);

	if (codec->irq >= 0)
		devm_free_irq(codec->device, codec->irq, codec);
	avpu_deinit_clocks(codec);

	return 0;
}

static const struct of_device_id avpu_codec_of_match[] = {
	{ .compatible = "t31,avpu" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, avpu_codec_of_match);

static struct platform_driver avpu_platform_driver = {
	.probe			= avpu_codec_probe,
	.remove			= avpu_codec_remove,
	.driver			=       {
		.name		= "avpu",
		.of_match_table = of_match_ptr(avpu_codec_of_match),
		/* remove() frees the codec while an open file still points at
		 * it; its release() would then use freed memory. Without the
		 * sysfs bind/unbind files remove() only runs on rmmod, which the
		 * module reference held by every open file already prevents. */
		.suppress_bind_attrs = true,
	},
};

static int create_module_class(void)
{
	module_class = class_create(THIS_MODULE, "avpu_class");
	if (IS_ERR(module_class))
		return PTR_ERR(module_class);

	return 0;
}

static void destroy_module_class(void)
{
	class_destroy(module_class);
}

int avpu_module_init(void)
{
	int ret;

	ret = platform_device_register(&jz_avpu_irq_device);
	if(ret){
		printk("Failed to insmod t31 vpu driver!\n");
		return ret;
	}

	ret = platform_driver_register(&avpu_platform_driver);
	if (ret)
		platform_device_unregister(&jz_avpu_irq_device);

	return ret;
}

void avpu_module_deinit(void)
{
	platform_driver_unregister(&avpu_platform_driver);
	platform_device_unregister(&jz_avpu_irq_device);
}

static int __init avpu_codec_init(void)
{
	dev_t devno;
	int err = setup_chrdev_region();

	if (err)
		return err;

	err = create_module_class();
	if (err)
		goto fail;

	err = avpu_module_init();
	if (err)
		goto fail_class;

	return 0;

fail_class:
	destroy_module_class();
fail:
	devno = MKDEV(avpu_codec_major, 0);
	unregister_chrdev_region(devno, avpu_codec_nr_devs);

	return err;
}

static void __exit avpu_codec_exit(void)
{
	dev_t devno = MKDEV(avpu_codec_major, 0);

	avpu_module_deinit();
	destroy_module_class();
	unregister_chrdev_region(devno, avpu_codec_nr_devs);
}

module_init(avpu_codec_init);
module_exit(avpu_codec_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kevin Grandemange");
MODULE_AUTHOR("Sebastien Alaiwan");
MODULE_DESCRIPTION("T31 Vpu Driver");
