/* Pixel conversions into the shared-memory layouts xrdp's encoders expect,
 * using the same integer formulas as xorgxrdp (rdpCapture.c) so colours
 * match an Xorg session. Sources are XRGB8888 (bytes B, G, R, X). */
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "color.h"

static inline uint8_t clamp8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

/* NV12, BT.709 full range (xrdp's XRDP_nv12_709fr), for H.264.
 * Converts the 2x2-aligned rect r of src into dst (Y plane w*h, then
 * interleaved UV at w*h, stride w). Pixels outside the source are black. */
void nv12_convert_rect(uint8_t *dst, int w, int h, const uint8_t *src, int src_stride, int src_w,
                       int src_h, struct rect r)
{
    uint8_t *y_plane = dst;
    uint8_t *uv_plane = dst + (size_t)w * h;

    for (int y = r.y; y < r.y + r.h; y += 2) {
        uint8_t *y0 = y_plane + (size_t)y * w;
        uint8_t *y1 = y0 + w;
        uint8_t *uv = uv_plane + (size_t)(y / 2) * w;
        const uint32_t *s0 = (const uint32_t *)(src + (size_t)y * src_stride);
        const uint32_t *s1 = (const uint32_t *)(src + (size_t)(y + 1) * src_stride);
        bool row0 = y < src_h, row1 = y + 1 < src_h;

        for (int x = r.x; x < r.x + r.w; x += 2) {
            uint32_t p[4] = {0, 0, 0, 0};
            if (row0 && x < src_w)
                p[0] = s0[x];
            if (row0 && x + 1 < src_w)
                p[1] = s0[x + 1];
            if (row1 && x < src_w)
                p[2] = s1[x];
            if (row1 && x + 1 < src_w)
                p[3] = s1[x + 1];

            int u_sum = 0, v_sum = 0;
            for (int i = 0; i < 4; i++) {
                int R = (p[i] >> 16) & 0xff, G = (p[i] >> 8) & 0xff, B = p[i] & 0xff;
                uint8_t Y = clamp8((54 * R + 183 * G + 18 * B) >> 8);
                u_sum += clamp8(((-29 * R - 99 * G + 128 * B) >> 8) + 128);
                v_sum += clamp8(((128 * R - 116 * G - 12 * B) >> 8) + 128);
                if (i == 0)
                    y0[x] = Y;
                else if (i == 1)
                    y0[x + 1] = Y;
                else if (i == 2)
                    y1[x] = Y;
                else
                    y1[x + 1] = Y;
            }
            uv[x] = (uint8_t)((u_sum + 2) / 4);
            uv[x + 1] = (uint8_t)((v_sum + 2) / 4);
        }
    }
}

/* RemoteFX tiles: 64x64 tiles in row-major tile order, each four 4096-byte
 * planes Y, U, V, A. Converts the whole tile at (tx, ty) (tile units).
 * tiles_per_row = align64(w) / 64. */
void rfx_convert_tile(uint8_t *dst, int tiles_per_row, const uint8_t *src, int src_stride, int src_w,
                      int src_h, int tx, int ty)
{
    uint8_t *tile = dst + ((size_t)ty * tiles_per_row + tx) * 16384;
    uint8_t *py = tile, *pu = tile + 4096, *pv = tile + 8192, *pa = tile + 12288;
    int x0 = tx * 64, y0 = ty * 64;

    for (int j = 0; j < 64; j++) {
        int sy = y0 + j;
        const uint32_t *row = (const uint32_t *)(src + (size_t)sy * src_stride);
        for (int i = 0; i < 64; i++) {
            int sx = x0 + i;
            uint32_t p = (sy < src_h && sx < src_w) ? row[sx] : 0;
            int R = (p >> 16) & 0xff, G = (p >> 8) & 0xff, B = p & 0xff;
            int o = j * 64 + i;
            py[o] = clamp8((19595 * R + 38470 * G + 7471 * B) >> 16);
            pu[o] = clamp8(((-11071 * R - 21736 * G + 32807 * B) >> 16) + 128);
            pv[o] = clamp8(((32756 * R - 27429 * G - 5327 * B) >> 16) + 128);
            pa[o] = 0xff;
        }
    }
}

