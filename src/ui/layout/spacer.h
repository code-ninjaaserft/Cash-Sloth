#pragma once

#include "ui/layout/layout_node.h"

namespace ui::layout {

class Spacer : public Node {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& final_rect) override;
    bool IsSpacer() const override { return true; }

protected:
    std::string_view TypeName() const override { return "Spacer"; }
};

}  // namespace ui::layout
