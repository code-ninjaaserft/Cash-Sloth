#include <cstdlib>
#include <iostream>

#include "ui/layout/grid.h"
#include "ui/layout/hbox.h"
#include "ui/layout/leaf_box.h"
#include "ui/layout/spacer.h"
#include "ui/layout/vbox.h"

namespace ui::layout::tests {

void ExpectEqual(int actual, int expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << "\n";
        std::exit(EXIT_FAILURE);
    }
}

void ExpectGreater(int actual, int expected_min, const char* message) {
    if (actual <= expected_min) {
        std::cerr << "FAIL: " << message << " expected > " << expected_min << " actual=" << actual << "\n";
        std::exit(EXIT_FAILURE);
    }
}

void TestVBoxGapPadding() {
    VBox root;
    root.SetGap(3);
    root.SetPadding(Thickness::Uniform(2));

    auto box1 = std::make_unique<LeafBox>();
    auto box2 = std::make_unique<LeafBox>();
    auto box3 = std::make_unique<LeafBox>();

    box1->SetPreferredSize(Size{10, 10});
    box2->SetPreferredSize(Size{10, 10});
    box3->SetPreferredSize(Size{10, 10});

    LeafBox* box1_ptr = static_cast<LeafBox*>(root.AddChild(std::move(box1)));
    LeafBox* box2_ptr = static_cast<LeafBox*>(root.AddChild(std::move(box2)));
    LeafBox* box3_ptr = static_cast<LeafBox*>(root.AddChild(std::move(box3)));

    root.Measure(Size{100, 100});
    root.Arrange(Rect{0, 0, 100, 100});

    ExpectEqual(box1_ptr->ArrangedRect().y, 2, "VBox box1 y");
    ExpectEqual(box2_ptr->ArrangedRect().y, 15, "VBox box2 y");
    ExpectEqual(box3_ptr->ArrangedRect().y, 28, "VBox box3 y");
}

void TestHBoxSpacer() {
    HBox root;
    root.SetGap(2);

    auto left = std::make_unique<LeafBox>();
    auto spacer = std::make_unique<Spacer>();
    auto right = std::make_unique<LeafBox>();

    left->SetPreferredSize(Size{10, 10});
    right->SetPreferredSize(Size{10, 10});

    LeafBox* left_ptr = static_cast<LeafBox*>(root.AddChild(std::move(left)));
    Spacer* spacer_ptr = static_cast<Spacer*>(root.AddChild(std::move(spacer)));
    LeafBox* right_ptr = static_cast<LeafBox*>(root.AddChild(std::move(right)));

    root.Measure(Size{100, 20});
    root.Arrange(Rect{0, 0, 100, 20});

    ExpectEqual(left_ptr->ArrangedRect().x, 0, "HBox left x");
    ExpectEqual(spacer_ptr->ArrangedRect().w, 76, "HBox spacer width");
    ExpectEqual(right_ptr->ArrangedRect().x, 90, "HBox right x");
}

void TestGridUniform() {
    Grid grid(2, 2);
    grid.SetGap(2);

    auto a = std::make_unique<LeafBox>();
    auto b = std::make_unique<LeafBox>();
    auto c = std::make_unique<LeafBox>();
    auto d = std::make_unique<LeafBox>();

    a->SetPreferredSize(Size{10, 10});
    b->SetPreferredSize(Size{10, 10});
    c->SetPreferredSize(Size{10, 10});
    d->SetPreferredSize(Size{10, 10});

    LeafBox* a_ptr = static_cast<LeafBox*>(grid.AddChild(std::move(a), GridPlacement{0, 0, 1, 1}));
    LeafBox* b_ptr = static_cast<LeafBox*>(grid.AddChild(std::move(b), GridPlacement{0, 1, 1, 1}));
    LeafBox* c_ptr = static_cast<LeafBox*>(grid.AddChild(std::move(c), GridPlacement{1, 0, 1, 1}));
    LeafBox* d_ptr = static_cast<LeafBox*>(grid.AddChild(std::move(d), GridPlacement{1, 1, 1, 1}));

    grid.Measure(Size{100, 100});
    grid.Arrange(Rect{0, 0, 100, 100});

    ExpectEqual(a_ptr->ArrangedRect().w, 49, "Grid cell width");
    ExpectEqual(a_ptr->ArrangedRect().h, 49, "Grid cell height");
    ExpectEqual(b_ptr->ArrangedRect().x, 51, "Grid cell x");
    ExpectEqual(c_ptr->ArrangedRect().y, 51, "Grid cell y");
    ExpectEqual(d_ptr->ArrangedRect().x, 51, "Grid cell x2");
}

void TestMarginPadding() {
    VBox root;
    root.SetPadding(Thickness::Uniform(3));

    auto child = std::make_unique<LeafBox>();
    child->SetPreferredSize(Size{10, 10});
    child->SetMargin(Thickness::Uniform(5));
    child->SetAlign(AlignH::Left, AlignV::Top);

    LeafBox* child_ptr = static_cast<LeafBox*>(root.AddChild(std::move(child)));

    root.Measure(Size{50, 50});
    root.Arrange(Rect{0, 0, 50, 50});

    ExpectEqual(child_ptr->ArrangedRect().x, 8, "Margin/padding x");
    ExpectEqual(child_ptr->ArrangedRect().y, 8, "Margin/padding y");
    ExpectEqual(child_ptr->ArrangedRect().w, 10, "Margin/padding width");
    ExpectEqual(child_ptr->ArrangedRect().h, 10, "Margin/padding height");
}

void TestVBoxActionPanelPreserved() {
    VBox root;
    root.SetGap(2);

    auto spacer = std::make_unique<Spacer>();
    auto summary = std::make_unique<LeafBox>();
    auto action = std::make_unique<LeafBox>();

    summary->SetPreferredSize(Size{10, 40});
    action->SetPreferredSize(Size{10, 30});

    root.AddChild(std::move(spacer));
    LeafBox* summary_ptr = static_cast<LeafBox*>(root.AddChild(std::move(summary)));
    LeafBox* action_ptr = static_cast<LeafBox*>(root.AddChild(std::move(action)));

    root.Measure(Size{100, 50});
    root.Arrange(Rect{0, 0, 100, 50});

    ExpectGreater(summary_ptr->ArrangedRect().h, 0, "VBox summary height");
    ExpectGreater(action_ptr->ArrangedRect().h, 0, "VBox action height");
    ExpectGreater(action_ptr->ArrangedRect().y, summary_ptr->ArrangedRect().y, "VBox action below summary");
}

}  // namespace ui::layout::tests

int main() {
    ui::layout::tests::TestVBoxGapPadding();
    ui::layout::tests::TestHBoxSpacer();
    ui::layout::tests::TestGridUniform();
    ui::layout::tests::TestMarginPadding();
    ui::layout::tests::TestVBoxActionPanelPreserved();
    std::cout << "All layout tests passed." << std::endl;
    return 0;
}
