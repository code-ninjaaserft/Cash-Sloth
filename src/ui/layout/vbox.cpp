#include "ui/layout/vbox.h"

#include <algorithm>
#include <vector>

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

    const int child_count = static_cast<int>(children_.size());
    const int gap_total = (child_count > 1) ? gap_ * (child_count - 1) : 0;
    const int available_for_children = std::max(0, content.h - gap_total);

    int non_spacer_total = 0;
    int spacer_count = 0;
    for (const auto& child : children_) {
        if (child->IsSpacer()) {
            ++spacer_count;
        } else {
            non_spacer_total += child->DesiredSize().h;
        }
    }

    std::vector<int> heights;
    heights.reserve(children_.size());
    heights.assign(children_.size(), 0);

    if (non_spacer_total == 0) {
        if (spacer_count > 0) {
            int extra_per_spacer = available_for_children / spacer_count;
            int remainder = available_for_children % spacer_count;
            for (size_t i = 0; i < children_.size(); ++i) {
                if (!children_[i]->IsSpacer()) {
                    continue;
                }
                heights[i] = extra_per_spacer;
                if (remainder > 0) {
                    ++heights[i];
                    --remainder;
                }
            }
        }
    } else if (available_for_children >= non_spacer_total) {
        int extra = available_for_children - non_spacer_total;
        int extra_per_spacer = (spacer_count > 0) ? (extra / spacer_count) : 0;
        int remainder = (spacer_count > 0) ? (extra % spacer_count) : 0;
        for (size_t i = 0; i < children_.size(); ++i) {
            if (children_[i]->IsSpacer()) {
                heights[i] = extra_per_spacer;
                if (remainder > 0) {
                    ++heights[i];
                    --remainder;
                }
            } else {
                heights[i] = children_[i]->DesiredSize().h;
            }
        }
    } else {
        int used = 0;
        for (size_t i = 0; i < children_.size(); ++i) {
            if (children_[i]->IsSpacer()) {
                continue;
            }
            int scaled = (children_[i]->DesiredSize().h * available_for_children) / non_spacer_total;
            heights[i] = scaled;
            used += scaled;
        }
        int remainder = available_for_children - used;
        for (size_t i = 0; i < children_.size() && remainder > 0; ++i) {
            if (children_[i]->IsSpacer()) {
                continue;
            }
            ++heights[i];
            --remainder;
        }
    }

    int y = content.y;
    for (size_t i = 0; i < children_.size(); ++i) {
        int child_height = std::max(0, heights[i]);
        Rect slot{content.x, y, content.w, child_height};
        Rect aligned = AlignChild(slot, children_[i]->DesiredSize(), children_[i]->AlignHorizontal(), AlignV::Stretch);
        children_[i]->Arrange(aligned);

        y += child_height;
        if (i + 1 < children_.size()) {
            y += gap_;
        }
    }
}

}  // namespace ui::layout
