#include "ui/layout/layout_scene_cashsloth.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "ui/layout/grid.h"
#include "ui/layout/hbox.h"
#include "ui/layout/layout_engine.h"
#include "ui/layout/leaf_box.h"
#include "ui/layout/spacer.h"
#include "ui/layout/vbox.h"

namespace cashsloth::layout_scene {

LayoutScene ComputeLayoutScene(
    const StyleSheet::Metrics& metrics,
    int windowWidth,
    int windowHeight,
    std::size_t quickAmountCount,
    std::size_t categoryCount,
    std::size_t productCount) {
    LayoutScene layout{};
    layout.rcClient = ui::layout::Rect{0, 0, windowWidth, windowHeight};

    const double baseWidth = (metrics.baseWidth > 0) ? static_cast<double>(metrics.baseWidth) : 1600.0;
    const double baseHeight = (metrics.baseHeight > 0) ? static_cast<double>(metrics.baseHeight) : 900.0;
    const double sx = static_cast<double>(windowWidth) / baseWidth;
    const double sy = static_cast<double>(windowHeight) / baseHeight;
    layout.scale = std::clamp(std::min(sx, sy), 0.5, 2.0);
    layout.fontScale = layout.scale;

    auto scaled = [&](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };

    layout.metrics = metrics;
    layout.metrics.baseWidth = metrics.baseWidth;
    layout.metrics.baseHeight = metrics.baseHeight;
    layout.metrics.margin = scaled(metrics.margin);
    layout.metrics.infoHeight = scaled(metrics.infoHeight);
    layout.metrics.summaryHeight = scaled(metrics.summaryHeight);
    layout.metrics.gap = scaled(metrics.gap);
    layout.metrics.leftColumnWidth = scaled(metrics.leftColumnWidth);
    layout.metrics.minLeftColumnWidth = scaled(metrics.minLeftColumnWidth);
    layout.metrics.maxLeftColumnWidth = scaled(metrics.maxLeftColumnWidth);
    layout.metrics.minProductsWidth = scaled(metrics.minProductsWidth);
    layout.metrics.minRightColumnWidth = scaled(metrics.minRightColumnWidth);
    layout.metrics.rightColumnWidth = scaled(metrics.rightColumnWidth);
    layout.metrics.minCartListWidth = scaled(metrics.minCartListWidth);
    layout.metrics.minPaymentWidth = scaled(metrics.minPaymentWidth);
    layout.metrics.categoryHeight = scaled(metrics.categoryHeight);
    layout.metrics.categorySpacing = scaled(metrics.categorySpacing);
    layout.metrics.productTileHeight = scaled(metrics.productTileHeight);
    layout.metrics.tileGap = scaled(metrics.tileGap);
    layout.metrics.quickButtonHeight = scaled(metrics.quickButtonHeight);
    layout.metrics.quickColumns = (std::max)(1, metrics.quickColumns);
    layout.metrics.actionButtonHeight = scaled(metrics.actionButtonHeight);
    layout.metrics.panelRadius = scaled(metrics.panelRadius);
    layout.metrics.buttonRadius = scaled(metrics.buttonRadius);
    layout.metrics.titleHeight = scaled(metrics.titleHeight);
    layout.metrics.titleGap = scaled(metrics.titleGap);

    const int margin = layout.metrics.margin;
    const int summaryHeight = layout.metrics.summaryHeight;
    const int gap = layout.metrics.gap;

    layout.titleHeight = layout.metrics.titleHeight;
    layout.titleGap = layout.metrics.titleGap;
    if (layout.titleGap <= 0) {
        layout.titleGap = std::max(2, gap / 2);
    }

    const int headerHeight = layout.metrics.infoHeight + gap;
    layout.rcHeader = ui::layout::Rect{margin, margin, windowWidth - margin * 2, headerHeight};

    const int contentLeft = margin;
    const int contentTop = margin + headerHeight;
    const int contentRight = windowWidth - margin;
    const int contentBottom = windowHeight - margin;

    const int availableWidth = std::max(0, contentRight - contentLeft);
    const int columnGap = gap;
    const int leftMin = std::min(layout.metrics.minLeftColumnWidth, layout.metrics.maxLeftColumnWidth);
    const int leftMax = std::max(layout.metrics.minLeftColumnWidth, layout.metrics.maxLeftColumnWidth);
    const int fixedCategoryWidth = std::clamp(layout.metrics.leftColumnWidth, leftMin, leftMax);
    const int usableWidth = std::max(0, availableWidth - columnGap * 2);
    const int remainingWidth = std::max(0, usableWidth - fixedCategoryWidth);

    const int minProductsWidth = layout.metrics.minProductsWidth;
    const int minRightWidth = layout.metrics.minRightColumnWidth;

    int productsWidth = 0;
    int cartWidth = 0;
    const int minTotal = minProductsWidth + minRightWidth;
    if (remainingWidth <= minTotal) {
        const double factor = (minTotal > 0)
            ? static_cast<double>(remainingWidth) / static_cast<double>(minTotal)
            : 0.0;
        productsWidth = std::max(0, static_cast<int>(std::lround(static_cast<double>(minProductsWidth) * factor)));
        productsWidth = std::min(productsWidth, remainingWidth);
        cartWidth = remainingWidth - productsWidth;
    } else {
        const int extra = remainingWidth - minTotal;
        const int desiredRight = std::clamp(
            layout.metrics.rightColumnWidth,
            minRightWidth,
            minRightWidth + extra);
        const int addRight = desiredRight - minRightWidth;
        cartWidth = minRightWidth + addRight;
        const int addProducts = extra - addRight;
        productsWidth = minProductsWidth + std::max(0, addProducts);
    }

    if (productsWidth + cartWidth < remainingWidth) {
        productsWidth += remainingWidth - (productsWidth + cartWidth);
    } else if (productsWidth + cartWidth > remainingWidth) {
        const int overflow = productsWidth + cartWidth - remainingWidth;
        cartWidth = std::max(0, cartWidth - overflow);
    }

    const int quickColumns = layout.metrics.quickColumns;
    const int quickRows = (std::max)(
        1,
        static_cast<int>((quickAmountCount + static_cast<std::size_t>(quickColumns) - 1)
                          / static_cast<std::size_t>(quickColumns)));
    const int creditPadding = gap;

    const int actionPadding = gap;
    const int actionHeight = actionPadding
        + layout.metrics.actionButtonHeight
        + gap
        + layout.metrics.actionButtonHeight
        + actionPadding
        + scaled(20);

    const int innerGap = gap;
    const int minCartListWidth = layout.metrics.minCartListWidth;
    const int minPaymentWidth = layout.metrics.minPaymentWidth;
    const int availableCartContent = std::max(0, cartWidth - innerGap);
    const int minTotalWidth = minCartListWidth + minPaymentWidth;

    int cartListWidth = 0;
    int payWidth = 0;

    if (minTotalWidth > availableCartContent) {
        const double factor = (minTotalWidth > 0)
            ? static_cast<double>(availableCartContent) / static_cast<double>(minTotalWidth)
            : 0.0;
        cartListWidth = static_cast<int>(std::floor(static_cast<double>(minCartListWidth) * factor));
        payWidth = std::max(0, availableCartContent - cartListWidth);
    } else {
        const int desiredCartList = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(availableCartContent) * 0.56)),
            minCartListWidth,
            availableCartContent - minPaymentWidth);
        cartListWidth = desiredCartList;
        payWidth = availableCartContent - cartListWidth;
    }

    if (payWidth < minPaymentWidth && availableCartContent >= minPaymentWidth) {
        payWidth = minPaymentWidth;
        cartListWidth = availableCartContent - payWidth;
    }

    const int titleInset = std::max(scaled(6), gap / 2);

    auto make_leaf = [](const char* id, int height) {
        auto leaf = std::make_unique<ui::layout::LeafBox>();
        leaf->SetId(id);
        if (height > 0) {
            leaf->SetPreferredSize(ui::layout::Size{0, height});
            leaf->SetMinSize(ui::layout::Size{0, height});
        }
        return leaf;
    };

    ui::layout::HBox root;
    root.SetGap(columnGap);

    auto category_column = std::make_unique<ui::layout::VBox>();
    auto category_panel = std::make_unique<ui::layout::VBox>();
    category_panel->SetId("category_panel");
    category_panel->SetPadding(ui::layout::Thickness::Uniform(titleInset));
    category_panel->SetGap(layout.titleGap);
    category_panel->SetMinSize(ui::layout::Size{fixedCategoryWidth, 0});

    category_panel->AddChild(make_leaf("title_categories", layout.titleHeight));
    auto category_buttons_area = std::make_unique<ui::layout::Spacer>();
    category_buttons_area->SetId("category_buttons_area");
    category_panel->AddChild(std::move(category_buttons_area));

    auto category_footer = std::make_unique<ui::layout::VBox>();
    category_footer->SetId("category_footer");
    const int footerTopPadding = gap / 2;
    const int footerBottomPadding = gap - footerTopPadding;
    ui::layout::Thickness footerPad{};
    footerPad.l = 0;
    footerPad.t = footerTopPadding;
    footerPad.r = 0;
    footerPad.b = footerBottomPadding;
    category_footer->SetPadding(footerPad);
    category_footer->AddChild(make_leaf("btn_edit_mode", layout.metrics.quickButtonHeight));
    category_panel->AddChild(std::move(category_footer));
    category_column->AddChild(std::move(category_panel));

    auto product_column = std::make_unique<ui::layout::VBox>();
    auto product_panel = std::make_unique<ui::layout::VBox>();
    product_panel->SetId("product_panel");
    product_panel->SetPadding(ui::layout::Thickness::Uniform(titleInset));
    product_panel->SetGap(layout.titleGap);
    product_panel->SetMinSize(ui::layout::Size{productsWidth, 0});

    product_panel->AddChild(make_leaf("title_products", layout.titleHeight));
    auto product_buttons_area = std::make_unique<ui::layout::Spacer>();
    product_buttons_area->SetId("product_buttons_area");
    product_panel->AddChild(std::move(product_buttons_area));
    product_column->AddChild(std::move(product_panel));

    auto cart_column = std::make_unique<ui::layout::VBox>();
    cart_column->SetGap(columnGap);
    cart_column->SetMinSize(ui::layout::Size{cartListWidth, 0});

    auto cart_panel = std::make_unique<ui::layout::VBox>();
    cart_panel->SetId("cart_panel");
    cart_panel->SetPadding(ui::layout::Thickness::Uniform(titleInset));
    cart_panel->SetGap(layout.titleGap);
    cart_panel->SetMinSize(ui::layout::Size{cartListWidth, 0});
    cart_panel->AddChild(make_leaf("title_cart", layout.titleHeight));
    auto cart_list = std::make_unique<ui::layout::Spacer>();
    cart_list->SetId("cart_list");
    cart_panel->AddChild(std::move(cart_list));

    auto cart_summary = std::make_unique<ui::layout::VBox>();
    cart_summary->SetId("cart_summary");
    cart_summary->SetPadding(ui::layout::Thickness::Uniform(gap));
    cart_summary->SetMinSize(ui::layout::Size{cartListWidth, summaryHeight});
    cart_summary->SetMaxSize(ui::layout::Size{cartListWidth, summaryHeight});
    auto summary_label = std::make_unique<ui::layout::Spacer>();
    summary_label->SetId("summary_label");
    auto info_label = std::make_unique<ui::layout::Spacer>();
    info_label->SetId("info_label");
    cart_summary->AddChild(std::move(summary_label));
    cart_summary->AddChild(std::move(info_label));

    auto action_panel = std::make_unique<ui::layout::Grid>(2, 2);
    action_panel->SetId("action_panel");
    const int actionExtraBottom = scaled(20);
    ui::layout::Thickness actionPad{};
    actionPad.l = actionPadding;
    actionPad.t = actionPadding;
    actionPad.r = actionPadding;
    actionPad.b = actionPadding + actionExtraBottom;
    action_panel->SetPadding(actionPad);
    action_panel->SetGap(gap);
    action_panel->SetMinSize(ui::layout::Size{cartListWidth, actionHeight});
    action_panel->SetMaxSize(ui::layout::Size{cartListWidth, actionHeight});

    auto remove_button = make_leaf("btn_remove", layout.metrics.actionButtonHeight);
    auto clear_button = make_leaf("btn_clear", layout.metrics.actionButtonHeight);
    auto pay_button = make_leaf("btn_pay", layout.metrics.actionButtonHeight);
    action_panel->AddChild(std::move(remove_button), ui::layout::GridPlacement{0, 0});
    action_panel->AddChild(std::move(clear_button), ui::layout::GridPlacement{0, 1});
    action_panel->AddChild(std::move(pay_button), ui::layout::GridPlacement{1, 0, 1, 2});

    cart_column->AddChild(std::move(cart_panel));
    cart_column->AddChild(std::move(cart_summary));
    cart_column->AddChild(std::move(action_panel));

    auto credit_column = std::make_unique<ui::layout::VBox>();
    auto credit_panel = std::make_unique<ui::layout::VBox>();
    credit_panel->SetId("credit_panel");
    credit_panel->SetPadding(ui::layout::Thickness::Uniform(creditPadding));
    credit_panel->SetGap(gap);
    credit_panel->SetMinSize(ui::layout::Size{payWidth, 0});

    credit_panel->AddChild(make_leaf("title_credit", layout.titleHeight));
    credit_panel->AddChild(make_leaf("title_quick", layout.titleHeight));
    auto quick_grid_area = std::make_unique<ui::layout::Spacer>();
    quick_grid_area->SetId("quick_grid_area");
    credit_panel->AddChild(std::move(quick_grid_area));
    credit_panel->AddChild(make_leaf("title_manual", layout.titleHeight));
    credit_panel->AddChild(make_leaf("edit_manual", layout.metrics.quickButtonHeight));
    credit_panel->AddChild(make_leaf("btn_add_credit", layout.metrics.quickButtonHeight));
    credit_panel->AddChild(make_leaf("btn_undo_credit", layout.metrics.quickButtonHeight));
    credit_column->AddChild(std::move(credit_panel));

    auto right_column = std::make_unique<ui::layout::HBox>();
    right_column->SetGap(innerGap);
    right_column->AddChild(std::move(cart_column));
    right_column->AddChild(std::move(credit_column));

    root.AddChild(std::move(category_column));
    root.AddChild(std::move(product_column));
    root.AddChild(std::move(right_column));

    const int contentWidth = std::max(0, contentRight - contentLeft);
    const int contentHeight = std::max(0, contentBottom - contentTop);
    ui::layout::Size content_size{contentWidth, contentHeight};
    root.Measure(content_size);
    root.Arrange(ui::layout::Rect{contentLeft, contentTop, contentWidth, contentHeight});

    std::unordered_map<std::string, ui::layout::Rect> layout_rects;
    ui::layout::LayoutEngine::CollectRects(root, layout_rects);
    for (const auto& [id, rect] : layout_rects) {
        layout.rects[id] = rect;
    }

    auto quickAreaIt = layout_rects.find("quick_grid_area");
    if (quickAreaIt != layout_rects.end() && quickAmountCount > 0) {
        const ui::layout::Rect& quick_area = quickAreaIt->second;
        ui::layout::Grid quick_grid(quickRows, quickColumns);
        quick_grid.SetGap(gap);
        for (std::size_t i = 0; i < quickAmountCount; ++i) {
            auto leaf = std::make_unique<ui::layout::LeafBox>();
            leaf->SetId("quick_" + std::to_string(i));
            leaf->SetPreferredSize(ui::layout::Size{0, layout.metrics.quickButtonHeight});
            leaf->SetMinSize(ui::layout::Size{0, layout.metrics.quickButtonHeight});
            const int row = static_cast<int>(i / quickColumns);
            const int col = static_cast<int>(i % quickColumns);
            quick_grid.AddChild(std::move(leaf), ui::layout::GridPlacement{row, col});
        }
        quick_grid.Measure(ui::layout::Size{quick_area.w, quick_area.h});
        quick_grid.Arrange(quick_area);
        std::unordered_map<std::string, ui::layout::Rect> quick_rects;
        ui::layout::LayoutEngine::CollectRects(quick_grid, quick_rects);
        for (const auto& [id, rect] : quick_rects) {
            layout.rects[id] = rect;
        }
    }

    auto categoryAreaIt = layout_rects.find("category_buttons_area");
    if (categoryAreaIt != layout_rects.end() && categoryCount > 0) {
        const ui::layout::Rect& area = categoryAreaIt->second;
        const int buttonHeight = layout.metrics.categoryHeight;
        const int buttonSpacing = layout.metrics.categorySpacing;
        ui::layout::VBox category_box;
        category_box.SetGap(buttonSpacing);
        for (std::size_t i = 0; i < categoryCount; ++i) {
            auto leaf = std::make_unique<ui::layout::LeafBox>();
            leaf->SetId("cat_" + std::to_string(i));
            leaf->SetPreferredSize(ui::layout::Size{0, buttonHeight});
            leaf->SetMinSize(ui::layout::Size{0, buttonHeight});
            category_box.AddChild(std::move(leaf));
        }
        category_box.Measure(ui::layout::Size{area.w, area.h});
        category_box.Arrange(area);
        std::unordered_map<std::string, ui::layout::Rect> category_rects;
        ui::layout::LayoutEngine::CollectRects(category_box, category_rects);
        for (const auto& [id, rect] : category_rects) {
            layout.rects[id] = rect;
        }
    }

    auto productAreaIt = layout_rects.find("product_buttons_area");
    if (productAreaIt != layout_rects.end() && productCount > 0) {
        struct ProductGridSettings {
            int columns = 1;
            int tileWidth = 0;
            int tileHeight = 0;
            int padding = 0;
        };

        const ui::layout::Rect& area = productAreaIt->second;
        ProductGridSettings grid{};
        grid.padding = layout.metrics.gap;
        const int panelWidth = std::max(0, area.w);
        const int availableWidth = std::max(0, panelWidth - grid.padding * 2);
        const int minTileWidth = scaled(160);
        const int maxTileWidth = scaled(240);
        int columns = 3;
        while (columns > 1) {
            const int rawWidth = availableWidth - grid.padding * (columns + 1);
            const int testWidth = (columns > 0) ? std::max(0, rawWidth / columns) : 0;
            if (testWidth >= minTileWidth) {
                break;
            }
            --columns;
        }
        columns = std::max(1, columns);
        grid.columns = columns;
        const int finalRawWidth = availableWidth - grid.padding * (columns + 1);
        const int finalWidth = (columns > 0) ? std::max(0, finalRawWidth / columns) : 0;
        grid.tileWidth = std::clamp(finalWidth, minTileWidth, maxTileWidth);
        int tileHeight = static_cast<int>(std::round(static_cast<double>(grid.tileWidth) * 0.75));
        grid.tileHeight = std::clamp(tileHeight, scaled(120), scaled(200));

        const int rows = std::max(1, static_cast<int>((productCount + static_cast<std::size_t>(grid.columns) - 1)
            / static_cast<std::size_t>(grid.columns)));
        ui::layout::Grid product_grid(rows, grid.columns);
        product_grid.SetPadding(ui::layout::Thickness::Uniform(grid.padding));
        product_grid.SetGap(grid.padding);
        for (std::size_t i = 0; i < productCount; ++i) {
            auto leaf = std::make_unique<ui::layout::LeafBox>();
            leaf->SetId("prod_" + std::to_string(i));
            leaf->SetPreferredSize(ui::layout::Size{0, grid.tileHeight});
            leaf->SetMinSize(ui::layout::Size{0, grid.tileHeight});
            const int row = static_cast<int>(i / static_cast<std::size_t>(grid.columns));
            const int col = static_cast<int>(i % static_cast<std::size_t>(grid.columns));
            product_grid.AddChild(std::move(leaf), ui::layout::GridPlacement{row, col});
        }
        product_grid.Measure(ui::layout::Size{area.w, area.h});
        product_grid.Arrange(area);
        std::unordered_map<std::string, ui::layout::Rect> product_rects;
        ui::layout::LayoutEngine::CollectRects(product_grid, product_rects);
        for (const auto& [id, rect] : product_rects) {
            layout.rects[id] = rect;
        }
    }

    layout.rcCategoryPanel = layout.get("category_panel");
    layout.rcProductPanel = layout.get("product_panel");
    layout.rcCartPanel = layout.get("cart_panel");
    layout.rcCartSummary = layout.get("cart_summary");
    layout.rcActionPanel = layout.get("action_panel");
    layout.rcCreditPanel = layout.get("credit_panel");
    layout.rcQuickGrid = layout.get("quick_grid_area");
    layout.rcCategoryFooter = layout.get("category_footer");
    return layout;
}

}  // namespace cashsloth::layout_scene
