#include <linux/device.h>
#include "avpu_alloc.h"
#include "avpu_ioctl.h"

struct avpu_buffer_info {
	u32 bus_address;
	u32 size;
};

/* Allocates a coherent buffer, exports it as a dma-buf and returns fd,
 * size and bus address through uinfo. The fd is only installed once the
 * reply reached userspace, so no failure leaves an fd behind. */
int avpu_allocate_dmabuf_fd(struct device *dev, struct avpu_dma_info *info,
			    struct avpu_dma_info __user *uinfo);
int avpu_dmabuf_get_address(struct device *dev, u32 fd, u32 *bus_address);
