#include "ui/layout/grid.h"

#include <algorithm>

namespace ui::layout {

Grid::Grid(int rows, int cols) {
    SetDimensions(rows, cols);
}

void Grid::SetGap(int gap) {
    gap_ = std::max(0, gap);
}

void Grid::SetDimensions(int rows, int cols) {
    rows_ = std::max(1, rows);
    cols_ = std::max(1, cols);
}

Node* Grid::AddChild(std::unique_ptr<Node> child, GridPlacement placement) {
    placements_.push_back(placement);
    return ContainerNode::AddChild(std::move(child));
}

Size Grid::Measure(const Size& available) {
    Size inner_available = RemoveThickness(available, margin_);
    inner_available = RemoveThickness(inner_available, padding_);

    int cell_width = 0;
    int cell_height = 0;

    for (std::size_t i = 0; i < children_.size(); ++i) {
        const auto& entry = placements_[i];
        const auto& child = children_[i];
        Size child_available = RemoveThickness(inner_available, child->Margin());
        Size child_desired = child->Measure(child_available);
        int col_span = std::max(1, entry.col_span);
        int row_span = std::max(1, entry.row_span);
        cell_width = std::max(cell_width, (child_desired.w + col_span - 1) / col_span);
        cell_height = std::max(cell_height, (child_desired.h + row_span - 1) / row_span);
    }

    int total_width = cols_ * cell_width + gap_ * (cols_ - 1);
    int total_height = rows_ * cell_height + gap_ * (rows_ - 1);

    Size desired{total_width, total_height};
    desired = AddThickness(desired, padding_);
    desired = ClampToMinMax(desired);
    desired = AddThickness(desired, margin_);
    SetDesiredSize(desired);
    return desired;
}

void Grid::Arrange(const Rect& final_rect) {
    arranged_ = ApplyMargin(final_rect);
    Rect content = ContentRect(arranged_);

    int total_gap_w = gap_ * (cols_ - 1);
    int total_gap_h = gap_ * (rows_ - 1);
    int cell_w = (cols_ > 0) ? std::max(0, (content.w - total_gap_w) / cols_) : 0;
    int cell_h = (rows_ > 0) ? std::max(0, (content.h - total_gap_h) / rows_) : 0;

    for (std::size_t i = 0; i < children_.size(); ++i) {
        const auto& entry = placements_[i];
        const auto& child = children_[i];
        int col = std::clamp(entry.col, 0, cols_ - 1);
        int row = std::clamp(entry.row, 0, rows_ - 1);
        int col_span = std::clamp(entry.col_span, 1, cols_ - col);
        int row_span = std::clamp(entry.row_span, 1, rows_ - row);

        int x = content.x + col * (cell_w + gap_);
        int y = content.y + row * (cell_h + gap_);
        int w = cell_w * col_span + gap_ * (col_span - 1);
        int h = cell_h * row_span + gap_ * (row_span - 1);

        Rect slot{x, y, std::max(0, w), std::max(0, h)};
        Rect aligned = AlignChild(slot, child->DesiredSize(), child->AlignHorizontal(), child->AlignVertical());
        child->Arrange(aligned);
    }
}

}  // namespace ui::layout
