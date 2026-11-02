/* See fisheye.h: the ROI part of the vendor's i264e_icodec_enc. */
#include <string.h>

#include "fisheye.h"

void fisheye_roi(int enable, uint32_t counter, int i_slice, uint32_t fps_num,
                 uint32_t fps_den, int qp, uint8_t roi[8][7])
{
    if (!enable)
        return;
    memset(roi, 0, 8 * 7);
    if (!i_slice) {
        /* the vendor traps on a zero frame rate; there is no whole second then */
        if (fps_num == 0u || (uint32_t)(counter * fps_den) % fps_num != 0u)
            return;
    }
    roi[0][0] = 1;                                  /* enable */
    roi[0][1] = 0;                                  /* absolute QP */
    roi[0][2] = (uint8_t)(qp < FISHEYE_QP_MAX ? qp : FISHEYE_QP_MAX);
    roi[0][3] = 0;                                  /* x0 */
    roi[0][4] = 3;                                  /* x1 */
    roi[0][5] = 0;                                  /* y0 */
    roi[0][6] = 1;                                  /* y1 */
}

void fisheye_roi_hw(int t21, int enable, uint32_t counter, int i_slice,
                    uint32_t fps_num, uint32_t fps_den, int qp,
                    uint8_t roi[8][7])
{
    fisheye_roi(enable, counter, i_slice, fps_num, fps_den, qp, roi);
    if (t21 && enable)
        memset(roi, 0, 8 * 7);
}
