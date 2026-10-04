/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "Camera.hpp"
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
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "linux/videodev2.h"
#include "dl_image_define.hpp"
#include "human_face_detect.hpp"
#include "pedestrian_detect.hpp"

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Camera"

LV_IMG_DECLARE(img_app_camera);

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

// RGB565 colors used for the detection overlay
#define DETECT_COLOR_BOX        0xF800  // red
#define DETECT_COLOR_KEYPOINT   0x07E0  // green
#define DETECT_COLOR_FACE_TAG   0x07FF  // cyan
#define DETECT_COLOR_PED_TAG    0xFFE0  // yellow
#define DETECT_TAG_SIZE         24

// Rotation applied to the detector input. PPA's 90 is counter-clockwise and 270 is clockwise.
// Only 90 and 270 are supported, because the coordinate mapping in detectTask() assumes a 90-degree turn.
#ifndef DETECT_PPA_ROTATE_ANGLE
#define DETECT_PPA_ROTATE_ANGLE PPA_SRM_ROTATION_ANGLE_270
#endif

#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB565
#define CAMERA_VIDEO_FMT V4L2_PIX_FMT_RGB565
#define CAMERA_PPA_COLOR_MODE PPA_SRM_COLOR_MODE_RGB565
#define CAMERA_BYTES_PER_PIXEL 2
#elif CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#define CAMERA_VIDEO_FMT V4L2_PIX_FMT_RGB24
#define CAMERA_PPA_COLOR_MODE PPA_SRM_COLOR_MODE_RGB888
#define CAMERA_BYTES_PER_PIXEL 3
#else
#error "Unsupported BSP LCD color format"
#endif

// The OV5647 800x1280 sensor mode squeezes an off-center 2110x1448 window into a portrait frame, so on a
// portrait LCD the preview looks shifted and distorted. Prefer a landscape sensor mode (undistorted, centered)
// and let PPA crop + rotate it to the panel orientation instead.
#ifdef CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS
#define CAMERA_PREFERRED_CAPTURE_WIDTH  1280
#define CAMERA_PREFERRED_CAPTURE_HEIGHT 960
#else
#define CAMERA_PREFERRED_CAPTURE_WIDTH  BSP_LCD_H_RES
#define CAMERA_PREFERRED_CAPTURE_HEIGHT BSP_LCD_V_RES
#endif

// PPA rotation (counter-clockwise) and mirroring applied when the capture orientation differs from the panel's.
// Adjust these if the preview appears rotated the wrong way or mirrored.
#ifndef CAMERA_PPA_ROTATE_ANGLE
#define CAMERA_PPA_ROTATE_ANGLE PPA_SRM_ROTATION_ANGLE_90
#endif
#ifndef CAMERA_PPA_MIRROR_X
#define CAMERA_PPA_MIRROR_X 0
#endif
#ifndef CAMERA_PPA_MIRROR_Y
#define CAMERA_PPA_MIRROR_Y 0
#endif

// Fine tuning for lens/sensor misalignment (e.g. wide-angle modules whose optical axis is off-center).
// The shift is in source-frame pixels (before rotation) and moves the crop window; the zoom (>= 1.0)
// shrinks the crop window around its center so there is room to shift along both axes.
#ifndef CAMERA_CROP_ZOOM
#define CAMERA_CROP_ZOOM 1.0f
#endif
#ifndef CAMERA_CROP_SHIFT_X
#define CAMERA_CROP_SHIFT_X 100
#endif
#ifndef CAMERA_CROP_SHIFT_Y
#define CAMERA_CROP_SHIFT_Y 0
#endif

#ifdef CONFIG_CAMERA_OV5647_MIPI_RAW8_800x1280_50FPS
#define CAMERA_FALLBACK_CAPTURE_WIDTH  800
#define CAMERA_FALLBACK_CAPTURE_HEIGHT 1280
#else
#define CAMERA_FALLBACK_CAPTURE_WIDTH  BSP_LCD_H_RES
#define CAMERA_FALLBACK_CAPTURE_HEIGHT BSP_LCD_V_RES
#endif

namespace esp_brookesia::apps
{
    struct CoverCropConfig {
        uint32_t offset_x;
        uint32_t offset_y;
        uint32_t width;
        uint32_t height;
        float scale_x;
        float scale_y;
    };

