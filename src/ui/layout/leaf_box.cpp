#include "ui/layout/leaf_box.h"

namespace ui::layout {

void LeafBox::SetPreferredSize(Size size) {
    preferred_ = ClampNonNegative(size);
}

Size LeafBox::Measure(const Size& available) {
    (void)available;
    Size content = preferred_;
    content.w = std::max(content.w, min_size_.w);
    content.h = std::max(content.h, min_size_.h);

    if (has_max_size_) {
        content.w = std::min(content.w, max_size_.w);
        content.h = std::min(content.h, max_size_.h);
    }

    Size desired = AddThickness(content, padding_);
    desired = ClampToMinMax(desired);
    desired = AddThickness(desired, margin_);
    SetDesiredSize(desired);
    return desired;
}

void LeafBox::Arrange(const Rect& final_rect) {
    arranged_ = ApplyMargin(final_rect);
}

}  // namespace ui::layout
