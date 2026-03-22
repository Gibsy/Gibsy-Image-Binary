#ifndef GIB_H
#define GIB_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GIB_MAGIC      0x32424947u  
#define GIB_FLAG_LOSSY (1 << 0)

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint8_t  flags;
    uint8_t  quality;
    uint16_t ch_h;
    uint16_t ch_w;
} GibHeader;
#pragma pack(pop)

uint8_t *gib_encode(const uint8_t *pixels, int w, int h, int quality, size_t *out_size);
uint8_t *gib_decode(const uint8_t *data, size_t data_size, int *w, int *h);

#ifdef GIB_IMPLEMENTATION

#include <zstd.h>
#include <stdlib.h>

/*  YCbCr  */

static inline void rgb_to_ycbcr(uint8_t r, uint8_t g, uint8_t b,
                                  uint8_t *Y, uint8_t *Cb, uint8_t *Cr)
{
    float rf = (float)r, gf = (float)g, bf = (float)b;
    float y  =  0.299f    * rf + 0.587f    * gf + 0.114f    * bf;
    float cb = -0.168736f * rf - 0.331264f * gf + 0.5f      * bf + 128.f;
    float cr =  0.5f      * rf - 0.418688f * gf - 0.081312f * bf + 128.f;
    *Y  = (uint8_t)(y  < 0 ? 0 : y  > 255 ? 255 : y );
    *Cb = (uint8_t)(cb < 0 ? 0 : cb > 255 ? 255 : cb);
    *Cr = (uint8_t)(cr < 0 ? 0 : cr > 255 ? 255 : cr);
}

static inline void ycbcr_to_rgb(uint8_t Y, uint8_t Cb, uint8_t Cr,
                                  uint8_t *r, uint8_t *g, uint8_t *b)
{
    float y  = (float)Y;
    float cb = (float)Cb - 128.f;
    float cr = (float)Cr - 128.f;
    float rf = y              + 1.402f    * cr;
    float gf = y - 0.344136f * cb - 0.714136f * cr;
    float bf = y + 1.772f    * cb;
    *r = (uint8_t)(rf < 0 ? 0 : rf > 255 ? 255 : rf);
    *g = (uint8_t)(gf < 0 ? 0 : gf > 255 ? 255 : gf);
    *b = (uint8_t)(bf < 0 ? 0 : bf > 255 ? 255 : bf);
}

static inline int abs_i(int x){ return x < 0 ? -x : x; }

static inline uint8_t paeth_pred(int a, int b, int c)
{
    int p  = a + b - c;
    int pa = abs_i(p - a);
    int pb = abs_i(p - b);
    int pc = abs_i(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc)             return (uint8_t)b;
    return (uint8_t)c;
}

static void filter_row(const uint8_t *src, const uint8_t *prev,
                        uint8_t *dst, int w)
{
    uint8_t *tmp  = (uint8_t *)malloc(w * 5);
    int      score[5] = {0,0,0,0,0};

    for (int x = 0; x < w; x++) {
        int v    = src[x];
        int left = x    ? src[x-1]              : 0;
        int up   = prev ? prev[x]               : 0;
        int ul   = (prev && x) ? prev[x-1]      : 0;

        int d0 = v;
        int d1 = v - left;
        int d2 = v - up;
        int d3 = v - ((left + up) >> 1);
        int d4 = v - (int)paeth_pred(left, up, ul);

        tmp[0*w+x] = (uint8_t)d0; score[0] += abs_i(d0);
        tmp[1*w+x] = (uint8_t)d1; score[1] += abs_i((int8_t)d1);
        tmp[2*w+x] = (uint8_t)d2; score[2] += abs_i((int8_t)d2);
        tmp[3*w+x] = (uint8_t)d3; score[3] += abs_i((int8_t)d3);
        tmp[4*w+x] = (uint8_t)d4; score[4] += abs_i((int8_t)d4);
    }

    int best = 0;
    for (int f = 1; f < 5; f++)
        if (score[f] < score[best]) best = f;

    dst[0] = (uint8_t)best;
    memcpy(dst + 1, tmp + best * w, w);
    free(tmp);
}

