#include "ui/UiTheme.h"

#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr lv_state_t kTitleRole = LV_STATE_USER_1;
constexpr lv_state_t kDimTextRole = LV_STATE_USER_2;
constexpr lv_state_t kSuccessTextRole = LV_STATE_USER_3;
constexpr lv_state_t kErrorTextRole = LV_STATE_USER_4;
constexpr lv_state_t kTextRoles = static_cast<lv_state_t>(kTitleRole | kDimTextRole |
                                                           kSuccessTextRole | kErrorTextRole);

std::uint32_t parse_hex_color(const std::string& color, const std::string& field) {
    if (color.size() != 7U || color.front() != '#') {
        throw std::runtime_error(field + " must be a #RRGGBB value");
    }

    std::uint32_t value = 0;
    for (std::size_t index = 1; index < color.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(color[index]);
        if (std::isxdigit(character) == 0) {
            throw std::runtime_error(field + " must be a #RRGGBB value");
        }
        value *= 16U;
        if (character >= '0' && character <= '9') {
            value += static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value += static_cast<std::uint32_t>(character - 'a' + 10U);
        } else {
            value += static_cast<std::uint32_t>(character - 'A' + 10U);
        }
    }
    return value;
}

const lv_font_t* parse_font(const std::string& name, const std::string& field) {
    if (name == "montserrat_12") {
        return &lv_font_montserrat_12;
    }
    if (name == "montserrat_16") {
        return &lv_font_montserrat_16;
    }
    if (name == "montserrat_20") {
        return &lv_font_montserrat_20;
    }
    throw std::runtime_error(field + " must name an enabled built-in font");
}

int parse_dimension(const nlohmann::json& value, const std::string& field,
                    int minimum, int maximum) {
    if (!value.is_number_integer()) {
        throw std::runtime_error(field + " must be an integer");
    }
    const int result = value.get<int>();
    if (result < minimum || result > maximum) {
        throw std::runtime_error(field + " is outside the supported range");
    }
    return result;
}

const nlohmann::json& required_object(const nlohmann::json& root, const char* key) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw std::runtime_error(std::string("skin has no ") + key + " object");
    }
    return root.at(key);
}

std::uint32_t required_color(const nlohmann::json& colors, const char* key) {
    if (!colors.contains(key)) {
        throw std::runtime_error(std::string("skin colors has no ") + key);
    }
    return parse_hex_color(colors.at(key).get<std::string>(), std::string("skin colors.") + key);
}

}  // namespace

const UiThemeSkin* UiTheme::callback_skin_ = nullptr;

UiTheme::UiTheme(std::filesystem::path skin_directory)
    : skin_directory_(std::move(skin_directory)) {}

UiTheme::~UiTheme() {
    if (display_ != nullptr && lv_display_get_theme(display_) == lv_theme_) {
        lv_display_set_theme(display_, nullptr);
    }
    if (lv_theme_ != nullptr) {
        lv_theme_delete(lv_theme_);
    }
    if (callback_skin_ == (active_skin_.has_value() ? &*active_skin_ : nullptr)) {
        callback_skin_ = nullptr;
    }
}

std::optional<UiThemeSkin> UiTheme::load_skin(const std::filesystem::path& path,
                                               std::string* diagnostic) {
    try {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("Cannot open " + path.string());
        }
        const nlohmann::json root = nlohmann::json::parse(stream);
        UiThemeSkin skin;
        skin.name = root.at("name").get<std::string>();
        if (skin.name.empty()) {
            throw std::runtime_error("skin name cannot be empty");
        }

        const nlohmann::json& colors = required_object(root, "colors");
        skin.colors = {
            required_color(colors, "bg"),
            required_color(colors, "surface"),
            required_color(colors, "chrome"),
            required_color(colors, "text"),
            required_color(colors, "text_dim"),
            required_color(colors, "accent"),
            required_color(colors, "ok"),
            required_color(colors, "warn"),
            required_color(colors, "error"),
        };

        const nlohmann::json& shape = required_object(root, "shape");
        skin.shape = {
            parse_dimension(shape.at("radius"), "skin shape.radius", 0, 32),
            parse_dimension(shape.at("tile_radius"), "skin shape.tile_radius", 0, 32),
            parse_dimension(shape.at("border_width"), "skin shape.border_width", 0, 8),
        };

        const nlohmann::json& type = required_object(root, "type");
        skin.fonts = {
            parse_font(type.at("font_body").get<std::string>(), "skin type.font_body"),
            parse_font(type.at("font_title").get<std::string>(), "skin type.font_title"),
            parse_font(type.at("font_small").get<std::string>(), "skin type.font_small"),
        };
        return skin;
    } catch (const std::exception& error) {
        if (diagnostic != nullptr) {
            *diagnostic = error.what();
        }
        return std::nullopt;
    }
}

lv_color_t UiTheme::to_lv_color(std::uint32_t color) {
    return lv_color_hex(color);
}

lv_color_t UiTheme::color_from_hex(const std::string& color) {
    return to_lv_color(parse_hex_color(color, "accent color"));
}

