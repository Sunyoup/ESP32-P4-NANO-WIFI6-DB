/**
 * @file lv_demo_music_font.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdlib.h>
#include "lv_demo_music_font.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "music_font"

/**********************
 *  STATIC VARIABLES
 **********************/
typedef struct {
    int px;
    const lv_font_t *font;
} music_font_entry_t;

static uint8_t *font_data = NULL;
static music_font_entry_t font_entries[] = {
    {12, NULL},
    {16, NULL},
    {22, NULL},
    {32, NULL},
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lv_demo_music_font_load(const char *path)
{
    if (font_data != NULL) {
        return true;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "font file not found: %s, using built-in font", path);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Keep the font data in PSRAM; the TinyTTF fonts read it for their whole lifetime
    uint8_t *data = (size > 0) ? heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM) : NULL;
    if (data == NULL || fread(data, 1, (size_t)size, fp) != (size_t)size) {
        ESP_LOGE(TAG, "failed to read font file (%ld bytes)", size);
        free(data);
        fclose(fp);
        return false;
    }
    fclose(fp);

    for (size_t i = 0; i < sizeof(font_entries) / sizeof(font_entries[0]); i++) {
        font_entries[i].font = lv_tiny_ttf_create_data(data, (size_t)size, font_entries[i].px);
        if (font_entries[i].font == NULL) {
            ESP_LOGE(TAG, "failed to create %d px font", font_entries[i].px);
        }
    }

    font_data = data;
    ESP_LOGI(TAG, "loaded font %s (%ld bytes)", path, size);
    return true;
}

const lv_font_t *lv_demo_music_font_or(int px, const lv_font_t *fallback)
{
    for (size_t i = 0; i < sizeof(font_entries) / sizeof(font_entries[0]); i++) {
        if (font_entries[i].px == px && font_entries[i].font != NULL) {
            return font_entries[i].font;
        }
    }
    return fallback;
}
