/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "driver/ppa.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include <cstdint>

namespace esp_brookesia::apps
{

    // Hugo Elias's 2D water ripple algorithm (see Note.txt). Simulated on a small grid, then
    // rendered (refraction + simple lighting over a checkerboard) and scaled up onto the panel
    // by PPA, following the same direct-framebuffer pattern as the Camera app.
    class Water2D : public systems::phone::App
    {
    public:
        static Water2D *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
        ~Water2D();

    protected:
        Water2D(bool use_status_bar, bool use_navigation_bar);

        bool run(void) override;
        bool back(void) override;
        bool close(void) override;
        bool init(void) override;
        bool deinit(void) override;
        bool pause(void) override;
        bool resume(void) override;

    private:
        static Water2D *_instance;

        // Simulation grid: downscaled from the panel resolution, upscaled back onto it by PPA.
        // 8 divides both 720 and 1280 exactly, so the PPA scale factor is an exact 8.0x with no
        // remainder; a non-exact scale left a couple of rows at the bottom un-written on some
        // frames, and since the LCD round-robins 3 physical framebuffers with different stale
        // content there, that showed up as flicker at the very bottom of the screen.
        static constexpr int GRID_DOWNSCALE = 8;
        static constexpr int GRID_W = BSP_LCD_H_RES / GRID_DOWNSCALE;
        static constexpr int GRID_H = BSP_LCD_V_RES / GRID_DOWNSCALE;
        static constexpr int GRID_CELLS = GRID_W * GRID_H;

        static constexpr int SIM_TASK_STACK_SIZE = 8 * 1024;
        static constexpr int SIM_TASK_PRIORITY = 8;
        // Touch held still this long counts as "done" and exits the app (dragging to make
        // ripples is the normal interaction, so exit needs a gesture that doesn't conflict).
        static constexpr int64_t LONG_PRESS_US = 1200 * 1000;
        static constexpr int TOUCH_MOVE_THRESHOLD = 12; // screen px

        lv_display_t *_display = nullptr;
        esp_lcd_panel_handle_t _display_panel = nullptr;
        ppa_client_handle_t _ppa_srm_handle = nullptr;
        esp_lcd_touch_handle_t _touch_handle = nullptr;

        TaskHandle_t _sim_task_handle = nullptr;
        TaskHandle_t _close_wait_task = nullptr;
        volatile bool _sim_running = false;
        bool _dummy_enabled = false;
        bool _lvgl_paused = false;

        bool _touch_active = false;
        bool _touch_moved = false;
        int64_t _touch_start_us = 0;
        uint16_t _touch_start_x = 0;
        uint16_t _touch_start_y = 0;

        size_t _data_cache_line_size = 64;
        void *_lcd_buffers[CONFIG_BSP_LCD_DPI_BUFFER_NUMS] = {};

        // Height field, ping-ponged each step; _height[_height_parity] always holds the latest result.
        int16_t *_height[2] = {nullptr, nullptr};
        int _height_parity = 0;
        uint16_t *_checker = nullptr;    // background texture, GRID_W x GRID_H, RGB565
        uint16_t *_render_buf = nullptr; // one rendered frame at grid resolution, RGB565, fed to PPA

        bool startSim();
        void requestStopSim();
        esp_err_t startDummyDraw();
        void stopDummyDraw();
        void allocateBuffers();
        void buildCheckerTexture();
        void simulateStep();
        void splash(int gx, int gy, int16_t amount);
        void renderFrame();
        bool pollTouchAndExit(); // also injects splashes at the touch point; returns true to exit
        void simTask();

        static void simTaskEntry(void *arg);
    };

} // namespace esp_brookesia::apps
