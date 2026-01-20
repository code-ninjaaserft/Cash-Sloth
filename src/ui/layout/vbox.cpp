#include "ui/layout/vbox.h"

#include "ui/layout/spacer.h"

#include <algorithm>
#include <vector>

namespace ui::layout {
namespace {

std::vector<int> DistributeWeighted(int amount, const std::vector<float>& weights) {
    std::vector<int> distribution(weights.size(), 0);
    if (amount <= 0 || weights.empty()) {
        return distribution;
    }

    double total_weight = 0.0;
    for (float weight : weights) {
        total_weight += std::max(0.0f, weight);
    }
    if (total_weight <= 0.0) {
        return distribution;
    }

    int assigned = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        double share = amount * (std::max(0.0f, weights[i]) / total_weight);
        int value = static_cast<int>(share);
        distribution[i] = value;
        assigned += value;
    }

    int remainder = amount - assigned;
    for (size_t i = 0; i < weights.size() && remainder > 0; ++i) {
        if (weights[i] <= 0.0f) {
            continue;
        }
        ++distribution[i];
        --remainder;
    }

    return distribution;
}

std::vector<int> DistributeAxisSizes(int available,
                                     const std::vector<int>& fixed_desired,
                                     const std::vector<int>& fixed_minimum,
                                     const std::vector<float>& fixed_flex_weights,
                                     const std::vector<float>& fixed_min_weights,
                                     const std::vector<int>& spacer_minimum,
                                     const std::vector<float>& spacer_weights,
                                     const std::vector<int>& fixed_indices,
                                     const std::vector<int>& spacer_indices,
                                     std::vector<int> sizes) {
    int fixed_desired_sum = 0;
    int fixed_min_sum = 0;
    int spacer_min_sum = 0;

    for (int value : fixed_desired) {
        fixed_desired_sum += value;
    }
    for (int value : fixed_minimum) {
        fixed_min_sum += value;
    }
    for (int value : spacer_minimum) {
        spacer_min_sum += value;
    }

    if (available >= fixed_desired_sum + spacer_min_sum) {
        for (size_t i = 0; i < fixed_indices.size(); ++i) {
            sizes[fixed_indices[i]] = fixed_desired[i];
        }
        int remaining = available - fixed_desired_sum - spacer_min_sum;
        std::vector<int> spacer_extra = DistributeWeighted(remaining, spacer_weights);
        for (size_t i = 0; i < spacer_indices.size(); ++i) {
            sizes[spacer_indices[i]] = spacer_minimum[i] + spacer_extra[i];
        }
        return sizes;
    }

    if (available >= fixed_min_sum + spacer_min_sum) {
        int available_for_fixed = std::max(0, available - spacer_min_sum);
        int fixed_extra = std::max(0, available_for_fixed - fixed_min_sum);
        std::vector<int> fixed_extra_distribution = DistributeWeighted(fixed_extra, fixed_flex_weights);
        for (size_t i = 0; i < fixed_indices.size(); ++i) {
            sizes[fixed_indices[i]] = fixed_minimum[i] + fixed_extra_distribution[i];
        }
        for (size_t i = 0; i < spacer_indices.size(); ++i) {
            sizes[spacer_indices[i]] = spacer_minimum[i];
        }
        return sizes;
    }

    std::vector<int> fixed_distribution = DistributeWeighted(
        available,
        fixed_min_sum > 0 ? fixed_min_weights : std::vector<float>(fixed_indices.size(), 1.0f));
    for (size_t i = 0; i < fixed_indices.size(); ++i) {
        sizes[fixed_indices[i]] = fixed_distribution[i];
    }
    for (int index : spacer_indices) {
        sizes[index] = 0;
    }
    return sizes;
}

}  // namespace

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
    const int available = std::max(0, content.h - gap_total);

    std::vector<int> heights(children_.size(), 0);
    std::vector<int> fixed_indices;
    std::vector<int> spacer_indices;
    std::vector<int> fixed_desired;
    std::vector<int> fixed_minimum;
    std::vector<float> fixed_flex_weights;
    std::vector<float> fixed_min_weights;
    std::vector<int> spacer_minimum;
    std::vector<float> spacer_weights;

    for (size_t i = 0; i < children_.size(); ++i) {
        const auto& child = children_[i];
        int desired = child->DesiredSize().h;
        int minimum = child->MinimumDesiredSize().h;
        if (child->IsSpacer()) {
            spacer_indices.push_back(static_cast<int>(i));
            spacer_minimum.push_back(minimum);
            const auto* spacer = static_cast<const Spacer*>(child.get());
            spacer_weights.push_back(spacer->Weight());
        } else {
            fixed_indices.push_back(static_cast<int>(i));
            fixed_desired.push_back(desired);
            fixed_minimum.push_back(minimum);
            fixed_flex_weights.push_back(static_cast<float>(std::max(0, desired - minimum)));
            fixed_min_weights.push_back(static_cast<float>(std::max(0, minimum)));
        }
    }

    heights = DistributeAxisSizes(available,
                                  fixed_desired,
                                  fixed_minimum,
                                  fixed_flex_weights,
                                  fixed_min_weights,
                                  spacer_minimum,
                                  spacer_weights,
                                  fixed_indices,
                                  spacer_indices,
                                  heights);

    int y = content.y;
    bool has_prev = false;
    for (size_t i = 0; i < children_.size(); ++i) {
        int child_height = std::max(0, heights[i]);
        if (child_height > 0 && has_prev) {
            y += gap_;
        }

        Rect slot{content.x, y, content.w, child_height};
        Rect aligned = AlignChild(slot, children_[i]->DesiredSize(), children_[i]->AlignHorizontal(), AlignV::Stretch);
        children_[i]->Arrange(aligned);

        if (child_height > 0) {
            y += child_height;
            has_prev = true;
        }
    }
}

}  // namespace ui::layout
