/**
 * @file lv_demo_music_cover.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lv_demo_music_cover.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "music_cover"
#define ID3_HEADER_SIZE 10
#define ID3_MAX_TAG_SIZE (2 * 1024 * 1024)

/**********************
 *  STATIC FUNCTIONS
 **********************/

static uint32_t syncsafe32(const uint8_t * p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static uint32_t be32(const uint8_t * p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Find the APIC frame in the ID3v2.3 / v2.4 tag and return a copy of its JPEG data (in PSRAM) */
static uint8_t * read_apic_jpeg(const char * path, size_t * out_size)
{
    FILE * fp = fopen(path, "rb");
    if(fp == NULL) return NULL;

    uint8_t hdr[ID3_HEADER_SIZE];
    uint8_t * tag = NULL;
    uint8_t * jpeg = NULL;

    if(fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) || memcmp(hdr, "ID3", 3) != 0) goto done;
    uint8_t ver = hdr[3];
    if(ver != 3 && ver != 4) goto done;

    size_t tag_size = syncsafe32(hdr + 6);
    if(tag_size < 10 || tag_size > ID3_MAX_TAG_SIZE) goto done;

    tag = heap_caps_malloc(tag_size, MALLOC_CAP_SPIRAM);
    if(tag == NULL || fread(tag, 1, tag_size, fp) != tag_size) goto done;

    size_t pos = 0;
    if(hdr[5] & 0x40) {  /* extended header: v2.4 size includes itself, v2.3 size does not */
        pos = (ver == 4) ? syncsafe32(tag) : be32(tag) + 4;
    }

    while(pos + 10 <= tag_size) {
        const uint8_t * fh = tag + pos;
        if(fh[0] == 0) break;  /* padding */

        size_t fsize = (ver == 4) ? syncsafe32(fh + 4) : be32(fh + 4);
        if(pos + 10 + fsize > tag_size) break;

        if(memcmp(fh, "APIC", 4) == 0) {
            const uint8_t * p = fh + 10;
            const uint8_t * end = p + fsize;

            uint8_t enc = *p++;                        /* text encoding of the description */
            while(p < end && *p) p++;                  /* MIME type, NUL terminated */
            if(p >= end) break;
            p++;
            if(p >= end) break;
            p++;                                       /* picture type */

            if(enc == 1 || enc == 2) {                 /* UTF-16: description ends with 2 NULs */
                while(p + 1 < end && !(p[0] == 0 && p[1] == 0)) p += 2;
                if(p + 1 >= end) break;
                p += 2;
            }
            else {                                     /* Latin-1 / UTF-8: one NUL */
                while(p < end && *p) p++;
                if(p >= end) break;
                p++;
            }

            if(end - p < 2 || p[0] != 0xFF || p[1] != 0xD8) {
                ESP_LOGW(TAG, "APIC picture is not JPEG: %s", path);
                break;
            }

            jpeg = heap_caps_malloc((size_t)(end - p), MALLOC_CAP_SPIRAM);
            if(jpeg) {
                memcpy(jpeg, p, (size_t)(end - p));
                *out_size = (size_t)(end - p);
            }
            break;
        }

        pos += 10 + fsize;
    }

done:
    heap_caps_free(tag);
    fclose(fp);
    return jpeg;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_image_dsc_t * lv_demo_music_cover_create(const char * mp3_path)
{
    size_t jpeg_size = 0;
    uint8_t * jpeg = read_apic_jpeg(mp3_path, &jpeg_size);
    if(jpeg == NULL) return NULL;

    lv_image_dsc_t * dsc = NULL;
    uint8_t * out = NULL;
    jpeg_dec_handle_t handle = NULL;

    jpeg_dec_config_t cfg = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_LE,  /* LVGL RGB565 is little-endian */
        .scale = {.width = 0, .height = 0},
        .clipper = {.width = 0, .height = 0},
        .rotate = JPEG_ROTATE_0D,
        .block_enable = false,
    };
    jpeg_dec_io_t io = {0};
    jpeg_dec_header_info_t info = {0};
    int out_len = 0;

    if(jpeg_dec_open(&cfg, &handle) != JPEG_ERR_OK) goto fail;

    io.inbuf = jpeg;
    io.inbuf_len = (int)jpeg_size;
    if(jpeg_dec_parse_header(handle, &io, &info) != JPEG_ERR_OK) goto fail;
    if(jpeg_dec_get_outbuf_len(handle, &out_len) != JPEG_ERR_OK || out_len <= 0) goto fail;

    out = heap_caps_aligned_calloc(16, 1, (size_t)out_len, MALLOC_CAP_SPIRAM);
    if(out == NULL) {
        ESP_LOGE(TAG, "no memory for %d bytes of cover art", out_len);
        goto fail;
    }

    io.outbuf = out;
    io.out_size = out_len;
    if(jpeg_dec_process(handle, &io) != JPEG_ERR_OK) goto fail;

    /* Center square crop, converted to ARGB8888 with the pixels outside the circle transparent,
     * so the picture is round no matter how the widget clips it */
    uint32_t side = info.width < info.height ? info.width : info.height;
    uint32_t x0 = (info.width - side) / 2;
    uint32_t y0 = (info.height - side) / 2;
    int32_t radius2 = (int32_t)(side / 2) * (int32_t)(side / 2);
    int32_t center = (int32_t)(side / 2);

    uint8_t * argb = heap_caps_aligned_calloc(16, 1, (size_t)side * side * 4, MALLOC_CAP_SPIRAM);
    if(argb == NULL) {
        ESP_LOGE(TAG, "no memory for %lu x %lu cover", (unsigned long)side, (unsigned long)side);
        goto fail;
    }

    for(uint32_t y = 0; y < side; y++) {
        for(uint32_t x = 0; x < side; x++) {
            uint8_t * dst = argb + (y * side + x) * 4;
            int32_t dx = (int32_t)x - center;
            int32_t dy = (int32_t)y - center;
            if(dx * dx + dy * dy > radius2) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
                continue;
            }
            /* RGB565 little-endian source pixel */
            const uint8_t * src = out + ((y0 + y) * info.width + (x0 + x)) * 2;
            uint16_t v = (uint16_t)(src[0] | (src[1] << 8));
            uint8_t r = (v >> 11) & 0x1F;
            uint8_t g = (v >> 5) & 0x3F;
            uint8_t b = v & 0x1F;
            dst[0] = (b << 3) | (b >> 2);   /* LVGL ARGB8888 byte order: B, G, R, A */
            dst[1] = (g << 2) | (g >> 4);
            dst[2] = (r << 3) | (r >> 2);
            dst[3] = 0xFF;
        }
    }

    dsc = calloc(1, sizeof(lv_image_dsc_t));
    if(dsc == NULL) {
        heap_caps_free(argb);
        goto fail;
    }

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = side;
    dsc->header.h = side;
    dsc->header.stride = side * 4;
    dsc->data = argb;
    dsc->data_size = side * side * 4;

    ESP_LOGI(TAG, "cover %ux%u -> round %lu px from %s", info.width, info.height, (unsigned long)side, mp3_path);
    heap_caps_free(out);
    out = NULL;

fail:
    if(handle) jpeg_dec_close(handle);
    heap_caps_free(jpeg);
    heap_caps_free(out);
    return dsc;
}

void lv_demo_music_cover_destroy(void * dsc)
{
    lv_image_dsc_t * d = (lv_image_dsc_t *)dsc;
    if(d == NULL) return;
    heap_caps_free((void *)d->data);
    free(d);
}