/* XRGB (B,G,R,X bytes) to XBGR (R,G,B,X bytes) for xrdp's JPEG path */
void swizzle_rect(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, struct rect r)
{
    for (int y = r.y; y < r.y + r.h; y++) {
        const uint32_t *s = (const uint32_t *)(src + (size_t)y * src_stride);
        uint32_t *d = (uint32_t *)(dst + (size_t)y * dst_stride);
        for (int x = r.x; x < r.x + r.w; x++) {
            uint32_t p = s[x];
            d[x] = (p & 0xff00ff00u) | ((p >> 16) & 0xff) | ((p & 0xff) << 16);
        }
    }
}

/* Big updates (a full 4K frame takes ~13 ms on one core) are cut into
 * even-height bands, converted by a few threads; small ones aren't worth it */
#define NV12_PARALLEL_MIN (256 * 1024) /* pixels */
#define NV12_MAX_THREADS 8
#define NV12_MAX_JOBS 256

struct nv12_job {
    uint8_t *dst;
    int w, h;
    const uint8_t *src;
    int src_stride, src_w, src_h;
    struct rect *bands;
    int n_bands, first, step;
};

static void *nv12_worker(void *arg)
{
    struct nv12_job *j = arg;
    for (int i = j->first; i < j->n_bands; i += j->step)
        nv12_convert_rect(j->dst, j->w, j->h, j->src, j->src_stride, j->src_w, j->src_h,
                          j->bands[i]);
    return NULL;
}

void nv12_convert_rects(uint8_t *dst, int w, int h, const uint8_t *src, int src_stride, int src_w,
                        int src_h, const struct rect *rects, int n)
{
    long area = 0;
    for (int i = 0; i < n; i++)
        area += (long)rects[i].w * rects[i].h;
    static int threads;
    if (!threads) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        threads = cpus >= 4 ? (int)(cpus / 2) : 1;
        if (threads > NV12_MAX_THREADS)
            threads = NV12_MAX_THREADS;
    }
    if (area < NV12_PARALLEL_MIN || threads < 2) {
        for (int i = 0; i < n; i++)
            nv12_convert_rect(dst, w, h, src, src_stride, src_w, src_h, rects[i]);
        return;
    }

    /* Bands of about area / (4 * threads) pixels, rows kept even */
    struct rect bands[NV12_MAX_JOBS];
    int n_bands = 0;
    long target = area / (4 * threads) + 1;
    for (int i = 0; i < n; i++) {
        struct rect r = rects[i];
        int rows = r.w > 0 ? (int)(target / r.w) : r.h;
        rows = rows < 2 ? 2 : rows & ~1;
        for (int y = 0; y < r.h; y += rows) {
            if (n_bands == NV12_MAX_JOBS) { /* no room left: the rest whole */
                bands[n_bands - 1].h = r.y + r.h - bands[n_bands - 1].y;
                break;
            }
            bands[n_bands++] = (struct rect){r.x, r.y + y, r.w, y + rows > r.h ? r.h - y : rows};
        }
    }

    pthread_t tids[NV12_MAX_THREADS];
    struct nv12_job jobs[NV12_MAX_THREADS];
    int started = 0;
    for (int t = 0; t < threads; t++) {
        jobs[t] = (struct nv12_job){dst, w, h, src, src_stride, src_w, src_h, bands, n_bands, t, threads};
        if (t > 0 && pthread_create(&tids[t], NULL, nv12_worker, &jobs[t]) == 0)
            started |= 1 << t;
    }
    nv12_worker(&jobs[0]);
    for (int t = 1; t < threads; t++) {
        if (started & (1 << t))
            pthread_join(tids[t], NULL);
        else
            nv12_worker(&jobs[t]); /* thread didn't start: do its share here */
    }
}
