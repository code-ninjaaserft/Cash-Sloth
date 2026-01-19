#pragma once

#include "ui/layout/layout_node.h"

namespace ui::layout {

struct GridPlacement {
    int row = 0;
    int col = 0;
    int row_span = 1;
    int col_span = 1;
};

class Grid : public ContainerNode {
public:
    Grid(int rows, int cols);

    void SetGap(int gap);
    void SetDimensions(int rows, int cols);

    Node* AddChild(std::unique_ptr<Node> child, GridPlacement placement);

    Size Measure(const Size& available) override;
    void Arrange(const Rect& final_rect) override;

protected:
    std::string_view TypeName() const override { return "Grid"; }

private:
    int rows_ = 1;
    int cols_ = 1;
    int gap_ = 0;
    std::vector<GridPlacement> placements_{};
};

}  // namespace ui::layout
