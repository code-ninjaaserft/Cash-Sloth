#pragma once

#include <string>
#include <unordered_map>

#include "ui/layout/layout_node.h"

namespace ui::layout {

class LayoutEngine {
public:
    static std::string Dump(const Node& root);
    static void CollectRects(const Node& root, std::unordered_map<std::string, Rect>& out);
};

}  // namespace ui::layout
