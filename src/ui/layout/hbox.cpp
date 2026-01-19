#include "ui/layout/hbox.h"

#include <algorithm>

namespace ui::layout {

void HBox::SetGap(int gap) {
    gap_ = std::max(0, gap);
}

Size HBox::Measure(const Size& available) {
    Size inner_available = RemoveThickness(available, margin_);
    inner_available = RemoveThickness(inner_available, padding_);

    int total_width = 0;
    int max_height = 0;
    bool first = true;

    for (const auto& child : children_) {
        Size child_available = RemoveThickness(inner_available, child->Margin());
        Size child_desired = child->Measure(child_available);
        max_height = std::max(max_height, child_desired.h);
        total_width += child_desired.w;
        if (!first) {
            total_width += gap_;
        }
        first = false;
    }

    Size desired{total_width, max_height};
    desired = AddThickness(desired, padding_);
    desired = ClampToMinMax(desired);
    desired = AddThickness(desired, margin_);
    SetDesiredSize(desired);
    return desired;
}

void HBox::Arrange(const Rect& final_rect) {
    arranged_ = ApplyMargin(final_rect);
    Rect content = ContentRect(arranged_);

    int total_fixed = 0;
    int spacer_count = 0;
    bool first = true;
    for (const auto& child : children_) {
        if (child->IsSpacer()) {
            ++spacer_count;
        } else {
            total_fixed += child->DesiredSize().w;
        }
        if (!first) {
            total_fixed += gap_;
        }
        first = false;
    }

    int remaining = content.w - total_fixed;
    int extra_per_spacer = (spacer_count > 0 && remaining > 0) ? (remaining / spacer_count) : 0;
    int remainder = (spacer_count > 0 && remaining > 0) ? (remaining % spacer_count) : 0;

    int x = content.x;
    int available_remaining = content.w;
    for (const auto& child : children_) {
        int child_width = child->DesiredSize().w;
        if (child->IsSpacer()) {
            child_width += extra_per_spacer;
            if (remainder > 0) {
                ++child_width;
                --remainder;
            }
        }
        child_width = std::max(0, child_width);
        child_width = std::min(child_width, available_remaining);

        Rect slot{x, content.y, child_width, content.h};
        Rect aligned = AlignChild(slot, child->DesiredSize(), AlignH::Stretch, child->AlignVertical());
        child->Arrange(aligned);

        x += child_width + gap_;
        available_remaining = std::max(0, content.w - (x - content.x));
    }
}

}  // namespace ui::layout
