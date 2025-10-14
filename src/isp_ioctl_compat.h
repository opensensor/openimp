#ifndef ISP_IOCTL_COMPAT_H
#define ISP_IOCTL_COMPAT_H

#include <stdint.h>

/*
 * TX-ISP ioctl compatibility between T31 and T23 platforms.
 * T23 encodes a 12-byte struct for GET_BUF/SET_BUF (index, phys, size).
 * T31 encodes an 8-byte struct (phys/addr, size).
 */

#ifdef PLATFORM_T23
    /* T23 ioctls (vendor path):
     * - REGISTER_SENSOR: 0x805456c1 (correct T23 ioctl, different from T31)
     * - GET_BUF:        0x800C56D5 (12-byte: index/phys/size)
     * - SET_BUF:        0x800C56D4 (12-byte: index/phys/size)
     */
    #define TX_ISP_GET_BUF            0x800c56d5u
    #define TX_ISP_SET_BUF            0x800c56d4u
    #define TX_ISP_REGISTER_SENSOR    0x805456c1u

    typedef struct {
        uint32_t index; /* active sensor index (or driver selector) */
        uint32_t phys;  /* physical address for SET_BUF */
        uint32_t size;  /* buffer size returned by GET_BUF */
    } tx_isp_buf_t;

    #define TXISP_BUF_INIT(buf)          do { (buf).index = 0; (buf).phys = 0; (buf).size = 0; } while (0)
    #define TXISP_BUF_SET_INDEX(buf, i)  do { (buf).index = (uint32_t)(i); } while (0)
    #define TXISP_BUF_GET_SIZE(buf)      ((buf).size)
    #define TXISP_BUF_SET_PHYS_SIZE(buf, phys_, size_) \
        do { (buf).phys = (uint32_t)(phys_); (buf).size = (uint32_t)(size_); } while (0)
#else
    /* Default to T31-compatible ioctls */
    #define TX_ISP_GET_BUF            0x800856d5u
    #define TX_ISP_SET_BUF            0x800856d4u
    #define TX_ISP_REGISTER_SENSOR    0x805056c1u  /* size 0x50 (IMPSensorInfo without private_data) */

    typedef struct {
        uint32_t addr; /* physical address (input for SET_BUF, typically 0 for GET_BUF) */
        uint32_t size; /* buffer size returned by GET_BUF */
    } tx_isp_buf_t;

    #define TXISP_BUF_INIT(buf)          do { (buf).addr = 0; (buf).size = 0; } while (0)
    #define TXISP_BUF_SET_INDEX(buf, i)  do { (void)(buf); (void)(i); } while (0)
    #define TXISP_BUF_GET_SIZE(buf)      ((buf).size)
    #define TXISP_BUF_SET_PHYS_SIZE(buf, phys_, size_) \
        do { (buf).addr = (uint32_t)(phys_); (buf).size = (uint32_t)(size_); } while (0)
#endif

#endif /* ISP_IOCTL_COMPAT_H */

