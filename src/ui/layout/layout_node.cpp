#include "ui/layout/layout_node.h"

#include <sstream>

namespace ui::layout {

void Node::SetId(std::string id) {
    id_ = std::move(id);
}

const std::string& Node::Id() const {
    return id_;
}

void Node::SetPadding(Thickness padding) {
    padding_ = padding;
}

void Node::SetMargin(Thickness margin) {
    margin_ = margin;
}

void Node::SetMinSize(Size size) {
    min_size_ = ClampNonNegative(size);
}

void Node::SetMaxSize(Size size) {
    max_size_ = ClampNonNegative(size);
    has_max_size_ = true;
}

void Node::SetAlign(AlignH align_h, AlignV align_v) {
    align_h_ = align_h;
    align_v_ = align_v;
}

void Node::SetRules(GroupRules rules) {
    rules_ = rules;
}

const Rect& Node::ArrangedRect() const {
    return arranged_;
}

const Size& Node::DesiredSize() const {
    return desired_;
}

void Node::SetDesiredSize(Size size) {
    desired_ = ClampNonNegative(size);
}

Rect Node::ApplyMargin(const Rect& final_rect) const {
    return DeflateRect(final_rect, margin_);
}

Rect Node::ContentRect(const Rect& rect) const {
    return DeflateRect(rect, padding_);
}

Size Node::ClampToMinMax(Size size) const {
    size = ClampNonNegative(size);
    size.w = std::max(size.w, min_size_.w);
    size.h = std::max(size.h, min_size_.h);
    if (has_max_size_) {
        size.w = std::min(size.w, max_size_.w);
        size.h = std::min(size.h, max_size_.h);
    }
    return size;
}

std::string Node::DumpTree() const {
    std::ostringstream os;
    AppendDump(os, 0);
    return os.str();
}

void Node::AppendDump(std::ostream& os, int depth) const {
    os << std::string(static_cast<std::size_t>(depth * 2), ' ');
    os << TypeName();
    if (!id_.empty()) {
        os << " id=\"" << id_ << "\"";
    }
    os << " desired=" << desired_.w << "x" << desired_.h;
    os << " rect=" << arranged_.x << "," << arranged_.y << " " << arranged_.w << "x" << arranged_.h;
    os << '\n';
}

Node* ContainerNode::AddChild(std::unique_ptr<Node> child) {
    Node* raw = child.get();
    children_.push_back(std::move(child));
    return raw;
}

void ContainerNode::AppendDump(std::ostream& os, int depth) const {
    Node::AppendDump(os, depth);
    for (const auto& child : children_) {
        child->AppendDump(os, depth + 1);
    }
}

Rect AlignChild(const Rect& bounds, const Size& desired, AlignH align_h, AlignV align_v) {
    Rect result = bounds;
    result.w = (align_h == AlignH::Stretch) ? bounds.w : std::min(bounds.w, desired.w);
    result.h = (align_v == AlignV::Stretch) ? bounds.h : std::min(bounds.h, desired.h);

    if (align_h == AlignH::Center) {
        result.x = bounds.x + (bounds.w - result.w) / 2;
    } else if (align_h == AlignH::Right) {
        result.x = bounds.x + (bounds.w - result.w);
    }

    if (align_v == AlignV::Middle) {
        result.y = bounds.y + (bounds.h - result.h) / 2;
    } else if (align_v == AlignV::Bottom) {
        result.y = bounds.y + (bounds.h - result.h);
    }

    result.w = ClampNonNegative(result.w);
    result.h = ClampNonNegative(result.h);
    return result;
}

}  // namespace ui::layout
