#pragma once

#include "ui/layout/layout_node.h"

namespace ui::layout {

class VBox : public ContainerNode {
public:
    void SetGap(int gap);

    Size Measure(const Size& available) override;
    void Arrange(const Rect& final_rect) override;

protected:
    std::string_view TypeName() const override { return "VBox"; }

private:
    int gap_ = 0;
};

}  // namespace ui::layout
