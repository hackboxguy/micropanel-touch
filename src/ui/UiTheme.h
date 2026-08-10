#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <lvgl.h>

namespace micropanel_touch::ui {

struct UiThemeColors {
    std::uint32_t background{};
    std::uint32_t surface{};
    std::uint32_t chrome{};
    std::uint32_t text{};
    std::uint32_t text_dim{};
    std::uint32_t accent{};
    std::uint32_t ok{};
    std::uint32_t warn{};
    std::uint32_t error{};
};

struct UiThemeShape {
    int radius{};
    int tile_radius{};
    int border_width{};
};

struct UiThemeFonts {
    const lv_font_t* body{};
    const lv_font_t* title{};
    const lv_font_t* small{};
};

struct UiThemeSkin {
    std::string name;
    UiThemeColors colors;
    UiThemeShape shape;
    UiThemeFonts fonts;
};

enum class UiThemeRole {
    Title,
    DimText,
    SuccessText,
    ErrorText,
    Tile,
};

class UiTheme {
public:
    explicit UiTheme(std::filesystem::path skin_directory);
    ~UiTheme();
    UiTheme(const UiTheme&) = delete;
    UiTheme& operator=(const UiTheme&) = delete;

    static std::optional<UiThemeSkin> load_skin(const std::filesystem::path& path,
                                                std::string* diagnostic);
    static lv_color_t to_lv_color(std::uint32_t color);
    static lv_color_t color_from_hex(const std::string& color);
    static void set_role(lv_obj_t* object, UiThemeRole role);

    bool activate(const std::string& requested_skin, lv_display_t* display,
                  std::string* diagnostic);
    const UiThemeSkin& active_skin() const;
    bool has_active_skin() const;

private:
    std::filesystem::path skin_path(const std::string& requested_skin) const;
    static void apply_callback(lv_theme_t* theme, lv_obj_t* object);

    std::filesystem::path skin_directory_;
    std::optional<UiThemeSkin> active_skin_;
    lv_theme_t* lv_theme_{nullptr};
    lv_display_t* display_{nullptr};
    static const UiThemeSkin* callback_skin_;
};

}  // namespace micropanel_touch::ui
