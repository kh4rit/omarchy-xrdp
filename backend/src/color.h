#pragma once

#include <stdint.h>

#include "backend.h"

void nv12_convert_rect(uint8_t *dst, int w, int h, const uint8_t *src, int src_stride, int src_w,
                       int src_h, struct rect r);
/* Several rects, in parallel when they are big */
void nv12_convert_rects(uint8_t *dst, int w, int h, const uint8_t *src, int src_stride, int src_w,
                        int src_h, const struct rect *rects, int n);
void rfx_convert_tile(uint8_t *dst, int tiles_per_row, const uint8_t *src, int src_stride, int src_w,
                      int src_h, int tx, int ty);
void swizzle_rect(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, struct rect r);
