/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "MusicPlayer.hpp"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:MusicPlayer"
#include "esp_lib_utils.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "gui_music/lv_demo_music.h"
#include "gui_music/lv_demo_music_main.h"
#include "gui_music/lv_demo_music_font.h"
#include <dirent.h>

#define MUSIC_DIR BSP_SD_MOUNT_POINT "/music"
#define MUSIC_FONT_FILE BSP_SD_MOUNT_POINT "/font/music.otf"

LV_IMG_DECLARE(img_app_musicplayer);

static const char *TAG = "MusicPlayer";

static bool sd_card_ready(void)
{
    // VideoPlayer may already have mounted the card, and mounting it twice fails
    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    if (dir) {
        closedir(dir);
        return true;
    }
    return bsp_sdcard_mount() == ESP_OK;
}

static void next_track_async(void *arg)
{
    LV_UNUSED(arg);
    lv_demo_music_album_next(true);
}

// Called from the audio task for every player event. Only the IDLE event means
// the file really ended; the LVGL part runs later in the GUI task.
static void audio_event_cb(audio_player_cb_ctx_t *ctx)
{
    if (ctx->audio_event != AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        return;
    }
    bsp_display_lock(-1);
    lv_async_call(next_track_async, NULL);
    bsp_display_unlock();
}

static void show_status_text(const char *text)
{
    bsp_display_lock(-1);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, BSP_LCD_H_RES);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    bsp_display_unlock();
}

namespace esp_brookesia::apps
{

    MusicPlayer *MusicPlayer::_instance = nullptr;

    MusicPlayer *MusicPlayer::requestInstance(bool use_status_bar, bool use_navigation_bar)
    {
        if (_instance == nullptr)
        {
            _instance = new MusicPlayer(use_status_bar, use_navigation_bar);
        }
        return _instance;
    }

    MusicPlayer::MusicPlayer(bool use_status_bar, bool use_navigation_bar) : App("MusicPlayer", &img_app_musicplayer, true, use_status_bar, use_navigation_bar),
                                                                             _file_iterator(NULL)
    {
    }

    MusicPlayer::~MusicPlayer()
    {
    }

    bool MusicPlayer::run(void)
    {
        ESP_UTILS_LOGD("Run");

        if (bsp_extra_player_init() != ESP_OK)
        {
            ESP_LOGE(TAG, "Play init with SPIFFS failed");
            return false;
        }

        if (!sd_card_ready())
        {
            ESP_LOGE(TAG, "Failed to mount SD card");
            show_status_text("sd error");
            return true;
        }

        // Optional: without the font file the built-in Montserrat fonts are used
        lv_demo_music_font_load(MUSIC_FONT_FILE);

        if (bsp_extra_file_instance_init(MUSIC_DIR, &_file_iterator) != ESP_OK ||
            file_iterator_get_count(_file_iterator) == 0)
        {
            ESP_LOGE(TAG, "No mp3 files in %s", MUSIC_DIR);
            show_status_text("No mp3 in /sdcard/music");
            return true;
        }

        bsp_extra_player_register_callback(audio_event_cb, NULL);
        lv_demo_music(lv_scr_act(), _file_iterator);
        return true;
    }

    bool MusicPlayer::back(void)
    {
        ESP_UTILS_LOGD("Back");
        // If the app needs to exit, call notifyCoreClosed() to notify the core to close the app
        ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
        return true;
    }

    bool MusicPlayer::close(void)
    {
        ESP_UTILS_LOGD("Close");
        bsp_extra_player_register_callback(NULL, NULL);
        if (audio_player_pause() != ESP_OK)
        {
            ESP_LOGE(TAG, "audio_player_pause failed");
            return false;
        }
        if (bsp_extra_player_del() != ESP_OK)
        {
            ESP_LOGE(TAG, "DEL Play init with SPIFFS failed");
            return false;
        }
        return true;
    }

    bool MusicPlayer::init()
    {
        ESP_UTILS_LOGD("Init");
        return true;
    }

    bool MusicPlayer::deinit()
    {
        ESP_UTILS_LOGD("Deinit");
        return true;
    }

    bool MusicPlayer::pause()
    {
        ESP_UTILS_LOGD("Pause");
        lv_demo_music_exit_pause();
        return true;
    }

    bool MusicPlayer::resume()
    {
        ESP_UTILS_LOGD("Resume");
        return true;
    }

} // namespace esp_brookesia::apps