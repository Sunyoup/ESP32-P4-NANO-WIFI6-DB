/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "Water2D.hpp"
#include "lvgl.h"
#include "esp_brookesia.hpp"

#include "bsp/touch.h"
#include "esp_lib_utils.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_cache.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstring>
#include <inttypes.h>

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Water2D"

LV_IMG_DECLARE(img_app_water2d);

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB565
#define WATER_PPA_COLOR_MODE PPA_SRM_COLOR_MODE_RGB565
#define WATER_BYTES_PER_PIXEL 2
#else
#error "Water2D only implements the RGB565 panel format"
#endif

// Simulation tuning. Values are picked to look reasonable on a 720x1280 panel; adjust to taste.
static constexpr int DAMPING_SHIFT = 5;    // higher = ripples die out slower
static constexpr int REFRACT_SHIFT = 5;    // higher = weaker/thinner refraction distortion
static constexpr int LIGHT_SHIFT = 4;      // higher = dimmer, thinner-looking highlight bands
static constexpr int SPLASH_RADIUS = 1;    // grid cells; smaller = thinner ripple source
static constexpr int16_t SPLASH_AMOUNT = -1200;
static constexpr int32_t HEIGHT_CLAMP = 20000;

namespace esp_brookesia::apps
{

    static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // Brighten (positive) or darken (negative) an RGB565 pixel. Green gets double the step
    // since it has 6 bits instead of 5, keeping the tint roughly neutral.
    static inline uint16_t shade(uint16_t c, int add)
    {
        int r = (c >> 11) & 0x1F;
        int g = (c >> 5) & 0x3F;
        int b = c & 0x1F;
        r = std::clamp(r + add, 0, 31);
        g = std::clamp(g + add * 2, 0, 63);
        b = std::clamp(b + add, 0, 31);
        return static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }

    Water2D *Water2D::_instance = nullptr;

    Water2D *Water2D::requestInstance(bool use_status_bar, bool use_navigation_bar)
    {
        if (_instance == nullptr) {
            _instance = new Water2D(use_status_bar, use_navigation_bar);
        }
        return _instance;
    }

    Water2D::Water2D(bool use_status_bar, bool use_navigation_bar) :
        App("2D Water", &img_app_water2d, true, use_status_bar, use_navigation_bar)
    {
    }

    Water2D::~Water2D()
    {
        close();

        if (_height[0]) {
            heap_caps_free(_height[0]);
            _height[0] = nullptr;
        }
        if (_height[1]) {
            heap_caps_free(_height[1]);
            _height[1] = nullptr;
        }
        if (_checker) {
            heap_caps_free(_checker);
            _checker = nullptr;
        }
        if (_render_buf) {
            heap_caps_free(_render_buf);
            _render_buf = nullptr;
        }
    }

