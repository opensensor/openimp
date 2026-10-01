#include "avpu_alloc_ioctl.h"
#include "avpu_alloc.h"

#include <linux/uaccess.h>
#include "avpu_dmabuf.h"

/* Upper bound for one allocation; keeps PAGE_ALIGN() and the mmap offset
 * arithmetic clear of u32 overflow. */
#define AVPU_DMA_MAX_SIZE	0x40000000u

static int avpu_dma_size_valid(struct device *dev, u32 size)
{
	if (size == 0 || size > AVPU_DMA_MAX_SIZE) {
		dev_err(dev, "Invalid DMA buffer size %u\n", size);
		return 0;
	}
	return 1;
}

int avpu_ioctl_get_dma_fd(struct device *dev, unsigned long arg)
{
	struct avpu_dma_info info;

	if (copy_from_user(&info, (struct avpu_dma_info __user *)arg, sizeof(info)))
		return -EFAULT;

	if (!avpu_dma_size_valid(dev, info.size))
		return -EINVAL;

	return avpu_allocate_dmabuf_fd(dev, &info,
				       (struct avpu_dma_info __user *)arg);
}

static int add_buffer_to_list(struct avpu_codec_chan *chan, struct avpu_dma_buffer *buf)
{
	struct avpu_dma_buf_mmap *buf_mmap = kmalloc(sizeof(*buf_mmap), GFP_KERNEL);

	if (!buf_mmap)
		return -ENOMEM;
	buf_mmap->buf = buf;
	spin_lock(&chan->lock);
	/* The id travels to userspace as id << PAGE_SHIFT in a u32. */
	if (chan->num_bufs > (int)(~0U >> PAGE_SHIFT)) {
		spin_unlock(&chan->lock);
		kfree(buf_mmap);
		return -ENOSPC;
	}
	list_add_tail(&buf_mmap->list, &chan->mem);
	buf_mmap->buf_id = chan->num_bufs++;
	spin_unlock(&chan->lock);
	return buf_mmap->buf_id;
}

int avpu_ioctl_get_dma_mmap(struct device *dev, struct avpu_codec_chan *chan,
			   unsigned long arg)
{
	struct avpu_dma_info info;
	struct avpu_dma_buffer *buf = NULL;
	int id;

	if (copy_from_user(&info, (struct avpu_dma_info __user *)arg, sizeof(info)))
		return -EFAULT;

	if (!avpu_dma_size_valid(dev, info.size))
		return -EINVAL;

	buf = avpu_alloc_dma(dev, info.size);

	if (!buf) {
		dev_err(dev, "Can't alloc DMA buffer\n");
		return -ENOMEM;
	}

	id = add_buffer_to_list(chan, buf);
	if (id < 0) {
		avpu_free_dma(dev, buf);
		return id;
	}
	/* offset for mmap needs to be a multiple of page size */
	info.fd = (u32)id << PAGE_SHIFT;

	info.phy_addr = (__u32)buf->dma_handle;

	dev_dbg(dev, "allocated buffer cpu: %p, phy:0x%08x, offset:0x%x\n",
		buf->cpu_handle, info.phy_addr, info.fd);

	/* On failure the buffer stays on chan->mem and is freed on release. */
	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

int avpu_ioctl_get_dmabuf_dma_addr(struct device *dev, unsigned long arg)
{
	struct avpu_dma_info info;
	int err;

	if (copy_from_user(&info, (struct avpu_dma_info __user *)arg, sizeof(info)))
		return -EFAULT;

	err = avpu_dmabuf_get_address(dev, info.fd, &info.phy_addr);
	if (err)
		return err;

	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}