void UiTheme::set_role(lv_obj_t* object, UiThemeRole role) {
    if (object == nullptr) {
        return;
    }

    switch (role) {
    case UiThemeRole::Title:
        lv_obj_remove_state(object, kTextRoles);
        lv_obj_add_state(object, kTitleRole);
        break;
    case UiThemeRole::DimText:
        lv_obj_remove_state(object, kTextRoles);
        lv_obj_add_state(object, kDimTextRole);
        break;
    case UiThemeRole::SuccessText:
        lv_obj_remove_state(object, kTextRoles);
        lv_obj_add_state(object, kSuccessTextRole);
        break;
    case UiThemeRole::ErrorText:
        lv_obj_remove_state(object, kTextRoles);
        lv_obj_add_state(object, kErrorTextRole);
        break;
    }
}

void UiTheme::apply_tile_variant(lv_obj_t* object) const {
    if (object == nullptr || !active_skin_.has_value()) {
        return;
    }
    lv_obj_set_style_radius(object, active_skin_->shape.tile_radius, 0);
}

bool UiTheme::activate(const std::string& requested_skin, lv_display_t* display,
                       std::string* diagnostic) {
    if (display == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "no LVGL display is available";
        }
        return false;
    }

    const auto skin = load_skin(skin_path(requested_skin), diagnostic);
    if (!skin.has_value()) {
        return false;
    }

    lv_theme_t* const next_theme = lv_theme_create();
    if (next_theme == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "unable to allocate LVGL theme";
        }
        return false;
    }

    active_skin_ = *skin;
    callback_skin_ = &*active_skin_;
    lv_theme_set_apply_cb(next_theme, apply_callback);
    lv_theme_t* const previous_theme = lv_theme_;
    lv_theme_ = next_theme;
    display_ = display;
    lv_display_set_theme(display_, lv_theme_);
    // lv_theme_apply() re-themes this object only, not its existing subtree.
    // Live selection in the starter UI immediately rebuilds its current
    // screen; a future persistent chrome bar must be rebuilt too, or use a
    // recursive re-apply path after activating a skin.
    lv_theme_apply(lv_screen_active());
    if (previous_theme != nullptr) {
        lv_theme_delete(previous_theme);
    }
    return true;
}

const UiThemeSkin& UiTheme::active_skin() const {
    return *active_skin_;
}

bool UiTheme::has_active_skin() const {
    return active_skin_.has_value();
}

std::filesystem::path UiTheme::skin_path(const std::string& requested_skin) const {
    const std::filesystem::path requested(requested_skin);
    if (requested.has_parent_path() || requested.extension() == ".json") {
        return requested;
    }
    return skin_directory_ / (requested_skin + ".json");
}

void UiTheme::apply_callback(lv_theme_t*, lv_obj_t* object) {
    if (callback_skin_ == nullptr) {
        return;
    }
    const UiThemeSkin& skin = *callback_skin_;

    if (lv_obj_get_parent(object) == nullptr) {
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.background), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text), 0);
        lv_obj_set_style_text_font(object, skin.fonts.body, 0);
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.surface), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.chrome), LV_STATE_PRESSED);
        lv_obj_set_style_radius(object, skin.shape.radius, 0);
        lv_obj_set_style_border_width(object, skin.shape.border_width, 0);
        lv_obj_set_style_border_color(object, to_lv_color(skin.colors.accent), 0);
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text), 0);
        lv_obj_set_style_text_font(object, skin.fonts.body, 0);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text_dim), kDimTextRole);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.ok), kSuccessTextRole);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.error), kErrorTextRole);
        lv_obj_set_style_text_font(object, skin.fonts.title, kTitleRole);
    }
    if (lv_obj_check_type(object, &lv_textarea_class)) {
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.surface), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text), 0);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text_dim),
                                    LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_border_width(object, skin.shape.border_width + 1, 0);
        lv_obj_set_style_border_color(object, to_lv_color(skin.colors.accent), 0);
        lv_obj_set_style_radius(object, skin.shape.radius, 0);
        lv_obj_set_style_pad_ver(object, 10, 0);
        lv_obj_set_style_pad_hor(object, 8, 0);
        lv_obj_set_style_text_font(object, skin.fonts.body, 0);
    }
    if (lv_obj_check_type(object, &lv_keyboard_class)) {
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.chrome), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.surface), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(object, to_lv_color(skin.colors.text), LV_PART_ITEMS);
        lv_obj_set_style_radius(object, skin.shape.radius, LV_PART_ITEMS);
        lv_obj_set_style_text_font(object, skin.fonts.body, LV_PART_ITEMS);
    }
    if (lv_obj_check_type(object, &lv_spinner_class)) {
        lv_obj_set_style_arc_color(object, to_lv_color(skin.colors.surface), LV_PART_MAIN);
        lv_obj_set_style_arc_color(object, to_lv_color(skin.colors.accent), LV_PART_INDICATOR);
    }
    if (lv_obj_check_type(object, &lv_bar_class)) {
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.surface), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(object, skin.shape.radius, LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(object, skin.shape.radius, LV_PART_INDICATOR);
    }
    if (lv_obj_check_type(object, &lv_slider_class)) {
        // Slider does not inherit the bar class's applied theme callback, so
        // style all three parts explicitly. The dim rail deliberately remains
        // visible after the accent indicator, showing the full 0–100% range.
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.text_dim), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(object, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_radius(object, skin.shape.radius, LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(object, skin.shape.radius, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(object, to_lv_color(skin.colors.text), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    }
}

}  // namespace micropanel_touch::ui
