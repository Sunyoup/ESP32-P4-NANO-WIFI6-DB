/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

static const char *TAG = "bsp_extra_board";

static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

static bool _is_audio_init = false;
static bool _is_player_init = false;
static int _vloume_intensity = CODEC_DEFAULT_VOLUME;

static audio_player_cb_t audio_idle_callback = NULL;
static void *audio_idle_cb_user_data = NULL;
static char audio_file_path[128];

#define BSP_EXTRA_BRIGHTNESS_I2C_ADDR    (0x45)
#define BSP_EXTRA_BRIGHTNESS_REG         (0x96)
#define BSP_EXTRA_BRIGHTNESS_DEFAULT     (100)

/**************************************************************************************************
 *
 * Extra Board Function
 *
 **************************************************************************************************/

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    // Volume saved when muting and restored when unmuting. Restoring volume is necessary
    // as es8311_set_voice_mute(true) results in voice volume (REG32) being set to zero.

    bsp_extra_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);

    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, _vloume_intensity), TAG, "Set Codec volume failed");
    }

    return ESP_OK;
}

static void audio_callback(audio_player_cb_ctx_t *ctx)
{
    if (audio_idle_callback) {
        ctx->user_ctx = audio_idle_cb_user_data;
        audio_idle_callback(ctx);
    }
}

esp_err_t bsp_extra_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    *bytes_read = len;
    return ret;
}

esp_err_t bsp_extra_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    *bytes_written = len;
    return ret;
}

esp_err_t bsp_extra_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, CODEC_DEFAULT_ADC_VOLUME);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }
    return ret;
}

esp_err_t bsp_extra_codec_volume_set(int volume, int *volume_set)
{
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, volume), TAG, "Set Codec volume failed");
    _vloume_intensity = volume;

    ESP_LOGI(TAG, "Setting volume: %d", volume);

    return ESP_OK;
}

int bsp_extra_codec_volume_get(void)
{
    return _vloume_intensity;
}

esp_err_t bsp_extra_codec_mute_set(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

esp_err_t bsp_extra_codec_dev_stop(void)
{
    esp_err_t ret = ESP_OK;

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (record_dev_handle) {
        ret = esp_codec_dev_close(record_dev_handle);
    }
    return ret;
}

esp_err_t bsp_extra_codec_dev_resume(void)
{
    return bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);
}

esp_err_t bsp_extra_codec_init()
{
    if (_is_audio_init) {
        return ESP_OK;
    }

    play_dev_handle = bsp_audio_codec_speaker_init();
    assert((play_dev_handle) && "play_dev_handle not initialized");

    record_dev_handle = bsp_audio_codec_microphone_init();
    assert((record_dev_handle) && "record_dev_handle not initialized");

    bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);

    _is_audio_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_init(void)
{
    if (_is_player_init) {
        return ESP_OK;
    }

    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_extra_i2s_write,
                                     .clk_set_fn = bsp_extra_codec_set_fs,
                                     .priority = 5
                                   };
    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "audio_player_init failed");
    audio_player_callback_register(audio_callback, NULL);

    _is_player_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_del(void)
{
    _is_player_init = false;

    ESP_RETURN_ON_ERROR(audio_player_delete(), TAG, "audio_player_delete failed");

    return ESP_OK;
}

static bool is_mp3_file(const struct dirent *entry)
{
    // Skip hidden files such as macOS "._*.mp3" metadata written to the SD card
    if (entry->d_type != DT_REG || entry->d_name[0] == '.') {
        return false;
    }
    const char *ext = strrchr(entry->d_name, '.');
    return ext && strcasecmp(ext, ".mp3") == 0;
}

static int mp3_name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

esp_err_t bsp_extra_file_instance_init(const char *path, file_iterator_instance_t **ret_instance)
{
    ESP_RETURN_ON_FALSE(path, ESP_FAIL, TAG, "path is NULL");
    ESP_RETURN_ON_FALSE(ret_instance, ESP_FAIL, TAG, "ret_instance is NULL");

    DIR *dir = opendir(path);
    ESP_RETURN_ON_FALSE(dir, ESP_FAIL, TAG, "opendir failed, %s", path);

    // First pass counts the mp3 files so the list is allocated once
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (is_mp3_file(entry)) {
            count++;
        }
    }
    rewinddir(dir);

    file_iterator_instance_t *instance = calloc(1, sizeof(file_iterator_instance_t));
    char **list = calloc(count ? count : 1, sizeof(char *));
    if (!instance || !list) {
        free(instance);
        free(list);
        closedir(dir);
        ESP_LOGE(TAG, "no memory for mp3 list");
        return ESP_ERR_NO_MEM;
    }

    size_t index = 0;
    while (index < count && (entry = readdir(dir)) != NULL) {
        if (is_mp3_file(entry)) {
            list[index++] = strdup(entry->d_name);
        }
    }
    closedir(dir);

    // readdir order on FAT is not alphabetical, so sort for a stable playlist
    qsort(list, count, sizeof(char *), mp3_name_cmp);

    instance->count = count;
    instance->index = 0;
    instance->list = list;
    instance->directory_path = strdup(path);
    *ret_instance = instance;

    return ESP_OK;
}

// MPEG-1 and MPEG-2/2.5 Layer III bitrate tables (kbps), index 0 and 15 are invalid
static const uint16_t mp3_bitrate_v1_l3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const uint16_t mp3_bitrate_v2_l3[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};

