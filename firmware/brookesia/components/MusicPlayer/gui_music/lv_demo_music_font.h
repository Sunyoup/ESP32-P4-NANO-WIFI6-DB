/**
 * @file lv_demo_music_font.h
 *
 * Music Player font from the SD card. The font file is a TrueType/OpenType (CFF)
 * subset that covers Latin, Hangul, kana and the characters in the file names.
 * Without the file the Montserrat fonts are used.
 */

#ifndef LV_DEMO_MUSIC_FONT_H
#define LV_DEMO_MUSIC_FONT_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the font file into PSRAM and create the font sizes used by the Music Player.
 *
 * Must be called after the SD card is mounted. Calling it again does nothing once loaded.
 *
 * @param path Font file path, e.g. "/sdcard/font/music.otf"
 * @return true if the font is available
 */
bool lv_demo_music_font_load(const char *path);

/**
 * @brief Get the font of the given pixel size, or the fallback if it is not loaded.
 *
 * @param px Font size in pixels (12, 16, 22 or 32)
 * @param fallback Font to use when the file is not loaded
 */
const lv_font_t *lv_demo_music_font_or(int px, const lv_font_t *fallback);

#ifdef __cplusplus
}
#endif

#endif /* LV_DEMO_MUSIC_FONT_H */