static uint8_t *filter_plane(const uint8_t *plane, int w, int h, size_t *out_size)
{
    size_t   stride = (size_t)w + 1;
    uint8_t *out    = (uint8_t *)malloc(h * stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *src  = plane + y * w;
        const uint8_t *prev = y ? plane + (y-1) * w : NULL;
        filter_row(src, prev, out + y * stride, w);
    }
    *out_size = (size_t)h * stride;
    return out;
}

/* Reverse filter for one row */
static void unfilter_row(const uint8_t *src, uint8_t *prev, uint8_t *dst, int w)
{
    int type = src[0];
    src++;
    for (int x = 0; x < w; x++) {
        int v    = src[x];
        int left = x    ? dst[x-1]              : 0;
        int up   = prev ? prev[x]               : 0;
        int ul   = (prev && x) ? prev[x-1]      : 0;
        switch (type) {
            case 0: dst[x] = (uint8_t)v; break;
            case 1: dst[x] = (uint8_t)(v + left); break;
            case 2: dst[x] = (uint8_t)(v + up); break;
            case 3: dst[x] = (uint8_t)(v + ((left + up) >> 1)); break;
            case 4: dst[x] = (uint8_t)(v + (int)paeth_pred(left, up, ul)); break;
            default: dst[x] = (uint8_t)v; break;
        }
    }
}

/* Reconstruct plane from filtered data */
static uint8_t *unfilter_plane(const uint8_t *filtered, int w, int h)
{
    size_t   stride = (size_t)w + 1;
    uint8_t *out    = (uint8_t *)malloc((size_t)w * h);
    for (int y = 0; y < h; y++) {
        uint8_t *prev = y ? out + (y-1) * w : NULL;
        unfilter_row(filtered + y * stride, prev, out + y * w, w);
    }
    return out;
}

/* encode  */

static void write_u32le(uint8_t *p, uint32_t v)
{
    p[0]=v&0xff; p[1]=(v>>8)&0xff; p[2]=(v>>16)&0xff; p[3]=(v>>24)&0xff;
}
static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

uint8_t *gib_encode(const uint8_t *pixels, int w, int h, int quality, size_t *out_size)
{
    int lossy   = (quality > 0);
    int ch_w    = lossy ? (w + 1) / 2 : w;
    int ch_h    = lossy ? (h + 1) / 2 : h;
    int y_size  = w * h;
    int ch_size = ch_w * ch_h;

    uint8_t *Y_plane  = (uint8_t *)malloc(y_size);
    uint8_t *Cb_plane = (uint8_t *)malloc(ch_size);
    uint8_t *Cr_plane = (uint8_t *)malloc(ch_size);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const uint8_t *px = pixels + (row * w + col) * 3;
            uint8_t Y, Cb, Cr;
            rgb_to_ycbcr(px[0], px[1], px[2], &Y, &Cb, &Cr);
            Y_plane[row * w + col] = Y;
            if (lossy) {
                if (row % 2 == 0 && col % 2 == 0) {
                    int ci = (row/2) * ch_w + (col/2);
                    Cb_plane[ci] = Cb;
                    Cr_plane[ci] = Cr;
                }
            } else {
                Cb_plane[row * w + col] = Cb;
                Cr_plane[row * w + col] = Cr;
            }
        }
    }

    /* each channel separately */
    size_t   Yf_size, Cbf_size, Crf_size;
    uint8_t *Y_f  = filter_plane(Y_plane,  w,    h,    &Yf_size);
    uint8_t *Cb_f = filter_plane(Cb_plane, ch_w, ch_h, &Cbf_size);
    uint8_t *Cr_f = filter_plane(Cr_plane, ch_w, ch_h, &Crf_size);
    free(Y_plane); free(Cb_plane); free(Cr_plane);

    /* compress each channel independently for better ratio */
    int zstd_level = lossy ? (10 + quality) : 22;

    size_t   Yz_bound = ZSTD_compressBound(Yf_size);
    uint8_t *Y_z      = (uint8_t *)malloc(Yz_bound);
    size_t   Y_zsize  = ZSTD_compress(Y_z, Yz_bound, Y_f, Yf_size, zstd_level);

    size_t   Cbz_bound = ZSTD_compressBound(Cbf_size);
    uint8_t *Cb_z      = (uint8_t *)malloc(Cbz_bound);
    size_t   Cb_zsize  = ZSTD_compress(Cb_z, Cbz_bound, Cb_f, Cbf_size, zstd_level);

    size_t   Crz_bound = ZSTD_compressBound(Crf_size);
    uint8_t *Cr_z      = (uint8_t *)malloc(Crz_bound);
    size_t   Cr_zsize  = ZSTD_compress(Cr_z, Crz_bound, Cr_f, Crf_size, zstd_level);

    free(Y_f); free(Cb_f); free(Cr_f);

    size_t   payload_size = 3*4 + Y_zsize + Cb_zsize + Cr_zsize;
    uint8_t *payload      = (uint8_t *)malloc(payload_size);
    uint8_t *p            = payload;

    write_u32le(p, (uint32_t)Y_zsize);  p += 4;
    memcpy(p, Y_z,  Y_zsize);           p += Y_zsize;
    write_u32le(p, (uint32_t)Cb_zsize); p += 4;
    memcpy(p, Cb_z, Cb_zsize);          p += Cb_zsize;
    write_u32le(p, (uint32_t)Cr_zsize); p += 4;
    memcpy(p, Cr_z, Cr_zsize);          p += Cr_zsize;

    free(Y_z); free(Cb_z); free(Cr_z);

    /* final file */
    size_t   total  = sizeof(GibHeader) + payload_size;
    uint8_t *result = (uint8_t *)malloc(total);

    GibHeader hdr = {
        .magic   = GIB_MAGIC,
        .width   = (uint32_t)w,
        .height  = (uint32_t)h,
        .flags   = lossy ? GIB_FLAG_LOSSY : 0,
        .quality = (uint8_t)quality,
        .ch_h    = (uint16_t)ch_h,
        .ch_w    = (uint16_t)ch_w,
    };
    memcpy(result,               &hdr,    sizeof(hdr));
    memcpy(result + sizeof(hdr), payload, payload_size);
    *out_size = total;

    free(payload);
    return result;
}