    // Apply CAMERA_CROP_ZOOM / CAMERA_CROP_SHIFT_* to a centered cover crop, keeping the window inside the frame.
    static void applyCropAdjust(CoverCropConfig &crop, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h)
    {
        if (CAMERA_CROP_ZOOM <= 1.0f && CAMERA_CROP_SHIFT_X == 0 && CAMERA_CROP_SHIFT_Y == 0) {
            return;
        }

        const float zoom = CAMERA_CROP_ZOOM > 1.0f ? CAMERA_CROP_ZOOM : 1.0f;
        const uint32_t new_w = std::max<uint32_t>(static_cast<uint32_t>(crop.width / zoom), 1);
        const uint32_t new_h = std::max<uint32_t>(static_cast<uint32_t>(crop.height / zoom), 1);
        int32_t x = static_cast<int32_t>(crop.offset_x) + static_cast<int32_t>((crop.width - new_w) / 2) + CAMERA_CROP_SHIFT_X;
        int32_t y = static_cast<int32_t>(crop.offset_y) + static_cast<int32_t>((crop.height - new_h) / 2) + CAMERA_CROP_SHIFT_Y;
        x = std::clamp<int32_t>(x, 0, static_cast<int32_t>(src_w - new_w));
        y = std::clamp<int32_t>(y, 0, static_cast<int32_t>(src_h - new_h));

        crop.offset_x = x;
        crop.offset_y = y;
        crop.width = new_w;
        crop.height = new_h;
        crop.scale_x = static_cast<float>(dst_w) / static_cast<float>(new_w);
        crop.scale_y = static_cast<float>(dst_h) / static_cast<float>(new_h);
    }

    static CoverCropConfig computeCoverCrop(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h)
    {
        CoverCropConfig crop = {
            .offset_x = 0,
            .offset_y = 0,
            .width = src_w,
            .height = src_h,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
        };

        if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
            crop.width = crop.width ? crop.width : 1;
            crop.height = crop.height ? crop.height : 1;
            return crop;
        }

        if (static_cast<uint64_t>(src_w) * dst_h > static_cast<uint64_t>(src_h) * dst_w) {
            crop.width = static_cast<uint32_t>((static_cast<uint64_t>(src_h) * dst_w) / dst_h);
            crop.height = src_h;
        } else {
            crop.width = src_w;
            crop.height = static_cast<uint32_t>((static_cast<uint64_t>(src_w) * dst_h) / dst_w);
        }

        crop.width = crop.width ? crop.width : 1;
        crop.height = crop.height ? crop.height : 1;
        crop.offset_x = (src_w - crop.width) / 2;
        crop.offset_y = (src_h - crop.height) / 2;
        crop.scale_x = static_cast<float>(dst_w) / static_cast<float>(crop.width);
        crop.scale_y = static_cast<float>(dst_h) / static_cast<float>(crop.height);

