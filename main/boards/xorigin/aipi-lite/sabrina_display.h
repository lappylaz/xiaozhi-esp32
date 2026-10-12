#pragma once

#if CONFIG_SABRINA_VOICE
#include <cstdlib>
#include <cstring>
#include "display/lcd_display.h"
#include "sabrina_avatar.h"
#include "sabrina_clock.h"

// This 128 x 128 layout belongs to the Sabrina AiPi surface only. Other boards
// retain their normal LCD layout, emotion collection and idle-clock behavior.
class SabrinaAiPiDisplay final : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    ~SabrinaAiPiDisplay() override {
        DisplayLockGuard lock(this);
        if (emoji_box_)
            lv_anim_delete(emoji_box_, nullptr);
        if (clock_label_)
            lv_obj_delete(clock_label_);
    }

    void SetupUI() override {
        if (setup_ui_called_)
            return;
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
        auto screen = lv_display_get_screen_active(display_);
        // The board layout is independent of the selected upstream chat style.
        if (!emoji_box_) {
            emoji_box_ = lv_obj_create(screen);
            lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(emoji_box_, 0, 0);
            lv_obj_set_style_border_width(emoji_box_, 0, 0);
            lv_obj_set_parent(emoji_image_, emoji_box_);
        }
        if (content_)
            lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_parent(top_bar_, screen);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
        clock_label_ = lv_label_create(screen);
        lv_label_set_text(clock_label_, "--:--");
        lv_obj_set_width(clock_label_, 82);
        lv_obj_set_style_text_align(clock_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(clock_label_, LV_ALIGN_TOP_MID, 0, 0);

        portrait_.header.magic = LV_IMAGE_HEADER_MAGIC;
        portrait_.header.cf = LV_COLOR_FORMAT_ARGB8888;
        portrait_.header.w = 76;
        portrait_.header.h = 80;
        portrait_.header.stride = 76 * 4;
        portrait_.data_size = sizeof(kSabrinaAvatarPixels);
        portrait_.data = kSabrinaAvatarPixels;
        lv_image_set_src(emoji_image_, &portrait_);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(emoji_box_, 76, 80);
        lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, -7);
        lv_obj_center(emoji_image_);
        ApplyLayout();
    }

    void SetTheme(Theme* theme) override {
        SpiLcdDisplay::SetTheme(theme);
        if (clock_label_) {
            DisplayLockGuard lock(this);
            ApplyLayout();
        }
    }

    void SetStatus(const char* status) override {
        const char* text = status;
        if (strcmp(status, "Hold button to talk") == 0)
            text = "Hold to talk";
        else if (strcmp(status, "Connecting to Sabrina") == 0)
            text = "Connecting";
        else if (strcmp(status, "Sabrina is thinking") == 0)
            text = "Thinking";
        else if (strcmp(status, "Sabrina is speaking") == 0)
            text = "Speaking";
        LvglDisplay::SetStatus(text);
        DisplayLockGuard lock(this);
        error_detail_ = false;
        motion_requested_ = strcmp(text, "Listening") == 0 || strcmp(text, "Thinking") == 0 ||
                            strcmp(text, "Speaking") == 0 || strcmp(text, "Transcribing") == 0;
        UpdateMotion();
    }

    void SetEmotion(const char* emotion) override {
        // Keep the website portrait instead of switching back to upstream emoji.
        DisplayLockGuard lock(this);
        sleeping_ = strcmp(emotion, "sleepy") == 0;
        error_detail_ = strcmp(emotion, "cancel") == 0;
        UpdateMotion();
    }

    void SetChatMessage(const char* role, const char* content) override {
        DisplayLockGuard lock(this);
        // Voice-only surface: no transcript/task/email UI. Alert details still
        // get a readable bottom ticker after Application::Alert sets "cancel".
        if (error_detail_ && strcmp(role, "system") == 0 && content && *content)
            ShowNotification(content, 20000);
    }

    void ClearChatMessages() override {}

protected:
    void UpdateClock(bool) override {
        const time_t now = time(nullptr);
        tm local = {};
        if (!localtime_r(&now, &local) || local.tm_year < 2025 - 1900)
            return;
        char text[16];
        sabrina::FormatClock(local, text);
        DisplayLockGuard lock(this);
        if (clock_label_ && strcmp(lv_label_get_text(clock_label_), text) != 0)
            lv_label_set_text(clock_label_, text);
    }

private:
    lv_obj_t* clock_label_ = nullptr;
    lv_image_dsc_t portrait_ = {};
    bool sleeping_ = false, motion_requested_ = false, motion_running_ = false;
    bool error_detail_ = false;

    void ApplyLayout() {
        const auto background = lv_color_hex(0x0B1220);
        const auto white = lv_color_white();
        auto screen = lv_display_get_screen_active(display_);
        lv_obj_set_style_bg_color(screen, background, 0);
        lv_obj_set_style_bg_color(container_, background, 0);
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_height(top_bar_, 18);
        lv_obj_set_style_pad_all(top_bar_, 0, 0);
        lv_obj_set_style_bg_color(top_bar_, background, 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        for (auto* label : {network_label_, battery_label_, mute_label_, clock_label_})
            lv_obj_set_style_text_color(label, white, 0);

        // Bottom band stays opaque so the state cannot disappear into a photo.
        lv_obj_set_size(status_bar_, 128, 30);
        lv_obj_set_style_pad_all(status_bar_, 0, 0);
        lv_obj_set_style_bg_color(status_bar_, lv_color_hex(0x17263D), 0);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, 0);
        lv_obj_align(status_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_move_foreground(status_bar_);
        for (auto* label : {status_label_, notification_label_}) {
            lv_obj_set_width(label, 124);
            lv_obj_set_style_text_color(label, white, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        }
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(notification_label_, lv_anim_speed_clamped(40, 1000, 60000),
                                       LV_PART_MAIN);
        if (bottom_bar_)
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        // Preserve low-battery warning above, rather than over, the status band.
        lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -32);
    }

    void UpdateMotion() {
        const bool animate = motion_requested_ && !sleeping_ && !error_detail_;
        if (!emoji_box_ || animate == motion_running_)
            return;
        lv_anim_delete(emoji_box_, nullptr);
        lv_obj_set_style_translate_y(emoji_box_, 0, 0);
        motion_running_ = animate;
        if (!animate)
            return;
        // Two-pixel breathing motion reuses one image. No GIF decoder, extra
        // frame buffers, timer task, or microphone/amplitude sampling is needed.
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, emoji_box_);
        lv_anim_set_values(&animation, 0, -2);
        lv_anim_set_duration(&animation, 1300);
        lv_anim_set_reverse_duration(&animation, 1300);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&animation, [](void* object, int32_t value) {
            lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(object), value, 0);
        });
        lv_anim_start(&animation);
    }
};
#endif  // CONFIG_SABRINA_VOICE