/*  decode */

uint8_t *gib_decode(const uint8_t *data, size_t data_size, int *out_w, int *out_h)
{
    if (data_size < sizeof(GibHeader)) return NULL;

    GibHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != GIB_MAGIC) return NULL;

    int w       = (int)hdr.width;
    int h       = (int)hdr.height;
    int lossy   = (hdr.flags & GIB_FLAG_LOSSY) != 0;
    int ch_w    = (int)hdr.ch_w;
    int ch_h    = (int)hdr.ch_h;

    const uint8_t *p = data + sizeof(hdr);

    /* read and decompress each channel */
    uint8_t *Y_plane = NULL, *Cb_plane = NULL, *Cr_plane = NULL;

    for (int ch = 0; ch < 3; ch++) {
        uint32_t zsize = read_u32le(p); p += 4;

        int cw = (ch == 0) ? w    : ch_w;
        int ch2 = (ch == 0) ? h    : ch_h;
        size_t fsize = (size_t)ch2 * ((size_t)cw + 1);

        uint8_t *filtered = (uint8_t *)malloc(fsize);
        ZSTD_decompress(filtered, fsize, p, zsize);
        p += zsize;

        uint8_t *plane = unfilter_plane(filtered, cw, ch2);
        free(filtered);

        if (ch == 0) Y_plane  = plane;
        if (ch == 1) Cb_plane = plane;
        if (ch == 2) Cr_plane = plane;
    }

    uint8_t *rgb = (uint8_t *)malloc(w * h * 3);
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t Y  = Y_plane[row * w + col];
            uint8_t Cb, Cr;
            if (lossy) {
                int ci = (row/2) * ch_w + (col/2);
                Cb = Cb_plane[ci];
                Cr = Cr_plane[ci];
            } else {
                Cb = Cb_plane[row * w + col];
                Cr = Cr_plane[row * w + col];
            }
            uint8_t *out = rgb + (row * w + col) * 3;
            ycbcr_to_rgb(Y, Cb, Cr, &out[0], &out[1], &out[2]);
        }
    }

    *out_w = w; *out_h = h;
    free(Y_plane); free(Cb_plane); free(Cr_plane);
    return rgb;
}

#endif
#endif
