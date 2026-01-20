#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "cash_sloth_style.h"
#include "ui/layout/layout_types.h"

namespace cashsloth::layout_scene {

struct LayoutScene {
    ui::layout::Rect rcClient{};
    ui::layout::Rect rcHeader{};

    ui::layout::Rect rcCategoryPanel{};
    ui::layout::Rect rcProductPanel{};
    ui::layout::Rect rcCartArea{};
    ui::layout::Rect rcPaymentArea{};
    ui::layout::Rect rcCartPanel{};
    ui::layout::Rect rcCartSummary{};
    ui::layout::Rect rcCreditPanel{};
    ui::layout::Rect rcActionPanel{};
    ui::layout::Rect rcQuickGrid{};
    ui::layout::Rect rcCategoryFooter{};

    std::unordered_map<std::string, ui::layout::Rect> rects{};

    StyleSheet::Metrics metrics{};
    double scale = 1.0;
    double fontScale = 1.0;
    int titleHeight = 0;
    int titleGap = 0;

    const ui::layout::Rect& get(const char* id) const {
        const auto it = rects.find(id);
        if (it != rects.end()) {
            return it->second;
        }
        static const ui::layout::Rect empty{};
        return empty;
    }
};

LayoutScene ComputeLayoutScene(
    const StyleSheet::Metrics& metrics,
    int windowWidth,
    int windowHeight,
    std::size_t quickAmountCount,
    std::size_t categoryCount,
    std::size_t productCount);

}  // namespace cashsloth::layout_scene
