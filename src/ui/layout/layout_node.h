#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ui/layout/layout_types.h"

namespace ui::layout {

class Node {
public:
    virtual ~Node() = default;

    void SetId(std::string id);
    const std::string& Id() const;

    void SetPadding(Thickness padding);
    void SetMargin(Thickness margin);
    void SetMinSize(Size size);
    void SetMaxSize(Size size);
    void SetAlign(AlignH align_h, AlignV align_v);
    void SetRules(GroupRules rules);

    virtual Size Measure(const Size& available) = 0;
    virtual void Arrange(const Rect& final_rect) = 0;

    const Rect& ArrangedRect() const;
    const Size& DesiredSize() const;
    std::string DumpTree() const;

    const Thickness& Margin() const { return margin_; }
    const Thickness& Padding() const { return padding_; }
    AlignH AlignHorizontal() const { return align_h_; }
    AlignV AlignVertical() const { return align_v_; }

protected:
    void SetDesiredSize(Size size);
    Rect ApplyMargin(const Rect& final_rect) const;
    Rect ContentRect(const Rect& rect) const;
    Size ClampToMinMax(Size size) const;

    virtual std::string_view TypeName() const = 0;
    virtual void AppendDump(std::ostream& os, int depth) const;
    virtual bool IsSpacer() const { return false; }

    GroupRules rules_{};
    Rect arranged_{};
    Size desired_{};
    Thickness padding_{};
    Thickness margin_{};
    Size min_size_{};
    Size max_size_{};
    bool has_max_size_ = false;
    AlignH align_h_ = AlignH::Stretch;
    AlignV align_v_ = AlignV::Stretch;
    std::string id_{};
};

class ContainerNode : public Node {
public:
    Node* AddChild(std::unique_ptr<Node> child);
    const std::vector<std::unique_ptr<Node>>& Children() const { return children_; }

protected:
    void AppendDump(std::ostream& os, int depth) const override;
    std::vector<std::unique_ptr<Node>> children_{};
};

Rect AlignChild(const Rect& bounds, const Size& desired, AlignH align_h, AlignV align_v);

}  // namespace ui::layout
