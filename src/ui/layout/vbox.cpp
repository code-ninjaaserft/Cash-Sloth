#include "ui/layout/vbox.h"

#include <algorithm>

namespace ui::layout {

void VBox::SetGap(int gap) {
    gap_ = std::max(0, gap);
}

Size VBox::Measure(const Size& available) {
    Size inner_available = RemoveThickness(available, margin_);
    inner_available = RemoveThickness(inner_available, padding_);

    int total_height = 0;
    int max_width = 0;
    bool first = true;

    for (const auto& child : children_) {
        Size child_available = RemoveThickness(inner_available, child->Margin());
        Size child_desired = child->Measure(child_available);
        max_width = std::max(max_width, child_desired.w);
        total_height += child_desired.h;
        if (!first) {
            total_height += gap_;
        }
        first = false;
    }

    Size desired{max_width, total_height};
    desired = AddThickness(desired, padding_);
    desired = ClampToMinMax(desired);
    desired = AddThickness(desired, margin_);
    SetDesiredSize(desired);
    return desired;
}

void VBox::Arrange(const Rect& final_rect) {
    arranged_ = ApplyMargin(final_rect);
    Rect content = ContentRect(arranged_);

    int total_fixed = 0;
    int spacer_count = 0;
    bool first = true;
    for (const auto& child : children_) {
        if (child->IsSpacer()) {
            ++spacer_count;
        } else {
            total_fixed += child->DesiredSize().h;
        }
        if (!first) {
            total_fixed += gap_;
        }
        first = false;
    }

    int remaining = content.h - total_fixed;
    int extra_per_spacer = (spacer_count > 0 && remaining > 0) ? (remaining / spacer_count) : 0;
    int remainder = (spacer_count > 0 && remaining > 0) ? (remaining % spacer_count) : 0;

    int y = content.y;
    int available_remaining = content.h;
    for (const auto& child : children_) {
        int child_height = child->DesiredSize().h;
        if (child->IsSpacer()) {
            child_height += extra_per_spacer;
            if (remainder > 0) {
                ++child_height;
                --remainder;
            }
        }
        child_height = std::max(0, child_height);
        child_height = std::min(child_height, available_remaining);

        Rect slot{content.x, y, content.w, child_height};
        Rect aligned = AlignChild(slot, child->DesiredSize(), child->AlignHorizontal(), AlignV::Stretch);
        child->Arrange(aligned);

        y += child_height + gap_;
        available_remaining = std::max(0, content.h - (y - content.y));
    }
}

}  // namespace ui::layout
