#pragma once

#include "ui/layout/layout_node.h"

namespace ui::layout {

class Spacer : public Node {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& final_rect) override;
    bool IsSpacer() const override { return true; }
    void SetWeight(float weight);
    float Weight() const { return weight_; }

protected:
    std::string_view TypeName() const override { return "Spacer"; }

private:
    float weight_ = 1.0f;
};

}  // namespace ui::layout
