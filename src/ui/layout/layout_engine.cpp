#include "ui/layout/layout_engine.h"

namespace ui::layout {

namespace {

void CollectRectsRecursive(const Node& node, std::unordered_map<std::string, Rect>& out) {
    if (!node.Id().empty()) {
        out[node.Id()] = node.ArrangedRect();
    }

    const auto* container = dynamic_cast<const ContainerNode*>(&node);
    if (!container) {
        return;
    }

    for (const auto& child : container->Children()) {
        CollectRectsRecursive(*child, out);
    }
}

}  // namespace

std::string LayoutEngine::Dump(const Node& root) {
    return root.DumpTree();
}

void LayoutEngine::CollectRects(const Node& root, std::unordered_map<std::string, Rect>& out) {
    CollectRectsRecursive(root, out);
}

}  // namespace ui::layout
