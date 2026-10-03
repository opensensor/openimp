#ifndef OPENIMP_T40_EP1_H
#define OPENIMP_T40_EP1_H

#include <stddef.h>

int openimp_t40_init_ep1(void *ep1, size_t size, int use_fixqp_lda);
int openimp_t41_init_ep1(void *ep1, size_t size);
/* T31 HEVC: HEVC default lambda table, no scaling list. */
int openimp_t31_init_hevc_ep1(void *ep1, size_t size);

/* Rewrite only T41's 52 AVC lambda words for the next picture type.
 * AL_GetLambda indexes the four-component default table by slice type:
 * B=0, P=1 and I=2.  Component 3 remains the second hardware lane. */
int openimp_t41_update_ep1_lambda(void *ep1, size_t size,
                                  unsigned int picture_type);
/* The same T41 layout with the AVC (hevc = 0) or HEVC default table;
 * T41 AL_GetLambda selects HEVC_NEW_DEFAULT_LDA_TABLE for codec 1. */
int openimp_t41_update_ep1_lambda_codec(void *ep1, size_t size,
                                        unsigned int picture_type,
                                        unsigned int hevc);
/* T41 HEVC: HEVC lambda words for an I picture, no scaling list. */
int openimp_t41_init_hevc_ep1(void *ep1, size_t size);

#endif
