#pragma once

#include <algorithm>
#include <cstdint>

namespace ui::layout {

struct Size {
    int w = 0;
    int h = 0;
};

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct Thickness {
    int l = 0;
    int t = 0;
    int r = 0;
    int b = 0;

    static Thickness Uniform(int value) {
        return Thickness{value, value, value, value};
    }
};

enum class AlignH {
    Left,
    Center,
    Right,
    Stretch
};

enum class AlignV {
    Top,
    Middle,
    Bottom,
    Stretch
};

struct GroupRules {
    int minButtonW = 0;
    int minButtonH = 0;
    int padding = 0;
    int gap = 0;
    int minFont = 0;
    int maxFont = 0;
    bool allowTwoLines = false;
    bool ellipsis = false;
};

inline int ClampNonNegative(int value) {
    return std::max(0, value);
}

inline Size ClampNonNegative(Size value) {
    value.w = ClampNonNegative(value.w);
    value.h = ClampNonNegative(value.h);
    return value;
}

inline int Horizontal(const Thickness& thickness) {
    return thickness.l + thickness.r;
}

inline int Vertical(const Thickness& thickness) {
    return thickness.t + thickness.b;
}

inline Size AddThickness(Size size, const Thickness& thickness) {
    size.w += Horizontal(thickness);
    size.h += Vertical(thickness);
    return ClampNonNegative(size);
}

inline Size RemoveThickness(Size size, const Thickness& thickness) {
    size.w -= Horizontal(thickness);
    size.h -= Vertical(thickness);
    return ClampNonNegative(size);
}

inline Rect DeflateRect(const Rect& rect, const Thickness& thickness) {
    Rect result{};
    result.x = rect.x + thickness.l;
    result.y = rect.y + thickness.t;
    result.w = ClampNonNegative(rect.w - Horizontal(thickness));
    result.h = ClampNonNegative(rect.h - Vertical(thickness));
    return result;
}

}  // namespace ui::layout
