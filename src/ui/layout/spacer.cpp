#include "ui/layout/spacer.h"

namespace ui::layout {

Size Spacer::Measure(const Size& available) {
    (void)available;
    Size content = min_size_;
    Size desired = AddThickness(content, padding_);
    desired = ClampToMinMax(desired);
    desired = AddThickness(desired, margin_);
    SetDesiredSize(desired);
    return desired;
}

void Spacer::Arrange(const Rect& final_rect) {
    arranged_ = ApplyMargin(final_rect);
}

}  // namespace ui::layout
