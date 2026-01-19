#pragma once

#include "ui/layout/layout_node.h"

namespace ui::layout {

class LeafBox : public Node {
public:
    void SetPreferredSize(Size size);

    Size Measure(const Size& available) override;
    void Arrange(const Rect& final_rect) override;

protected:
    std::string_view TypeName() const override { return "LeafBox"; }

private:
    Size preferred_{};
};

}  // namespace ui::layout
