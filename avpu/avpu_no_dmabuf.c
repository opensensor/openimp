#include "avpu_dmabuf.h"

int avpu_allocate_dmabuf_fd(struct device *dev, struct avpu_dma_info *info,
			    struct avpu_dma_info __user *uinfo)
{
	pr_err("dmabuf interface not supported");
	return -EINVAL;
}

int avpu_dmabuf_get_address(struct device *dev, u32 fd, u32 *bus_address)
{
	pr_err("dmabuf interface not supported");
	return -EINVAL;
}