typedef struct {
    uint32_t audio_offset;   // first byte after the ID3v2 tag
    uint32_t bitrate_kbps;   // bitrate of the first Layer III frame
    uint32_t file_size;
} mp3_info_t;

// Read the ID3v2 size and the first frame's bitrate. Returns false if no frame is found.
static bool mp3_read_info(const char *path, mp3_info_t *info)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }

    uint8_t buf[4096];
    memset(info, 0, sizeof(*info));

    fseek(fp, 0, SEEK_END);
    info->file_size = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Skip the ID3v2 tag (its size is a 28-bit syncsafe integer)
    if (fread(buf, 1, 10, fp) == 10 && memcmp(buf, "ID3", 3) == 0) {
        info->audio_offset = 10 + (((uint32_t)(buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) |
                                   ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f));
    }

    fseek(fp, info->audio_offset, SEEK_SET);
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    for (size_t i = 0; i + 4 <= n; i++) {
        if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        int version = (buf[i + 1] >> 3) & 0x3;  // 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5
        int layer = (buf[i + 1] >> 1) & 0x3;    // 1 = Layer III
        int bitrate_idx = buf[i + 2] >> 4;
        if (version == 1 || layer != 1 || bitrate_idx == 0 || bitrate_idx == 15) {
            continue;
        }
        info->bitrate_kbps = (version == 3) ? mp3_bitrate_v1_l3[bitrate_idx] : mp3_bitrate_v2_l3[bitrate_idx];
        break;
    }

    return info->bitrate_kbps != 0 && info->file_size > info->audio_offset;
}

uint32_t bsp_extra_mp3_duration_sec(const char *path)
{
    mp3_info_t info;
    if (!mp3_read_info(path, &info)) {
        return 0;
    }
    // Constant bitrate assumed: duration = audio bytes * 8 / bitrate
    uint64_t audio_bytes = (uint64_t)(info.file_size - info.audio_offset);
    return (uint32_t)((audio_bytes * 8) / ((uint64_t)info.bitrate_kbps * 1000));
}

esp_err_t bsp_extra_player_play_index_at(file_iterator_instance_t *instance, int index, uint32_t start_sec)
{
    ESP_RETURN_ON_FALSE(instance, ESP_FAIL, TAG, "instance is NULL");

    char filename[256];
    int retval = file_iterator_get_full_path_from_index(instance, index, filename, sizeof(filename));
    ESP_RETURN_ON_FALSE(retval > 0 && retval < (int)sizeof(filename), ESP_FAIL, TAG, "file path failed");

    mp3_info_t info;
    ESP_RETURN_ON_FALSE(mp3_read_info(filename, &info), ESP_FAIL, TAG, "not a readable mp3: %s", filename);

    // Byte position of the requested time, from the CBR bitrate (kbps * 1000 / 8 = bytes per second)
    uint32_t start_offset = info.audio_offset;
    if (start_sec > 0) {
        uint64_t pos = (uint64_t)info.audio_offset + (uint64_t)start_sec * info.bitrate_kbps * 125;
        ESP_RETURN_ON_FALSE(pos < info.file_size, ESP_ERR_INVALID_ARG, TAG, "start %lu s is past the end", (unsigned long)start_sec);
        start_offset = (uint32_t)pos;
    }

    ESP_LOGI(TAG, "seek '%s' to %lu s (byte %lu)", filename, (unsigned long)start_sec, (unsigned long)start_offset);
    FILE *fp = fopen(filename, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    esp_err_t ret = audio_player_play_at(fp, start_offset);
    if (ret != ESP_OK) {
        fclose(fp);  // on success the audio player closes fp
        return ret;
    }

    snprintf(audio_file_path, sizeof(audio_file_path), "%s", filename);
    return ESP_OK;
}

esp_err_t bsp_extra_player_play_index(file_iterator_instance_t *instance, int index)
{
    ESP_RETURN_ON_FALSE(instance, ESP_FAIL, TAG, "instance is NULL");

    ESP_LOGI(TAG, "play_index(%d)", index);
    char filename[128];
    int retval = file_iterator_get_full_path_from_index(instance, index, filename, sizeof(filename));
    ESP_RETURN_ON_FALSE(retval != 0, ESP_FAIL, TAG, "file_iterator_get_full_path_from_index failed");

    ESP_LOGI(TAG, "opening file '%s'", filename);
    FILE *fp = fopen(filename, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", filename);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    memcpy(audio_file_path, filename, sizeof(audio_file_path));

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_file(const char *file_path)
{
    ESP_LOGI(TAG, "opening file '%s'", file_path);
    FILE *fp = fopen(file_path, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", file_path);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    memcpy(audio_file_path, file_path, sizeof(audio_file_path));

    return ESP_OK;
}

void bsp_extra_player_register_callback(audio_player_cb_t cb, void *user_data)
{
    audio_idle_callback = cb;
    audio_idle_cb_user_data = user_data;
}

bool bsp_extra_player_is_playing_by_path(const char *file_path)
{
    return (strcmp(audio_file_path, file_path) == 0);
}

bool bsp_extra_player_is_playing_by_index(file_iterator_instance_t *instance, int index)
{
    return (index == file_iterator_get_index(instance));
}