    bool Water2D::run(void)
    {
        ESP_UTILS_LOGD("Run");
        if (!startSim()) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Start simulation failed");
        }
        return true;
    }

    bool Water2D::back(void)
    {
        ESP_UTILS_LOGD("Back");
        requestStopSim();
        ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
        return true;
    }

    bool Water2D::close()
    {
        ESP_UTILS_LOGD("Close");
        requestStopSim();
        return true;
    }

    bool Water2D::init()
    {
        ESP_UTILS_LOGD("Init");
        return true;
    }

    bool Water2D::deinit()
    {
        ESP_UTILS_LOGD("Deinit");
        return true;
    }

    bool Water2D::pause()
    {
        ESP_UTILS_LOGD("Pause");
        requestStopSim();
        return true;
    }

    bool Water2D::resume()
    {
        ESP_UTILS_LOGD("Resume");
        return true;
    }

    void Water2D::allocateBuffers()
    {
#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
        _data_cache_line_size = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
#else
        _data_cache_line_size = 64;
#endif
        const size_t height_bytes = ALIGN_UP(GRID_CELLS * sizeof(int16_t), _data_cache_line_size);
        const size_t pixel_bytes = ALIGN_UP(GRID_CELLS * sizeof(uint16_t), _data_cache_line_size);

        // Allocated once and kept for the app's lifetime (a few hundred KB in PSRAM), same reasoning
        // as Camera's detect input buffer: cheap to keep, avoids alloc churn across open/close.
        if (!_height[0]) {
            _height[0] = static_cast<int16_t *>(heap_caps_aligned_calloc(_data_cache_line_size, 1, height_bytes, MALLOC_CAP_SPIRAM));
        }
        if (!_height[1]) {
            _height[1] = static_cast<int16_t *>(heap_caps_aligned_calloc(_data_cache_line_size, 1, height_bytes, MALLOC_CAP_SPIRAM));
        }
        if (!_checker) {
            _checker = static_cast<uint16_t *>(heap_caps_aligned_calloc(_data_cache_line_size, 1, pixel_bytes, MALLOC_CAP_SPIRAM));
        }
        if (!_render_buf) {
            _render_buf = static_cast<uint16_t *>(heap_caps_aligned_calloc(_data_cache_line_size, 1, pixel_bytes, MALLOC_CAP_SPIRAM));
        }
    }

    void Water2D::buildCheckerTexture()
    {
        constexpr uint16_t DARK = rgb565(10, 30, 90);
        constexpr uint16_t LIGHT = rgb565(55, 95, 175);
        constexpr int CELL = (GRID_W / 6) > 0 ? (GRID_W / 6) : 1;

        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                bool a = ((x / CELL) + (y / CELL)) & 1;
                _checker[y * GRID_W + x] = a ? LIGHT : DARK;
            }
        }
    }

    bool Water2D::startSim()
    {
        if (_sim_task_handle) {
            return true;
        }

        allocateBuffers();
        if (!_height[0] || !_height[1] || !_checker || !_render_buf) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Allocate simulation buffers failed");
            return false;
        }

        const size_t height_bytes = ALIGN_UP(GRID_CELLS * sizeof(int16_t), _data_cache_line_size);
        memset(_height[0], 0, height_bytes);
        memset(_height[1], 0, height_bytes);
        _height_parity = 0;
        buildCheckerTexture();

        _sim_running = true;
        BaseType_t ret = xTaskCreatePinnedToCore(
            simTaskEntry,
            "water2d_sim",
            SIM_TASK_STACK_SIZE,
            this,
            SIM_TASK_PRIORITY,
            &_sim_task_handle,
            0);

        if (ret != pdPASS) {
            _sim_running = false;
            _sim_task_handle = nullptr;
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Create water2d sim task failed");
            return false;
        }

        return true;
    }

    void Water2D::requestStopSim()
    {
        if (!_sim_task_handle) {
            stopDummyDraw();
            return;
        }

        if (xTaskGetCurrentTaskHandle() == _sim_task_handle) {
            _sim_running = false;
            return;
        }

        _sim_running = false;
        _close_wait_task = xTaskGetCurrentTaskHandle();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
        _close_wait_task = nullptr;
    }

    esp_err_t Water2D::startDummyDraw()
    {
        _display = lv_display_get_default();
        ESP_RETURN_ON_FALSE(_display != nullptr, ESP_ERR_INVALID_STATE, ESP_UTILS_LOG_TAG, "LVGL display is not ready");

        _display_panel = bsp_display_get_panel_handle();
        ESP_RETURN_ON_FALSE(_display_panel != nullptr, ESP_ERR_INVALID_STATE, ESP_UTILS_LOG_TAG, "LCD panel is not ready");

#if CONFIG_BSP_LCD_DPI_BUFFER_NUMS >= 3
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(_display_panel, CONFIG_BSP_LCD_DPI_BUFFER_NUMS, &_lcd_buffers[0], &_lcd_buffers[1], &_lcd_buffers[2]), ESP_UTILS_LOG_TAG, "Get LCD frame buffers failed");
#elif CONFIG_BSP_LCD_DPI_BUFFER_NUMS == 2
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(_display_panel, CONFIG_BSP_LCD_DPI_BUFFER_NUMS, &_lcd_buffers[0], &_lcd_buffers[1]), ESP_UTILS_LOG_TAG, "Get LCD frame buffers failed");
#else
#error "Water2D needs at least two LCD frame buffers"
#endif

        if (_ppa_srm_handle == nullptr) {
            ppa_client_config_t ppa_srm_config = {
                .oper_type = PPA_OPERATION_SRM,
            };
            ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_srm_config, &_ppa_srm_handle), ESP_UTILS_LOG_TAG, "Register PPA SRM failed");
        }

        bsp_touch_config_t touch_cfg = {
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        ESP_RETURN_ON_ERROR(bsp_touch_new(&touch_cfg, &_touch_handle), ESP_UTILS_LOG_TAG, "Create touch handle failed");

        ESP_RETURN_ON_ERROR(esp_lv_adapter_set_dummy_draw(_display, true), ESP_UTILS_LOG_TAG, "Enable dummy draw failed");
        _dummy_enabled = true;

        ESP_RETURN_ON_ERROR(esp_lv_adapter_pause(-1), ESP_UTILS_LOG_TAG, "Pause LVGL failed");
        _lvgl_paused = true;

        return ESP_OK;
    }

    void Water2D::stopDummyDraw()
    {
        if (_lvgl_paused) {
            esp_lv_adapter_resume();
            _lvgl_paused = false;
        }

        if (_dummy_enabled && _display) {
            esp_lv_adapter_set_dummy_draw(_display, false);
            _dummy_enabled = false;
        }

        if (_touch_handle) {
            esp_lcd_touch_del(_touch_handle);
            _touch_handle = nullptr;
        }

        for (auto &t : _touch) {
            t.active = false;
        }
    }

    // newHeight = (sum of 4 neighbors in the latest buffer) / 2 - (value the target buffer held
    // two steps ago), then damped. This is Hugo Elias's classic 2D water algorithm (see Note.txt).
    void Water2D::simulateStep()
    {
        const int16_t *latest = _height[_height_parity];
        int16_t *target = _height[1 - _height_parity];

        for (int y = 1; y < GRID_H - 1; ++y) {
            int row = y * GRID_W;
            for (int x = 1; x < GRID_W - 1; ++x) {
                int idx = row + x;
                int32_t v = static_cast<int32_t>(latest[idx - 1]) + latest[idx + 1] + latest[idx - GRID_W] + latest[idx + GRID_W];
                v >>= 1;
                v -= target[idx];
                v -= v >> DAMPING_SHIFT;
                target[idx] = static_cast<int16_t>(std::clamp<int32_t>(v, -HEIGHT_CLAMP, HEIGHT_CLAMP));
            }
        }

        _height_parity = 1 - _height_parity;
    }

    void Water2D::splash(int gx, int gy, int16_t amount)
    {
        int16_t *latest = _height[_height_parity];

        for (int dy = -SPLASH_RADIUS; dy <= SPLASH_RADIUS; ++dy) {
            int yy = gy + dy;
            if (yy < 1 || yy >= GRID_H - 1) {
                continue;
            }
            for (int dx = -SPLASH_RADIUS; dx <= SPLASH_RADIUS; ++dx) {
                int xx = gx + dx;
                if (xx < 1 || xx >= GRID_W - 1) {
                    continue;
                }
                if (dx * dx + dy * dy > SPLASH_RADIUS * SPLASH_RADIUS) {
                    continue;
                }
                latest[yy * GRID_W + xx] = amount;
            }
        }
    }

    void Water2D::renderFrame()
    {
        const int16_t *h = _height[_height_parity];

        for (int y = 1; y < GRID_H - 1; ++y) {
            int row = y * GRID_W;
            for (int x = 1; x < GRID_W - 1; ++x) {
                int idx = row + x;
                int dxh = h[idx + 1] - h[idx - 1];
                int dyh = h[idx + GRID_W] - h[idx - GRID_W];

                int ox = std::clamp(x + (dxh >> REFRACT_SHIFT), 0, GRID_W - 1);
                int oy = std::clamp(y + (dyh >> REFRACT_SHIFT), 0, GRID_H - 1);
                uint16_t base = _checker[oy * GRID_W + ox];

                int light = (dxh + dyh) >> LIGHT_SHIFT;
                _render_buf[idx] = shade(base, light);
            }
        }

        // Border cells have no gradient to sample; just show the plain checker there.
        for (int x = 0; x < GRID_W; ++x) {
            _render_buf[x] = _checker[x];
            _render_buf[(GRID_H - 1) * GRID_W + x] = _checker[(GRID_H - 1) * GRID_W + x];
        }
        for (int y = 0; y < GRID_H; ++y) {
            _render_buf[y * GRID_W] = _checker[y * GRID_W];
            _render_buf[y * GRID_W + GRID_W - 1] = _checker[y * GRID_W + GRID_W - 1];
        }
    }

    bool Water2D::pollTouchAndExit()
    {
        if (!_touch_handle) {
            return false;
        }

        esp_lcd_touch_read_data(_touch_handle);

        esp_lcd_touch_point_data_t points[MAX_TOUCH_POINTS] = {};
        uint8_t point_num = 0;
        esp_err_t ret = esp_lcd_touch_get_data(_touch_handle, points, &point_num, MAX_TOUCH_POINTS);

        if (ret != ESP_OK || point_num == 0) {
            for (auto &t : _touch) {
                t.active = false;
            }
            return false;
        }

        // Points aren't tracked by a stable finger ID across calls, just matched by slot index.
        // That only risks a one-frame "moved" misjudgment right as a finger lifts/lands, which is
        // harmless here (it can only delay the long-press exit, never trigger it early).
        bool exit_requested = false;
        const int64_t now = esp_timer_get_time();

        for (uint8_t i = 0; i < point_num; ++i) {
            TouchState &t = _touch[i];
            if (!t.active) {
                t.active = true;
                t.moved = false;
                t.start_x = points[i].x;
                t.start_y = points[i].y;
                t.start_us = now;
            } else {
                int dx = static_cast<int>(points[i].x) - static_cast<int>(t.start_x);
                int dy = static_cast<int>(points[i].y) - static_cast<int>(t.start_y);
                if (dx * dx + dy * dy > TOUCH_MOVE_THRESHOLD * TOUCH_MOVE_THRESHOLD) {
                    t.moved = true;
                }
            }

            int gx = std::clamp(static_cast<int>(points[i].x) * GRID_W / BSP_LCD_H_RES, 0, GRID_W - 1);
            int gy = std::clamp(static_cast<int>(points[i].y) * GRID_H / BSP_LCD_V_RES, 0, GRID_H - 1);
            splash(gx, gy, SPLASH_AMOUNT);

            // Held still (no drag) long enough: treat as "done", since dragging to make ripples is
            // the normal interaction and would otherwise conflict with a swipe-to-exit gesture.
            if (!t.moved && (now - t.start_us) >= LONG_PRESS_US) {
                exit_requested = true;
            }
        }

        for (uint8_t i = point_num; i < MAX_TOUCH_POINTS; ++i) {
            _touch[i].active = false;
        }

        return exit_requested;
    }

    void Water2D::simTask()
    {
        bool request_close = false;
        esp_err_t ret = startDummyDraw();

        uint32_t buffer_index = 0;
        int64_t last_frame_us = esp_timer_get_time();
        const size_t render_bytes = ALIGN_UP(GRID_CELLS * sizeof(uint16_t), _data_cache_line_size);

        int fps_frame_count = 0;
        int64_t fps_window_start_us = esp_timer_get_time();

        while (_sim_running && ret == ESP_OK) {
            simulateStep();
            renderFrame();

            // CPU just wrote this buffer; drop stale dirty lines before PPA's DMA reads it
            esp_cache_msync(_render_buf, render_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

            void *lcd_buf = _lcd_buffers[buffer_index];
            buffer_index = (buffer_index + 1) % CONFIG_BSP_LCD_DPI_BUFFER_NUMS;

            ppa_srm_oper_config_t srm_config = {};
            srm_config.in.buffer = _render_buf;
            srm_config.in.pic_w = GRID_W;
            srm_config.in.pic_h = GRID_H;
            srm_config.in.block_w = GRID_W;
            srm_config.in.block_h = GRID_H;
            srm_config.in.block_offset_x = 0;
            srm_config.in.block_offset_y = 0;
            srm_config.in.srm_cm = WATER_PPA_COLOR_MODE;

            srm_config.out.buffer = lcd_buf;
            srm_config.out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * WATER_BYTES_PER_PIXEL, _data_cache_line_size);
            srm_config.out.pic_w = BSP_LCD_H_RES;
            srm_config.out.pic_h = BSP_LCD_V_RES;
            srm_config.out.block_offset_x = 0;
            srm_config.out.block_offset_y = 0;
            srm_config.out.srm_cm = WATER_PPA_COLOR_MODE;

            srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
            srm_config.scale_x = static_cast<float>(BSP_LCD_H_RES) / GRID_W;
            srm_config.scale_y = static_cast<float>(BSP_LCD_V_RES) / GRID_H;
            srm_config.mirror_x = 0;
            srm_config.mirror_y = 0;
            srm_config.rgb_swap = 0;
            srm_config.byte_swap = 0;
            srm_config.mode = PPA_TRANS_MODE_BLOCKING;

            ret = ppa_do_scale_rotate_mirror(_ppa_srm_handle, &srm_config);
            if (ret == ESP_OK && _dummy_enabled) {
                ret = esp_lv_adapter_dummy_draw_blit(_display, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, lcd_buf, true);
            }

            ++fps_frame_count;
            {
                int64_t fps_now = esp_timer_get_time();
                int64_t fps_elapsed = fps_now - fps_window_start_us;
                if (fps_elapsed >= 1000000) {
                    float fps = fps_frame_count * 1000000.0f / static_cast<float>(fps_elapsed);
                    ESP_LOGI(ESP_UTILS_LOG_TAG, "FPS: %.1f", fps);
                    fps_frame_count = 0;
                    fps_window_start_us = fps_now;
                }
            }

            if (pollTouchAndExit()) {
                request_close = true;
                _sim_running = false;
            }

            int64_t now = esp_timer_get_time();
            int64_t elapsed = now - last_frame_us;
            constexpr int64_t TARGET_FRAME_US = 33000; // ~30 fps
            if (elapsed < TARGET_FRAME_US) {
                vTaskDelay(pdMS_TO_TICKS((TARGET_FRAME_US - elapsed) / 1000));
            }
            last_frame_us = esp_timer_get_time();
        }

        stopDummyDraw();

        _sim_running = false;
        _sim_task_handle = nullptr;

        if (_close_wait_task) {
            xTaskNotifyGive(_close_wait_task);
        }

        if (request_close) {
            notifyCoreClosed();
        }

        vTaskDelete(NULL);
    }

    void Water2D::simTaskEntry(void *arg)
    {
        static_cast<Water2D *>(arg)->simTask();
    }

} // namespace esp_brookesia::apps
