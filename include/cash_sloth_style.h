#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wingdi.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include "cash_sloth_json.h"

namespace cashsloth {

struct StyleSheet {
    struct Palette {
        COLORREF background = RGB(10, 13, 23);
        COLORREF backgroundGlow = RGB(18, 24, 40);
        COLORREF panelBase = RGB(22, 29, 45);
        COLORREF panelElevated = RGB(27, 35, 55);
        COLORREF panelBorder = RGB(41, 52, 79);
        COLORREF accent = RGB(130, 110, 255);
        COLORREF accentStrong = RGB(108, 88, 255);
        COLORREF accentSoft = RGB(176, 190, 255);
        COLORREF textPrimary = RGB(244, 247, 255);
        COLORREF textSecondary = RGB(140, 151, 183);
        COLORREF success = RGB(90, 214, 165);
        COLORREF danger = RGB(244, 128, 144);
        COLORREF tileBase = RGB(35, 44, 67);
        COLORREF tileRaised = RGB(42, 52, 78);
        COLORREF quickBase = RGB(37, 45, 69);
        COLORREF quickPressed = RGB(30, 37, 57);
        COLORREF actionBase = RGB(39, 48, 72);
    } palette;

    struct Metrics {
        int margin = 26;
        int infoHeight = 60;
        int summaryHeight = 52;
        int gap = 20;
        int leftColumnWidth = 280;
        int rightColumnWidth = 340;
        int categoryHeight = 86;
        int categorySpacing = 14;
        int productTileHeight = 148;
        int tileGap = 18;
        int quickButtonHeight = 58;
        int quickColumns = 3;
        int actionButtonHeight = 66;
        int panelRadius = 30;
        int buttonRadius = 22;
    } metrics;

    struct FontSpec {
        int sizePt = 24;
        int weight = FW_NORMAL;
    };

    struct Typography {
        FontSpec heading;
        FontSpec tile;
        FontSpec button;
        FontSpec body;
    } typography;

    struct HeroContent {
        std::wstring title = L"Cash-Sloth POS";
        std::wstring subtitle = L"Modern Point-of-Sale";
        std::wstring badge = L"";
    } hero;

    std::wstring fontFamily = L"Segoe UI";
    std::vector<double> quickAmounts{0.5, 1.0, 2.0, 5.0, 10.0, 20.0};
    double glassStrength = 0.2;
    double accentGlow = 0.3;

    static StyleSheet load(const std::filesystem::path& baseDir);

private:
    static std::optional<COLORREF> parseColorValue(const JsonValue& value);
    static std::optional<COLORREF> parseHexColor(const std::string& text);
    static int parseFontWeightToken(const std::string& token);
    static FontSpec parseFontSpec(const JsonValue& node, FontSpec fallback);
};

COLORREF mixColor(COLORREF start, COLORREF target, double factor);
COLORREF lighten(COLORREF color, double factor);
COLORREF darken(COLORREF color, double factor);
TRIVERTEX makeVertex(LONG x, LONG y, COLORREF color);

} // namespace cashsloth

