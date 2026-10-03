/**
 * @file lv_demo_music_cover.h
 *
 * Album art of an mp3 file: the JPEG in the ID3v2 APIC (picture) frame, decoded to RGB565.
 */

#ifndef LV_DEMO_MUSIC_COVER_H
#define LV_DEMO_MUSIC_COVER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read and decode the album art of an mp3 file.
 *
 * @param mp3_path Path of the mp3 file on the SD card
 * @return A new image descriptor (free it with lv_demo_music_cover_destroy()),
 *         or NULL if the file has no JPEG cover art
 */
lv_image_dsc_t * lv_demo_music_cover_create(const char * mp3_path);

/**
 * @brief Free an image descriptor returned by lv_demo_music_cover_create()
 */
void lv_demo_music_cover_destroy(void * dsc);

#ifdef __cplusplus
}
#endif

#endif /* LV_DEMO_MUSIC_COVER_H */