        return crop;
    }

    // Draw helpers for the RGB565 LCD frame. They take the frame size explicitly, so the overlay follows the panel.
    static inline void putPixel(uint16_t *buf, int w, int h, int x, int y, uint16_t color)
    {
        if (x >= 0 && x < w && y >= 0 && y < h) {
            buf[y * w + x] = color;
        }
    }

    static void drawBox(uint16_t *buf, int w, int h, int x1, int y1, int x2, int y2, uint16_t color, int thickness)
    {
        for (int t = 0; t < thickness; ++t) {
            for (int x = x1; x <= x2; ++x) {
                putPixel(buf, w, h, x, y1 + t, color);
                putPixel(buf, w, h, x, y2 - t, color);
            }
            for (int y = y1; y <= y2; ++y) {
                putPixel(buf, w, h, x1 + t, y, color);
                putPixel(buf, w, h, x2 - t, y, color);
            }
        }
    }

    static void fillSquare(uint16_t *buf, int w, int h, int x, int y, int size, uint16_t color)
    {
        for (int dy = 0; dy < size; ++dy) {
            for (int dx = 0; dx < size; ++dx) {
                putPixel(buf, w, h, x + dx, y + dy, color);
            }
        }
    }

    static void fillDot(uint16_t *buf, int w, int h, int cx, int cy, int radius, uint16_t color)
    {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                putPixel(buf, w, h, cx + dx, cy + dy, color);
            }
        }
    }

    Camera *Camera::_instance = nullptr;

    Camera *Camera::requestInstance(bool use_status_bar, bool use_navigation_bar)
    {
        if (_instance == nullptr)
        {
            _instance = new Camera(use_status_bar, use_navigation_bar);
        }
        return _instance;
    }

    Camera::Camera(bool use_status_bar, bool use_navigation_bar) : App("Camera", &img_app_camera, true, use_status_bar, use_navigation_bar)
    {
    }

    Camera::~Camera()
    {
        close();

        if (_detect_task_handle) {
            vTaskDelete(_detect_task_handle);
            _detect_task_handle = nullptr;
        }
        if (_detect_input) {
            heap_caps_free(_detect_input);
            _detect_input = nullptr;
        }
        if (_detect_results_mutex) {
            vSemaphoreDelete(_detect_results_mutex);
            _detect_results_mutex = nullptr;
        }
    }

    bool Camera::run(void)
    {
        ESP_UTILS_LOGD("Run");

        bsp_display_lock(-1);
        lv_obj_clean(lv_scr_act());
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
        _status_label = lv_label_create(lv_scr_act());
        lv_label_set_text(_status_label, "Camera");
        lv_obj_set_width(_status_label, BSP_LCD_H_RES);
        lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_30, LV_PART_MAIN);
        lv_obj_set_style_text_color(_status_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_align(_status_label, LV_ALIGN_CENTER, 0, 0);
        bsp_display_unlock();

        if (!startPreview()) {
            bsp_display_lock(-1);
            if (_status_label) {
                lv_label_set_text(_status_label, "camera error");
            }
            bsp_display_unlock();
        }

        return true;
    }

    bool Camera::back(void)
    {
        ESP_UTILS_LOGD("Back");
        requestStopPreview();
        ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
        return true;
    }

    bool Camera::close()
    {
        ESP_UTILS_LOGD("Close");
        requestStopPreview();

        bsp_display_lock(-1);
        if (_status_label) {
            lv_obj_del(_status_label);
            _status_label = nullptr;
        }
        bsp_display_unlock();
        return true;
    }

    bool Camera::init()
    {
        ESP_UTILS_LOGD("Init");
        return true;
    }

    bool Camera::deinit()
    {
        ESP_UTILS_LOGD("Deinit");
        return true;
    }

    bool Camera::pause()
    {
        ESP_UTILS_LOGD("Pause");
        requestStopPreview();
        return true;
    }

    bool Camera::resume()
    {
        ESP_UTILS_LOGD("Resume");
        return true;
    }

    bool Camera::startPreview()
    {
        if (_preview_task_handle) {
            return true;
        }

        // The detect task lives for the whole app lifetime; it only runs inference while detection is enabled.
        if (_detect_results_mutex == nullptr) {
            _detect_results_mutex = xSemaphoreCreateMutex();
            if (_detect_results_mutex == nullptr) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "Create detect mutex failed");
                return false;
            }
        }
        if (_detect_task_handle == nullptr) {
            BaseType_t detect_ret = xTaskCreatePinnedToCore(
                detectTaskEntry,
                "camera_detect",
                DETECT_TASK_STACK_SIZE,
                this,
                DETECT_TASK_PRIORITY,
                &_detect_task_handle,
                1);
            if (detect_ret != pdPASS) {
                _detect_task_handle = nullptr;
                ESP_LOGE(ESP_UTILS_LOG_TAG, "Create camera detect task failed");
                return false;
            }
        }
        _detect_running = true;

        _preview_running = true;
        BaseType_t ret = xTaskCreatePinnedToCore(
            previewTaskEntry,
            "camera_preview",
            PREVIEW_TASK_STACK_SIZE,
            this,
            PREVIEW_TASK_PRIORITY,
            &_preview_task_handle,
            0);

        if (ret != pdPASS) {
            _preview_running = false;
            _preview_task_handle = nullptr;
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Create camera preview task failed");
            return false;
        }

        return true;
    }

    void Camera::requestStopPreview()
    {
        if (!_preview_task_handle) {
            stopDummyPreview();
            releaseCameraBuffers();
            return;
        }

        if (xTaskGetCurrentTaskHandle() == _preview_task_handle) {
            _preview_running = false;
            return;
        }

        _preview_running = false;
        _close_wait_task = xTaskGetCurrentTaskHandle();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
        _close_wait_task = nullptr;
    }

    esp_err_t Camera::initVideoDriver()
    {
        if (_video_driver_initialized) {
            return ESP_OK;
        }

        esp_video_init_csi_config_t csi_config[] = {
            {
                .sccb_config = {
                    .init_sccb = false,
                    .i2c_handle = bsp_i2c_get_handle(),
                    .freq = CONFIG_BSP_I2C_CLK_SPEED_HZ,
                },
                .reset_pin = GPIO_NUM_NC,
                .pwdn_pin = GPIO_NUM_NC,
            },
        };

        esp_video_init_config_t cam_config = {
            .csi = csi_config,
        };

        esp_err_t ret = esp_video_init(&cam_config);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            _video_driver_initialized = true;
            return ESP_OK;
        }

        ESP_LOGE(ESP_UTILS_LOG_TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_err_t Camera::openVideoDevice()
    {
        if (_video_fd >= 0) {
            return ESP_OK;
        }

        _video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
        if (_video_fd < 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Open %s failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
            return ESP_FAIL;
        }

        struct v4l2_capability capability = {};
        if (ioctl(_video_fd, VIDIOC_QUERYCAP, &capability) != 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_QUERYCAP failed");
            ::close(_video_fd);
            _video_fd = -1;
            return ESP_FAIL;
        }

        struct v4l2_format format = {};
        auto set_capture_format = [&](uint32_t width, uint32_t height) -> bool {
            memset(&format, 0, sizeof(format));
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            format.fmt.pix.width = width;
            format.fmt.pix.height = height;
            format.fmt.pix.pixelformat = CAMERA_VIDEO_FMT;

            return ioctl(_video_fd, VIDIOC_S_FMT, &format) == 0;
        };

        // Prefer the panel size when the sensor supports it. Some camera modes are
        // fixed by Kconfig, so fall back to the known native capture mode and adapt
        // the preview to the LCD with PPA.
        if (!set_capture_format(CAMERA_PREFERRED_CAPTURE_WIDTH, CAMERA_PREFERRED_CAPTURE_HEIGHT)) {
            ESP_LOGW(
                ESP_UTILS_LOG_TAG,
                "VIDIOC_S_FMT %" PRIu32 "x%" PRIu32 " failed, trying fallback %" PRIu32 "x%" PRIu32,
                static_cast<uint32_t>(CAMERA_PREFERRED_CAPTURE_WIDTH),
                static_cast<uint32_t>(CAMERA_PREFERRED_CAPTURE_HEIGHT),
                static_cast<uint32_t>(CAMERA_FALLBACK_CAPTURE_WIDTH),
                static_cast<uint32_t>(CAMERA_FALLBACK_CAPTURE_HEIGHT)
            );
            if (!set_capture_format(CAMERA_FALLBACK_CAPTURE_WIDTH, CAMERA_FALLBACK_CAPTURE_HEIGHT)) {
                ESP_LOGE(
                    ESP_UTILS_LOG_TAG,
                    "VIDIOC_S_FMT fallback %" PRIu32 "x%" PRIu32 " failed",
                    static_cast<uint32_t>(CAMERA_FALLBACK_CAPTURE_WIDTH),
                    static_cast<uint32_t>(CAMERA_FALLBACK_CAPTURE_HEIGHT)
                );
                ::close(_video_fd);
                _video_fd = -1;
                return ESP_FAIL;
            }
        }

        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(_video_fd, VIDIOC_G_FMT, &format) != 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_G_FMT failed");
            ::close(_video_fd);
            _video_fd = -1;
            return ESP_FAIL;
        }

        _camera_width = format.fmt.pix.width;
        _camera_height = format.fmt.pix.height;
        _camera_buffer_size = _camera_width * _camera_height * CAMERA_BYTES_PER_PIXEL;
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Camera format: %" PRIu32 "x%" PRIu32, _camera_width, _camera_height);

        return ESP_OK;
    }

    esp_err_t Camera::setupCameraBuffers()
    {
#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
        _data_cache_line_size = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
#else
        _data_cache_line_size = 64;
#endif

        // Frame copy for the detector, sized like one LCD frame. Allocated once and kept, because the
        // detect task may still be reading it when the preview stops.
        if (_detect_input == nullptr) {
            size_t detect_bytes = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * CAMERA_BYTES_PER_PIXEL, _data_cache_line_size);
            _detect_input = static_cast<uint16_t *>(heap_caps_aligned_calloc(_data_cache_line_size, 1, detect_bytes, MALLOC_CAP_SPIRAM));
            ESP_RETURN_ON_FALSE(_detect_input, ESP_ERR_NO_MEM, ESP_UTILS_LOG_TAG, "Allocate detect frame buffer failed");
        }

        struct v4l2_requestbuffers req = {};
        req.count = CAMERA_BUFFER_COUNT;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (ioctl(_video_fd, VIDIOC_REQBUFS, &req) != 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_REQBUFS failed");
            return ESP_FAIL;
        }

        for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;
            buf.index = i;

            if (ioctl(_video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_QUERYBUF %d failed", i);
                return ESP_FAIL;
            }

            size_t alloc_size = buf.length ? buf.length : _camera_buffer_size;
            _camera_buffer_lengths[i] = alloc_size;
            _camera_buffers[i] = heap_caps_aligned_calloc(_data_cache_line_size, 1, alloc_size, MALLOC_CAP_SPIRAM);
            if (!_camera_buffers[i]) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "Camera buffer %d allocation failed", i);
                return ESP_ERR_NO_MEM;
            }

            buf.m.userptr = (unsigned long)_camera_buffers[i];
            buf.length = alloc_size;

            if (ioctl(_video_fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_QBUF %d failed", i);
                return ESP_FAIL;
            }
        }

        return ESP_OK;
    }

    esp_err_t Camera::startDummyPreview()
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
#error "Camera preview needs at least two LCD frame buffers"
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

    void Camera::stopDummyPreview()
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

        _touch_active = false;
    }

    void Camera::releaseCameraBuffers()
    {
        for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
            if (_camera_buffers[i]) {
                heap_caps_free(_camera_buffers[i]);
                _camera_buffers[i] = nullptr;
            }
            _camera_buffer_lengths[i] = 0;
        }

        if (_video_fd >= 0) {
            ::close(_video_fd);
            _video_fd = -1;
        }
    }

    Camera::TouchGesture Camera::pollTouch()
    {
        if (!_touch_handle) {
            return TouchGesture::None;
        }

        esp_lcd_touch_read_data(_touch_handle);

        esp_lcd_touch_point_data_t points[1] = {};
        uint8_t point_num = 0;
        esp_err_t ret = esp_lcd_touch_get_data(_touch_handle, points, &point_num, 1);

        if (ret != ESP_OK || point_num == 0) {
            // Finger lifted: a short touch without much movement counts as a tap
            TouchGesture gesture = (_touch_active && _touch_tap_candidate) ? TouchGesture::Tap : TouchGesture::None;
            _touch_active = false;
            return gesture;
        }

        if (!_touch_active) {
            _touch_active = true;
            _touch_tap_candidate = true;
            _touch_start_x = points[0].x;
            _touch_start_y = points[0].y;
            return TouchGesture::None;
        }

        int dx = abs((int)points[0].x - (int)_touch_start_x);
        int dy = abs((int)points[0].y - (int)_touch_start_y);
        if (dx > SWIPE_EXIT_THRESHOLD || dy > SWIPE_EXIT_THRESHOLD) {
            _touch_tap_candidate = false;
            return TouchGesture::SwipeExit;
        }
        if (dx > TAP_MOVE_THRESHOLD || dy > TAP_MOVE_THRESHOLD) {
            _touch_tap_candidate = false;
        }
        return TouchGesture::None;
    }

    // Normal -> Face -> Pedestrian -> Normal
    void Camera::cycleDetectMode()
    {
        DetectMode next = DetectMode::None;
        if (_detect_mode == DetectMode::None) {
            next = DetectMode::Face;
        } else if (_detect_mode == DetectMode::Face) {
            next = DetectMode::Pedestrian;
        }
        _detect_mode = next;

        xSemaphoreTake(_detect_results_mutex, portMAX_DELAY);
        _detect_objects.clear();
        xSemaphoreGive(_detect_results_mutex);

        const char *names[] = {"Normal", "Face detect", "Pedestrian detect"};
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Mode: %s", names[static_cast<int>(next)]);
    }

    // Rotate the finished LCD frame for the detector (PPA, counter-clockwise by default), so an upright
    // face or pedestrian in portrait view is upright in the detector input. Runs on the preview task;
    // skipped while the detector is busy.
    void Camera::submitFrameForDetect(uint16_t *lcd_buf, uint32_t width, uint32_t height)
    {
        if (!_detect_idle || _detect_mode == DetectMode::None || _detect_input == nullptr || _ppa_srm_handle == nullptr) {
            return;
        }

        // The rotated frame swaps width and height, so the buffer size stays the same
        size_t bytes = ALIGN_UP(width * height * CAMERA_BYTES_PER_PIXEL, _data_cache_line_size);

        ppa_srm_oper_config_t srm_config = {};
        srm_config.in.buffer = lcd_buf;
        srm_config.in.pic_w = width;
        srm_config.in.pic_h = height;
        srm_config.in.block_w = width;
        srm_config.in.block_h = height;
        srm_config.in.block_offset_x = 0;
        srm_config.in.block_offset_y = 0;
        srm_config.in.srm_cm = CAMERA_PPA_COLOR_MODE;

        srm_config.out.buffer = _detect_input;
        srm_config.out.buffer_size = bytes;
        srm_config.out.pic_w = height;
        srm_config.out.pic_h = width;
        srm_config.out.block_offset_x = 0;
        srm_config.out.block_offset_y = 0;
        srm_config.out.srm_cm = CAMERA_PPA_COLOR_MODE;

        srm_config.rotation_angle = DETECT_PPA_ROTATE_ANGLE;
        srm_config.scale_x = 1.0f;
        srm_config.scale_y = 1.0f;
        srm_config.mirror_x = 0;
        srm_config.mirror_y = 0;
        srm_config.rgb_swap = 0;
        srm_config.byte_swap = 0;
        srm_config.mode = PPA_TRANS_MODE_BLOCKING;

        if (ppa_do_scale_rotate_mirror(_ppa_srm_handle, &srm_config) != ESP_OK) {
            return;
        }

        _detect_idle = false;
        xTaskNotifyGive(_detect_task_handle);
    }

    // Draw the mode tag and the latest results onto the LCD frame before it is blitted.
    void Camera::drawDetectResults(uint16_t *lcd_buf, uint32_t width, uint32_t height)
    {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);

        if (_detect_mode == DetectMode::None) {
            return;
        }

        uint16_t tag_color = (_detect_mode == DetectMode::Face) ? DETECT_COLOR_FACE_TAG : DETECT_COLOR_PED_TAG;
        fillSquare(lcd_buf, w, h, 8, 8, DETECT_TAG_SIZE, tag_color);

        xSemaphoreTake(_detect_results_mutex, portMAX_DELAY);
        for (const auto &obj : _detect_objects) {
            drawBox(lcd_buf, w, h, obj.x1, obj.y1, obj.x2, obj.y2, DETECT_COLOR_BOX, 3);
            for (size_t i = 0; i + 1 < obj.keypoints.size(); i += 2) {
                fillDot(lcd_buf, w, h, obj.keypoints[i], obj.keypoints[i + 1], 3, DETECT_COLOR_KEYPOINT);
            }
        }
        xSemaphoreGive(_detect_results_mutex);
    }

    esp_err_t Camera::handleFrame()
    {
        struct v4l2_buffer v4l2_buf = {};
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_USERPTR;

        if (ioctl(_video_fd, VIDIOC_DQBUF, &v4l2_buf) != 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_DQBUF failed");
            return ESP_FAIL;
        }

        uint8_t buffer_index = v4l2_buf.index % CONFIG_BSP_LCD_DPI_BUFFER_NUMS;
        const uint32_t display_w = BSP_LCD_H_RES;
        const uint32_t display_h = BSP_LCD_V_RES;
        // Rotate by 90 degrees when the camera frame and the panel have different orientations.
        const bool rotate = (_camera_width > _camera_height) != (display_w > display_h);
        const uint32_t fit_w = rotate ? display_h : display_w;
        const uint32_t fit_h = rotate ? display_w : display_h;
        CoverCropConfig crop = computeCoverCrop(_camera_width, _camera_height, fit_w, fit_h);
        applyCropAdjust(crop, _camera_width, _camera_height, fit_w, fit_h);

        ppa_srm_oper_config_t srm_config = {};
        srm_config.in.buffer = _camera_buffers[v4l2_buf.index];
        srm_config.in.pic_w = _camera_width;
        srm_config.in.pic_h = _camera_height;
        srm_config.in.block_w = crop.width;
        srm_config.in.block_h = crop.height;
        srm_config.in.block_offset_x = crop.offset_x;
        srm_config.in.block_offset_y = crop.offset_y;
        srm_config.in.srm_cm = CAMERA_PPA_COLOR_MODE;

        srm_config.out.buffer = _lcd_buffers[buffer_index];
        srm_config.out.buffer_size = ALIGN_UP(display_w * display_h * CAMERA_BYTES_PER_PIXEL, _data_cache_line_size);
        srm_config.out.pic_w = display_w;
        srm_config.out.pic_h = display_h;
        srm_config.out.block_offset_x = 0;
        srm_config.out.block_offset_y = 0;
        srm_config.out.srm_cm = CAMERA_PPA_COLOR_MODE;

        srm_config.rotation_angle = rotate ? CAMERA_PPA_ROTATE_ANGLE : PPA_SRM_ROTATION_ANGLE_0;
        srm_config.scale_x = crop.scale_x;
        srm_config.scale_y = crop.scale_y;
        srm_config.mirror_x = CAMERA_PPA_MIRROR_X;
        srm_config.mirror_y = CAMERA_PPA_MIRROR_Y;
        srm_config.rgb_swap = 0;
        srm_config.byte_swap = 0;
        srm_config.mode = PPA_TRANS_MODE_BLOCKING;

        esp_err_t ret = ppa_do_scale_rotate_mirror(_ppa_srm_handle, &srm_config);
        if (ret == ESP_OK && _detect_mode != DetectMode::None) {
            uint16_t *lcd_buf = static_cast<uint16_t *>(_lcd_buffers[buffer_index]);
            // Copy first, so the overlay drawn below is not fed back to the detector
            submitFrameForDetect(lcd_buf, display_w, display_h);
            drawDetectResults(lcd_buf, display_w, display_h);
            esp_cache_msync(lcd_buf, ALIGN_UP(display_w * display_h * CAMERA_BYTES_PER_PIXEL, _data_cache_line_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
        if (ret == ESP_OK && _dummy_enabled) {
            ret = esp_lv_adapter_dummy_draw_blit(
                _display,
                0,
                0,
                display_w,
                display_h,
                _lcd_buffers[buffer_index],
                true);
        }

        v4l2_buf.m.userptr = (unsigned long)_camera_buffers[v4l2_buf.index];
        v4l2_buf.length = _camera_buffer_lengths[v4l2_buf.index];
        if (ioctl(_video_fd, VIDIOC_QBUF, &v4l2_buf) != 0) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_QBUF failed");
            return ESP_FAIL;
        }

        return ret;
    }

    void Camera::previewTask()
    {
        bool request_close = false;
        esp_err_t ret = initVideoDriver();
        if (ret == ESP_OK) {
            ret = openVideoDevice();
        }
        if (ret == ESP_OK) {
            ret = setupCameraBuffers();
        }
        if (ret == ESP_OK) {
            ret = startDummyPreview();
        }

        if (ret == ESP_OK) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(_video_fd, VIDIOC_STREAMON, &type) != 0) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "VIDIOC_STREAMON failed");
                ret = ESP_FAIL;
            }
        }

        while (_preview_running && ret == ESP_OK) {
            ret = handleFrame();
            TouchGesture gesture = pollTouch();
            if (gesture == TouchGesture::SwipeExit) {
                request_close = true;
                _preview_running = false;
            } else if (gesture == TouchGesture::Tap) {
                cycleDetectMode();
            }
        }

        if (_video_fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(_video_fd, VIDIOC_STREAMOFF, &type);
        }

        stopDummyPreview();
        releaseCameraBuffers();

        // Stop detection; the detect task releases its models when it wakes up
        _detect_mode = DetectMode::None;
        _detect_running = false;
        if (_detect_task_handle) {
            xTaskNotifyGive(_detect_task_handle);
        }

        _preview_running = false;
        _preview_task_handle = nullptr;

        if (_close_wait_task) {
            xTaskNotifyGive(_close_wait_task);
        }

        if (request_close) {
            notifyCoreClosed();
        }

        vTaskDelete(NULL);
    }

    void Camera::previewTaskEntry(void *arg)
    {
        static_cast<Camera *>(arg)->previewTask();
    }

    // Runs inference on frames handed over by the preview task. Blocks on a notification, so it uses no CPU while idle.
    void Camera::detectTask()
    {
        const size_t detect_bytes = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * CAMERA_BYTES_PER_PIXEL, _data_cache_line_size);

        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            if (!_detect_running) {
                releaseDetectors();
                _detect_idle = true;
                continue;
            }

            DetectMode mode = _detect_mode;
            if (mode == DetectMode::None || _detect_input == nullptr) {
                _detect_idle = true;
                continue;
            }

            // The preview task wrote this frame on another core, so drop stale cache lines before reading it
            esp_cache_msync(_detect_input, detect_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

            dl::image::img_t img = {};
            img.data = _detect_input;
            // The detector frame is rotated, so its width is the panel height
            img.width = BSP_LCD_V_RES;
            img.height = BSP_LCD_H_RES;
            img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

            // Map a point from the rotated detector frame back to the portrait panel frame
            auto toPanel = [](int xr, int yr, int &x, int &y) {
                if (DETECT_PPA_ROTATE_ANGLE == PPA_SRM_ROTATION_ANGLE_90) {
                    x = BSP_LCD_H_RES - 1 - yr;
                    y = xr;
                } else {
                    x = yr;
                    y = BSP_LCD_V_RES - 1 - xr;
                }
            };

            std::vector<DetectedObject> objects;
            auto collect = [&objects, &toPanel](const std::list<dl::detect::result_t> &results, bool with_keypoints) {
                for (const auto &res : results) {
                    if (res.box.size() < 4) {
                        continue;
                    }
                    int ax, ay, bx, by;
                    toPanel(res.box[0], res.box[1], ax, ay);
                    toPanel(res.box[2], res.box[3], bx, by);

                    DetectedObject obj;
                    obj.x1 = std::min(ax, bx);
                    obj.y1 = std::min(ay, by);
                    obj.x2 = std::max(ax, bx);
                    obj.y2 = std::max(ay, by);
                    if (with_keypoints && res.keypoint.size() >= 10) {
                        for (size_t i = 0; i < 10; i += 2) {
                            int px, py;
                            toPanel(res.keypoint[i], res.keypoint[i + 1], px, py);
                            obj.keypoints.push_back(px);
                            obj.keypoints.push_back(py);
                        }
                    }
                    objects.push_back(std::move(obj));
                }
            };

            int64_t start_us = esp_timer_get_time();
            if (mode == DetectMode::Face) {
                if (_face_detect == nullptr) {
                    _face_detect = new HumanFaceDetect();
                }
                collect(_face_detect->run(img), true);
            } else {
                if (_pedestrian_detect == nullptr) {
                    _pedestrian_detect = new PedestrianDetect();
                }
                collect(_pedestrian_detect->run(img), false);
            }
            ESP_LOGI(ESP_UTILS_LOG_TAG, "%s detect: %" PRId64 " ms, %d objects",
                     mode == DetectMode::Face ? "Face" : "Pedestrian",
                     (esp_timer_get_time() - start_us) / 1000,
                     static_cast<int>(objects.size()));

            xSemaphoreTake(_detect_results_mutex, portMAX_DELAY);
            _detect_objects = std::move(objects);
            xSemaphoreGive(_detect_results_mutex);

            _detect_idle = true;
        }
    }

    void Camera::detectTaskEntry(void *arg)
    {
        static_cast<Camera *>(arg)->detectTask();
    }

    void Camera::releaseDetectors()
    {
        if (_face_detect) {
            delete _face_detect;
            _face_detect = nullptr;
        }
        if (_pedestrian_detect) {
            delete _pedestrian_detect;
            _pedestrian_detect = nullptr;
        }

        xSemaphoreTake(_detect_results_mutex, portMAX_DELAY);
        _detect_objects.clear();
        xSemaphoreGive(_detect_results_mutex);
    }

} // namespace esp_brookesia::apps
