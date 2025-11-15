#include "cash_sloth_style.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include "cash_sloth_utils.h"

namespace cashsloth {

StyleSheet StyleSheet::load(const std::filesystem::path& baseDir) {
    StyleSheet sheet;
    const std::vector<std::filesystem::path> candidates = {
        baseDir / "assets" / "style.json",
        baseDir / "style.json",
        baseDir / "cash_sloth_styles_v25.11.json"
    };

    std::ifstream input;
    for (const auto& candidate : candidates) {
        input.open(candidate);
        if (input.is_open()) {
            break;
        }
        input.clear();
    }
    if (!input.is_open()) {
        return sheet;
    }
    const std::string payload{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    try {
        JsonParser parser(payload);
        const JsonValue root = parser.parse();
        if (!root.isObject()) {
            return sheet;
        }
        const auto& object = root.asObject();

        const auto paletteIt = object.find("palette");
        if (paletteIt != object.end() && paletteIt->second.isObject()) {
            const auto& paletteObj = paletteIt->second.asObject();
            auto colorOr = [&](const char* key, COLORREF fallback) {
                const auto it = paletteObj.find(key);
                if (it == paletteObj.end()) {
                    return fallback;
                }
                const auto parsed = parseColorValue(it->second);
                return parsed.value_or(fallback);
            };
            sheet.palette.background = colorOr("background", sheet.palette.background);
            sheet.palette.backgroundGlow = colorOr("background_glow", sheet.palette.backgroundGlow);
            sheet.palette.panelBase = colorOr("panel_base", sheet.palette.panelBase);
            sheet.palette.panelElevated = colorOr("panel_elevated", sheet.palette.panelElevated);
            sheet.palette.panelBorder = colorOr("panel_border", sheet.palette.panelBorder);
            sheet.palette.accent = colorOr("accent", sheet.palette.accent);
            sheet.palette.accentStrong = colorOr("accent_strong", sheet.palette.accentStrong);
            sheet.palette.accentSoft = colorOr("accent_soft", sheet.palette.accentSoft);
            sheet.palette.textPrimary = colorOr("text_primary", sheet.palette.textPrimary);
            sheet.palette.textSecondary = colorOr("text_secondary", sheet.palette.textSecondary);
            sheet.palette.success = colorOr("success", sheet.palette.success);
            sheet.palette.danger = colorOr("danger", sheet.palette.danger);
            sheet.palette.tileBase = colorOr("tile_base", sheet.palette.tileBase);
            sheet.palette.tileRaised = colorOr("tile_raised", sheet.palette.tileRaised);
            sheet.palette.quickBase = colorOr("quick_base", sheet.palette.quickBase);
            sheet.palette.quickPressed = colorOr("quick_pressed", sheet.palette.quickPressed);
            sheet.palette.actionBase = colorOr("action_base", sheet.palette.actionBase);
        }

        const auto metricsIt = object.find("metrics");
        if (metricsIt != object.end() && metricsIt->second.isObject()) {
            const auto& metricsObj = metricsIt->second.asObject();
            auto intOr = [&](const char* key, int fallback) {
                const auto it = metricsObj.find(key);
                if (it == metricsObj.end() || !it->second.isNumber()) {
                    return fallback;
                }
                return static_cast<int>(std::round(it->second.asNumber()));
            };
            sheet.metrics.margin = intOr("margin", sheet.metrics.margin);
            sheet.metrics.infoHeight = intOr("info_height", sheet.metrics.infoHeight);
            sheet.metrics.summaryHeight = intOr("summary_height", sheet.metrics.summaryHeight);
            sheet.metrics.gap = intOr("gap", sheet.metrics.gap);
            sheet.metrics.leftColumnWidth = intOr("left_column_width", sheet.metrics.leftColumnWidth);
            sheet.metrics.rightColumnWidth = intOr("right_column_width", sheet.metrics.rightColumnWidth);
            sheet.metrics.categoryHeight = intOr("category_height", sheet.metrics.categoryHeight);
            sheet.metrics.categorySpacing = intOr("category_spacing", sheet.metrics.categorySpacing);
            sheet.metrics.productTileHeight = intOr("product_tile_height", sheet.metrics.productTileHeight);
            sheet.metrics.tileGap = intOr("tile_gap", sheet.metrics.tileGap);
            sheet.metrics.quickButtonHeight = intOr("quick_button_height", sheet.metrics.quickButtonHeight);
            sheet.metrics.quickColumns = (std::max)(1, intOr("quick_columns", sheet.metrics.quickColumns));
            sheet.metrics.actionButtonHeight = intOr("action_button_height", sheet.metrics.actionButtonHeight);
            sheet.metrics.panelRadius = intOr("panel_radius", sheet.metrics.panelRadius);
            sheet.metrics.buttonRadius = intOr("button_radius", sheet.metrics.buttonRadius);
        }

        const auto typographyIt = object.find("typography");
        if (typographyIt != object.end() && typographyIt->second.isObject()) {
            const auto& typoObj = typographyIt->second.asObject();
            auto fontIt = typoObj.find("heading");
            if (fontIt != typoObj.end()) {
                sheet.typography.heading = parseFontSpec(fontIt->second, sheet.typography.heading);
            }
            fontIt = typoObj.find("tile");
            if (fontIt != typoObj.end()) {
                sheet.typography.tile = parseFontSpec(fontIt->second, sheet.typography.tile);
            }
            fontIt = typoObj.find("button");
            if (fontIt != typoObj.end()) {
                sheet.typography.button = parseFontSpec(fontIt->second, sheet.typography.button);
            }
            fontIt = typoObj.find("body");
            if (fontIt != typoObj.end()) {
                sheet.typography.body = parseFontSpec(fontIt->second, sheet.typography.body);
            }
            const auto familyIt = typoObj.find("font_family");
            if (familyIt != typoObj.end() && familyIt->second.isString()) {
                sheet.fontFamily = toWide(familyIt->second.asString());
            }
        }

        const auto quickIt = object.find("quick_amounts");
        if (quickIt != object.end() && quickIt->second.isArray()) {
            std::vector<double> amounts;
            for (const JsonValue& entry : quickIt->second.asArray()) {
                if (entry.isNumber()) {
                    const double value = entry.asNumber();
                    if (value > 0.0) {
                        amounts.push_back(value);
                    }
                }
            }
            if (!amounts.empty()) {
                sheet.quickAmounts = std::move(amounts);
            }
        }

        const auto heroIt = object.find("hero");
        if (heroIt != object.end() && heroIt->second.isObject()) {
            const auto& heroObj = heroIt->second.asObject();
            const auto titleIt = heroObj.find("title");
            if (titleIt != heroObj.end() && titleIt->second.isString()) {
                sheet.hero.title = toWide(titleIt->second.asString());
            }
            const auto subtitleIt = heroObj.find("subtitle");
            if (subtitleIt != heroObj.end() && subtitleIt->second.isString()) {
                sheet.hero.subtitle = toWide(subtitleIt->second.asString());
            }
            const auto badgeIt = heroObj.find("badge");
            if (badgeIt != heroObj.end() && badgeIt->second.isString()) {
                sheet.hero.badge = toWide(badgeIt->second.asString());
            }
        }

        const auto glassIt = object.find("glass_strength");
        if (glassIt != object.end() && glassIt->second.isNumber()) {
            sheet.glassStrength = std::clamp(glassIt->second.asNumber(), 0.05, 0.5);
        }
        const auto glowIt = object.find("accent_glow");
        if (glowIt != object.end() && glowIt->second.isNumber()) {
            sheet.accentGlow = std::clamp(glowIt->second.asNumber(), 0.05, 0.6);
        }
    } catch (const std::exception& exc) {
        std::cerr << "Warnung: Stylesheet konnte nicht geladen werden: " << exc.what() << '\n';
    }
    return sheet;
}

std::optional<COLORREF> StyleSheet::parseColorValue(const JsonValue& value) {
    if (value.isString()) {
        return parseHexColor(value.asString());
    }
    if (value.isArray()) {
        const auto& arr = value.asArray();
        if (arr.size() >= 3 && arr[0].isNumber() && arr[1].isNumber() && arr[2].isNumber()) {
            auto clampChannel = [](double v) -> int {
                return static_cast<int>(std::clamp(std::round(v), 0.0, 255.0));
            };
            return RGB(
                clampChannel(arr[0].asNumber()),
                clampChannel(arr[1].asNumber()),
                clampChannel(arr[2].asNumber()));
        }
    }
    return std::nullopt;
}

std::optional<COLORREF> StyleSheet::parseHexColor(const std::string& text) {
    std::string raw = trim(text);
    if (!raw.empty() && raw.front() == '#') {
        raw.erase(raw.begin());
    }
    if (raw.size() != 6 && raw.size() != 8) {
        return std::nullopt;
    }
    try {
        unsigned long value = std::stoul(raw, nullptr, 16);
        if (raw.size() == 8) {
            value &= 0x00FFFFFF;
        }
        const int r = static_cast<int>((value >> 16) & 0xFF);
        const int g = static_cast<int>((value >> 8) & 0xFF);
        const int b = static_cast<int>(value & 0xFF);
        return RGB(r, g, b);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int StyleSheet::parseFontWeightToken(const std::string& token) {
    const std::string lower = toLower(trim(token));
    if (lower == "thin") {
        return FW_THIN;
    }
    if (lower == "light") {
        return FW_LIGHT;
    }
    if (lower == "medium") {
        return FW_MEDIUM;
    }
    if (lower == "semibold" || lower == "demibold") {
        return FW_SEMIBOLD;
    }
    if (lower == "bold") {
        return FW_BOLD;
    }
    if (lower == "heavy" || lower == "black") {
        return FW_HEAVY;
    }
    return FW_NORMAL;
}

StyleSheet::FontSpec StyleSheet::parseFontSpec(const JsonValue& node, FontSpec fallback) {
    FontSpec spec = fallback;
    if (!node.isObject()) {
        return spec;
    }
    const auto& obj = node.asObject();
    const auto sizeIt = obj.find("size");
    if (sizeIt != obj.end() && sizeIt->second.isNumber()) {
        spec.sizePt = static_cast<int>(std::round(sizeIt->second.asNumber()));
    }
    const auto weightIt = obj.find("weight");
    if (weightIt != obj.end() && weightIt->second.isString()) {
        spec.weight = parseFontWeightToken(weightIt->second.asString());
    }
    return spec;
}

COLORREF mixColor(COLORREF start, COLORREF target, double factor) {
    factor = std::clamp(factor, 0.0, 1.0);
    auto blendChannel = [&](int base, int next) {
        return static_cast<int>(std::round(base + (next - base) * factor));
    };
    return RGB(
        blendChannel(GetRValue(start), GetRValue(target)),
        blendChannel(GetGValue(start), GetGValue(target)),
        blendChannel(GetBValue(start), GetBValue(target)));
}

COLORREF lighten(COLORREF color, double factor) {
    return mixColor(color, RGB(255, 255, 255), factor);
}

COLORREF darken(COLORREF color, double factor) {
    return mixColor(color, RGB(0, 0, 0), factor);
}

TRIVERTEX makeVertex(LONG x, LONG y, COLORREF color) {
    TRIVERTEX vertex{};
    vertex.x = x;
    vertex.y = y;
    vertex.Red = static_cast<COLOR16>(GetRValue(color) << 8);
    vertex.Green = static_cast<COLOR16>(GetGValue(color) << 8);
    vertex.Blue = static_cast<COLOR16>(GetBValue(color) << 8);
    vertex.Alpha = 0;
    return vertex;
}

} // namespace cashsloth

