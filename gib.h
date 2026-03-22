#ifndef GIB_H
#define GIB_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GIB_MAGIC      0x31424947u
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

static void delta_encode(const uint8_t *src, uint8_t *dst, int len)
{
    dst[0] = src[0];
    for (int i = 1; i < len; i++)
        dst[i] = (uint8_t)((int)src[i] - (int)src[i-1]);
}

static void delta_decode(const uint8_t *src, uint8_t *dst, int len)
{
    dst[0] = src[0];
    for (int i = 1; i < len; i++)
        dst[i] = (uint8_t)((int)dst[i-1] + (int)(int8_t)src[i]);
}

static void write_u32le(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t *gib_encode(const uint8_t *pixels, int w, int h, int quality, size_t *out_size)
{
    int lossy   = (quality > 0);
    int ch_w    = lossy ? (w + 1) / 2 : w;
    int ch_h    = lossy ? (h + 1) / 2 : h;
    int y_size  = w * h;
    int ch_size = ch_w * ch_h;

    uint8_t *Y_plane  = (uint8_t*)malloc(y_size);
    uint8_t *Cb_plane = (uint8_t*)malloc(ch_size);
    uint8_t *Cr_plane = (uint8_t*)malloc(ch_size);
    uint8_t *Y_delta  = (uint8_t*)malloc(y_size);
    uint8_t *Cb_delta = (uint8_t*)malloc(ch_size);
    uint8_t *Cr_delta = (uint8_t*)malloc(ch_size);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const uint8_t *px = pixels + (row * w + col) * 3;
            uint8_t Y, Cb, Cr;
            rgb_to_ycbcr(px[0], px[1], px[2], &Y, &Cb, &Cr);
            Y_plane[row * w + col] = Y;
            if (lossy) {
                if (row % 2 == 0 && col % 2 == 0) {
                    int ci = (row / 2) * ch_w + (col / 2);
                    Cb_plane[ci] = Cb;
                    Cr_plane[ci] = Cr;
                }
            } else {
                Cb_plane[row * w + col] = Cb;
                Cr_plane[row * w + col] = Cr;
            }
        }
    }

    delta_encode(Y_plane,  Y_delta,  y_size);
    delta_encode(Cb_plane, Cb_delta, ch_size);
    delta_encode(Cr_plane, Cr_delta, ch_size);

    size_t   payload_size = 3 * 4 + y_size + 2 * ch_size;
    uint8_t *payload = (uint8_t*)malloc(payload_size);
    uint8_t *p = payload;

    write_u32le(p, (uint32_t)y_size);  p += 4;
    memcpy(p, Y_delta,  y_size);        p += y_size;
    write_u32le(p, (uint32_t)ch_size); p += 4;
    memcpy(p, Cb_delta, ch_size);       p += ch_size;
    write_u32le(p, (uint32_t)ch_size); p += 4;
    memcpy(p, Cr_delta, ch_size);       p += ch_size;

    int    zstd_level = lossy ? (10 + quality) : 22;
    size_t bound      = ZSTD_compressBound(payload_size);
    uint8_t *zbuf     = (uint8_t*)malloc(bound);
    size_t   zsize    = ZSTD_compress(zbuf, bound, payload, payload_size, zstd_level);

    size_t   total  = sizeof(GibHeader) + zsize;
    uint8_t *result = (uint8_t*)malloc(total);

    GibHeader hdr = {
        .magic   = GIB_MAGIC,
        .width   = (uint32_t)w,
        .height  = (uint32_t)h,
        .flags   = lossy ? GIB_FLAG_LOSSY : 0,
        .quality = (uint8_t)quality,
        .ch_h    = (uint16_t)ch_h,
        .ch_w    = (uint16_t)ch_w,
    };
    memcpy(result,                &hdr,  sizeof(hdr));
    memcpy(result + sizeof(hdr),  zbuf,  zsize);
    *out_size = total;

    free(Y_plane); free(Cb_plane); free(Cr_plane);
    free(Y_delta); free(Cb_delta); free(Cr_delta);
    free(payload); free(zbuf);
    return result;
}

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
    int y_size  = w * h;
    int ch_size = ch_w * ch_h;

    const uint8_t *zbuf  = data + sizeof(hdr);
    size_t         zsize = data_size - sizeof(hdr);
    size_t payload_size  = (size_t)ZSTD_getFrameContentSize(zbuf, zsize);

    uint8_t *payload = (uint8_t*)malloc(payload_size);
    ZSTD_decompress(payload, payload_size, zbuf, zsize);

    uint8_t *p = payload;
    uint32_t y_len  = read_u32le(p); p += 4;
    uint8_t *Y_delta  = p;           p += y_len;
    uint32_t cb_len = read_u32le(p); p += 4;
    uint8_t *Cb_delta = p;           p += cb_len;
    p += 4;
    uint8_t *Cr_delta = p;

    uint8_t *Y_plane  = (uint8_t*)malloc(y_size);
    uint8_t *Cb_plane = (uint8_t*)malloc(ch_size);
    uint8_t *Cr_plane = (uint8_t*)malloc(ch_size);

    delta_decode(Y_delta,  Y_plane,  y_size);
    delta_decode(Cb_delta, Cb_plane, ch_size);
    delta_decode(Cr_delta, Cr_plane, ch_size);

    uint8_t *rgb = (uint8_t*)malloc(w * h * 3);
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t Y  = Y_plane[row * w + col];
            uint8_t Cb, Cr;
            if (lossy) {
                int ci = (row / 2) * ch_w + (col / 2);
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
    free(payload); free(Y_plane); free(Cb_plane); free(Cr_plane);
    return rgb;
}

#endif
#endif