#include <windows.h>
#include <windowsx.h>
#if !defined(_WIN32)
#include <cstdlib>
#include <iostream>

int main() {
    std::cerr << "Cash-Sloth POS Touch requires Windows to run." << std::endl;
    return EXIT_FAILURE;
}

#else

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#include <commctrl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cash_sloth_json.h"
#include "cash_sloth_style.h"
#include "cash_sloth_diagnostics.h"
#include "cash_sloth_utils.h"
#include "ui/layout/hbox.h"
#include "ui/layout/layout_engine.h"
#include "ui/layout/layout_types.h"
#include "ui/layout/leaf_box.h"
#include "ui/layout/grid.h"
#include "ui/layout/spacer.h"
#include "ui/layout/vbox.h"

#if defined(_MSC_VER)
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#endif  // defined(_MSC_VER)

namespace cashsloth {

struct Layout {
    RECT rcClient{};
    RECT rcHeader{};

    RECT rcCategoryPanel{};
    RECT rcProductPanel{};
    RECT rcCartArea{};
    RECT rcPaymentArea{};
    RECT rcCartPanel{};
    RECT rcCartSummary{};
    RECT rcCreditPanel{};
    RECT rcActionPanel{};
    RECT rcQuickGrid{};
    RECT rcCategoryFooter{};

    std::unordered_map<std::string, RECT> rects{};

    StyleSheet::Metrics metrics{};
    double scale = 1.0;
    double fontScale = 1.0;
    int titleHeight = 0;
    int titleGap = 0;

    const RECT& get(const char* id) const {
        const auto it = rects.find(id);
        if (it != rects.end()) {
            return it->second;
        }
        static const RECT empty{};
        return empty;
    }
};

struct Article {
    std::string name;
    double price = 0.0;
    std::string barcode;
};

struct Category {
    std::string name;
    std::vector<Article> articles;
};

class Catalogue {
public:
    bool loadFromFile(const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input.is_open()) {
            return false;
        }
        const std::string payload{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        try {
            JsonParser parser(payload);
            JsonValue root = parser.parse();
            std::vector<Category> newCategories = parseCategories(root);
            if (newCategories.empty()) {
                return false;
            }
            categories_ = std::move(newCategories);
            rebuildBarcodeIndex();
            loadedFile_ = path;
            return true;
        } catch (const std::exception& exc) {
            std::cerr << "Warnung: Katalog konnte nicht aus \"" << path << "\" gelesen werden: "
                      << exc.what() << '\n';
            DiagnosticsMonitor::instance().recordWarning(
                std::string("Katalog konnte nicht geladen werden: ") + path.string() + " | " + exc.what());
            return false;
        }
    }

    void loadDefault() {
        categories_ = buildDefaultCatalogue();
        rebuildBarcodeIndex();
        loadedFile_.clear();
    }

    bool empty() const { return categories_.empty(); }
    const std::vector<Category>& categories() const { return categories_; }

    const Article* findByBarcode(const std::string& raw) const {
        const std::string normalized = normalizeBarcode(raw);
        if (normalized.empty()) {
            return nullptr;
        }
        const auto it = barcodeIndex_.find(normalized);
        return (it != barcodeIndex_.end()) ? it->second : nullptr;
    }

    const std::filesystem::path& loadedFile() const { return loadedFile_; }

private:
    static std::string normalizeBarcode(const std::string& raw) {
        std::string result;
        result.reserve(raw.size());
        for (char ch : raw) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                result.push_back(ch);
            }
        }
        return result;
    }

    static std::optional<double> parsePrice(const JsonValue& value) {
        if (value.isNumber()) {
            return value.asNumber();
        }
        if (value.isString()) {
            std::string text = value.asString();
            text.erase(
                std::remove_if(
                    text.begin(),
                    text.end(),
                    [](unsigned char ch) { return std::isspace(ch); }),
                text.end());
            std::replace(text.begin(), text.end(), ',', '.');
            if (text.empty()) {
                return std::nullopt;
            }
            try {
                size_t consumed = 0;
                double parsed = std::stod(text, &consumed);
                if (consumed == text.size()) {
                    return parsed;
                }
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    static JsonValue::Object::const_iterator findAny(
        const JsonValue::Object& object,
        std::initializer_list<std::string_view> keys) {
        for (auto key : keys) {
            const auto it = object.find(std::string(key));
            if (it != object.end()) {
                return it;
            }
        }
        return object.end();
    }

    static std::vector<Category> parseCategories(const JsonValue& root) {
        std::vector<Category> result;

        auto parseCategoryArray = [&](const JsonValue::Array& array) {
            for (const JsonValue& entry : array) {
                if (!entry.isObject()) {
                    continue;
                }
                const auto& obj = entry.asObject();
                const auto nameIt = obj.find("name");
                if (nameIt == obj.end() || !nameIt->second.isString()) {
                    continue;
                }
                Category category;
                category.name = nameIt->second.asString();
                const auto articlesIt = obj.find("articles");
                if (articlesIt != obj.end() && articlesIt->second.isArray()) {
                    for (const JsonValue& artValue : articlesIt->second.asArray()) {
                        if (!artValue.isObject()) {
                            continue;
                        }
                        const auto& artObj = artValue.asObject();
                        const auto artNameIt = artObj.find("name");
                        if (artNameIt == artObj.end() || !artNameIt->second.isString()) {
                            continue;
                        }
                        const auto priceIt = findAny(artObj, {"price", "preis", "cost"});
                        if (priceIt == artObj.end()) {
                            continue;
                        }
                        const std::optional<double> maybePrice = parsePrice(priceIt->second);
                        if (!maybePrice.has_value() || maybePrice.value() < 0.0) {
                            continue;
                        }

                        Article article;
                        article.name = artNameIt->second.asString();
                        article.price = maybePrice.value();

                        const auto barcodeIt = artObj.find("barcode");
                        if (barcodeIt != artObj.end()) {
                            if (barcodeIt->second.isString()) {
                                article.barcode = normalizeBarcode(barcodeIt->second.asString());
                            } else if (barcodeIt->second.isNull()) {
                                article.barcode.clear();
                            }
                        }
                        category.articles.push_back(std::move(article));
                    }
                }
                if (!category.articles.empty()) {
                    result.push_back(std::move(category));
                }
            }
        };

        if (root.isObject()) {
            const auto& obj = root.asObject();
            const auto categoriesIt = obj.find("categories");
            if (categoriesIt != obj.end() && categoriesIt->second.isArray()) {
                parseCategoryArray(categoriesIt->second.asArray());
            } else {
                for (const auto& [key, value] : obj) {
                    if (!value.isArray()) {
                        continue;
                    }
                    Category category;
                    category.name = key;
                    for (const JsonValue& artValue : value.asArray()) {
                        if (!artValue.isObject()) {
                            continue;
                        }
                        const auto& artObj = artValue.asObject();
                        const auto artNameIt = artObj.find("name");
                        const auto priceIt = findAny(artObj, {"price", "preis", "cost"});
                        if (artNameIt == artObj.end() || priceIt == artObj.end()) {
                            continue;
                        }
                        if (!artNameIt->second.isString()) {
                            continue;
                        }
                        const std::optional<double> maybePrice = parsePrice(priceIt->second);
                        if (!maybePrice.has_value() || maybePrice.value() < 0.0) {
                            continue;
                        }
                        Article article;
                        article.name = artNameIt->second.asString();
                        article.price = maybePrice.value();
                        const auto barcodeIt = artObj.find("barcode");
                        if (barcodeIt != artObj.end() && barcodeIt->second.isString()) {
                            article.barcode = normalizeBarcode(barcodeIt->second.asString());
                        }
                        category.articles.push_back(std::move(article));
                    }
                    if (!category.articles.empty()) {
                        result.push_back(std::move(category));
                    }
                }
            }
        } else if (root.isArray()) {
            parseCategoryArray(root.asArray());
        }
        return result;
    }

    static std::vector<Category> buildDefaultCatalogue() {
        return {
            {"Alkoholische Getraenke",
             {
                 {"Bier", 4.0, "761000000001"},
                 {"Wein", 19.0, "761000000002"},
                 {"Schnaps", 5.0, "761000000003"},
             }},
            {"Softgetraenke",
             {
                 {"3dl Getraenk", 2.0, "761000000101"},
                 {"1.5l Getraenk", 7.0, "761000000102"},
             }},
            {"Snacks",
             {
                 {"Russenzopf & Kaffee", 3.0, "761000000201"},
                 {"Sandwich Salami", 6.5, "761000000202"},
             }},
            {"Kaffee & Tee",
             {
                 {"Espresso", 2.5, "761000000301"},
                 {"Cappuccino", 3.5, "761000000302"},
                 {"Gruentee", 3.5, ""},
                 {"Schwarztee", 4.0, ""},
                 {"Lungo", 2.5, ""},
             }},
        };
    }

    void rebuildBarcodeIndex() {
        barcodeIndex_.clear();
        for (const Category& category : categories_) {
            for (const Article& article : category.articles) {
                if (!article.barcode.empty()) {
                    barcodeIndex_[article.barcode] = &article;
                }
            }
        }
    }

    std::vector<Category> categories_;
    std::unordered_map<std::string, const Article*> barcodeIndex_;
    std::filesystem::path loadedFile_;
};

struct CartItem {
    const Article* article = nullptr;
    int quantity = 0;
};

class Cart {
public:
    void add(const Article& article) {
        for (CartItem& item : items_) {
            if (item.article == &article) {
                ++item.quantity;
                return;
            }
        }
        items_.push_back(CartItem{&article, 1});
    }

    void remove(std::size_t index) {
        if (index >= items_.size()) {
            return;
        }
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void clear() {
        items_.clear();
        credit_ = 0.0;
        creditHistory_.clear();
    }

    void addCredit(double amount) {
        credit_ += amount;
        creditHistory_.push_back(amount);
    }

    std::optional<double> undoCredit() {
        if (creditHistory_.empty()) {
            return std::nullopt;
        }
        const double amount = creditHistory_.back();
        creditHistory_.pop_back();
        credit_ = std::max(0.0, credit_ - amount);
        return amount;
    }

    double total() const {
        double sum = 0.0;
        for (const CartItem& item : items_) {
            if (item.article) {
                sum += item.article->price * static_cast<double>(item.quantity);
            }
        }
        return sum;
    }

    double change() const {
        const double diff = credit_ - total();
        return diff > 0.0 ? diff : 0.0;
    }

    bool empty() const { return items_.empty(); }
    bool hasCreditHistory() const { return !creditHistory_.empty(); }
    double credit() const { return credit_; }
    const std::vector<CartItem>& items() const { return items_; }

private:
    std::vector<CartItem> items_;
    double credit_ = 0.0;
    std::vector<double> creditHistory_;
};




Layout computeLayout(
    const StyleSheet::Metrics& metrics,
    int windowWidth,
    int windowHeight,
    std::size_t quickAmountCount,
    std::size_t categoryCount,
    std::size_t productCount) {
    Layout layout{};
    layout.rcClient = {0, 0, windowWidth, windowHeight};

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
    layout.rcHeader = {margin, margin, windowWidth - margin, margin + headerHeight};

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
    category_footer->SetPadding(ui::layout::Thickness{0, footerTopPadding, 0, footerBottomPadding});
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
    action_panel->SetPadding(ui::layout::Thickness{actionPadding, actionPadding, actionPadding, actionPadding + actionExtraBottom});
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

    auto toRect = [](const ui::layout::Rect& rect) {
        RECT rc{};
        rc.left = rect.x;
        rc.top = rect.y;
        rc.right = rect.x + rect.w;
        rc.bottom = rect.y + rect.h;
        return rc;
    };

    for (const auto& [id, rect] : layout_rects) {
        layout.rects[id] = toRect(rect);
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
            layout.rects[id] = toRect(rect);
        }
    }

    auto categoryAreaIt = layout_rects.find("category_buttons_area");
    if (categoryAreaIt != layout_rects.end() && categoryCount > 0) {
        const ui::layout::Rect& area = categoryAreaIt->second;
        const int buttonHeight = layout.metrics.categoryHeight;
        const int buttonSpacing = layout.metrics.categorySpacing;
        int maxFit = 0;
        if (buttonHeight > 0 && area.h > 0) {
            maxFit = (area.h + buttonSpacing) / (buttonHeight + buttonSpacing);
        }
        const std::size_t count = std::min(categoryCount, static_cast<std::size_t>(std::max(0, maxFit)));
        if (count > 0) {
            ui::layout::VBox category_box;
            category_box.SetGap(buttonSpacing);
            for (std::size_t i = 0; i < count; ++i) {
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
                layout.rects[id] = toRect(rect);
            }
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
        const int areaBottom = area.y + area.h;
        for (const auto& [id, rect] : product_rects) {
            if (rect.y + rect.h <= areaBottom) {
                layout.rects[id] = toRect(rect);
            }
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


}  // namespace cashsloth

using namespace cashsloth;

namespace {

constexpr wchar_t kAppVersion[] = L"25.11.10";
constexpr wchar_t kWindowTitle[] = L"Cash-Sloth POS Touch v25.11.10";

enum ControlId : int {
    ID_CART_LIST = 1101,
    ID_EDIT_CREDIT = 1102,
    ID_BUTTON_ADD_CREDIT = 1103,
    ID_BUTTON_UNDO_CREDIT = 1104,
    ID_BUTTON_REMOVE_ITEM = 1105,
    ID_BUTTON_CLEAR_CART = 1106,
    ID_BUTTON_PAY = 1107
};

constexpr int ID_CATEGORY_BASE = 2000;
constexpr int ID_PRODUCT_BASE = 3000;
constexpr int ID_QUICK_AMOUNT_BASE = 4000;

}  // namespace

class CashSlothGUI {
public:
    explicit CashSlothGUI(HINSTANCE instance);
    ~CashSlothGUI();

    int run(int nCmdShow);

private:
    struct ProductGridMetrics {
        int columns = 1;
        int tileWidth = 0;
        int tileHeight = 0;
        int padding = 0;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static constexpr UINT_PTR kAnimationTimerId = 1;

    void onCreate();
    void onDestroy();
    void onCommand(int controlId, int notificationCode);
    void onDrawItem(LPDRAWITEMSTRUCT dis);
    HBRUSH onCtlColorStatic(HDC dc, HWND hwnd);
    HBRUSH onCtlColorPanel(HDC dc);
    void onPaint();
    void onTimer(UINT_PTR timerId);
    LRESULT onNcHitTest(LPARAM lParam);

    void initDpiAndResources();
    void releaseGdiResources();
    void refreshFonts();
    void calculateLayout();
    void applyLayout();
    void createInfoAndSummary();
    void createPanelTitles();
    void createCategoryFooter();
    void createCartArea();
    void createCreditPanel();
    void createActionButtons();
    void toggleFullscreen();
    void loadCatalogue();
    void buildCategoryButtons();
    void rebuildProductButtons();
    void updateCategoryHighlight();
    void refreshCart();
    void refreshStatus();
    void showInfo(const std::wstring& text);
    void addCredit(double amount);
    void onAddCredit();
    void onUndoCredit();
    void onRemoveCartItem();
    void onPay();

    bool initializeFullUi(std::wstring& failureReason);
    void destroyAllChildWindows();
    void enterMinimalMode(const std::wstring& reason);
    void layoutMinimalMode();

    void drawCategoryButton(LPDRAWITEMSTRUCT dis);
    void drawProductButton(LPDRAWITEMSTRUCT dis);
    void drawQuickAmountButton(LPDRAWITEMSTRUCT dis);
    void drawActionButton(LPDRAWITEMSTRUCT dis);
    void drawRoundedButton(LPDRAWITEMSTRUCT dis, COLORREF baseColor, COLORREF textColor, const std::wstring& fallbackText, HFONT font, bool drawText);
    void ensureBackBuffer(HDC referenceDC, int width, int height);
    void releaseBackBuffer();
    void drawPanel(HDC dc, const RECT& area) const;
    void drawBackdrop(HDC dc) const;
    void drawCatalogueErrorBanner(HDC dc) const;
    HFONT createFont(const StyleSheet::FontSpec& spec) const;
    HFONT createFont(const StyleSheet::FontSpec& spec, int pointSize) const;
    void ensureSectionTitle(HWND& handle, const std::wstring& text, int x, int y, int width);
    int scale(int value) const;
    int measureTextWidth(HDC dc, HFONT font, const std::wstring& text) const;
    int measureFontHeight(HDC dc, HFONT font) const;
    double computeSingleLineFontScale(
        HDC dc,
        const StyleSheet::FontSpec& spec,
        int availableWidth,
        int availableHeight,
        const std::vector<std::wstring>& texts,
        int minPointSize) const;
    ProductGridMetrics computeProductGrid() const;
    void updateAdaptiveFonts();
    void updateProductNameFont(const ProductGridMetrics& grid);
    bool updateAdaptiveLayoutMetrics(StyleSheet::Metrics& metrics);
    void updateAnimation();
    void updateHeaderVisibility();

    HINSTANCE instance_;
    HWND window_ = nullptr;

    StyleSheet style_;
    Catalogue catalogue_;
    Cart cart_;
    std::vector<const Category*> categoryOrder_;
    std::vector<const Article*> visibleProducts_;
    std::filesystem::path exeDirectory_;
    std::wstring catalogueErrorMessage_;

    HFONT headingFont_ = nullptr;
    HFONT tileFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT categoryFont_ = nullptr;
    HFONT actionFont_ = nullptr;
    HFONT moneyFont_ = nullptr;
    HFONT productNameFont_ = nullptr;

    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HPEN panelBorderPen_ = nullptr;

    HDC backBufferDC_ = nullptr;
    HBITMAP backBufferBitmap_ = nullptr;
    HGDIOBJ backBufferOldBmp_ = nullptr;
    int backBufferWidth_ = 0;
    int backBufferHeight_ = 0;

    Layout layout_{};

    HWND summaryLabel_ = nullptr;
    HWND infoLabel_ = nullptr;
    HWND categoryTitle_ = nullptr;
    HWND productTitle_ = nullptr;
    HWND cartTitle_ = nullptr;
    HWND creditTitle_ = nullptr;
    HWND quickTitle_ = nullptr;
    HWND manualLabel_ = nullptr;

    HWND cartList_ = nullptr;
    HWND editModeButton_ = nullptr;
    HWND manualEntry_ = nullptr;
    HWND addCreditButton_ = nullptr;
    HWND undoCreditButton_ = nullptr;
    HWND removeButton_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND payButton_ = nullptr;

    std::vector<HWND> categoryButtons_;
    std::vector<HWND> productButtons_;
    std::vector<HWND> quickAmountButtons_;

    std::vector<double> quickAmounts_;
    std::vector<std::wstring> cartDisplayLines_;

    std::wstring infoText_;
    bool minimalMode_ = false;
    HWND minimalMessageLabel_ = nullptr;
    std::wstring minimalMessage_;

    UINT dpiX_ = 96;
    UINT dpiY_ = 96;

    int selectedCategoryIndex_ = 0;

    double accentPulse_ = 0.5;
    double animationTime_ = 0.0;
    ULONGLONG lastAnimationTick_ = 0;
    bool animationTimerActive_ = false;
    double currentFontScale_ = 1.0;
    bool fullscreen_ = false;
    RECT windowedRect_{};
};
CashSlothGUI::CashSlothGUI(HINSTANCE instance)
    : instance_(instance) {
    INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icex);
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(instance_, modulePath, MAX_PATH);
    exeDirectory_ = std::filesystem::path(modulePath).parent_path();
    style_ = StyleSheet::load(exeDirectory_);
    quickAmounts_ = style_.quickAmounts;
    infoText_ = style_.hero.subtitle;
}

CashSlothGUI::~CashSlothGUI() {
    releaseGdiResources();
}

int CashSlothGUI::run(int nCmdShow) {
    const wchar_t* className = L"CashSlothWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CashSlothGUI::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc)) {
        const DWORD error = GetLastError();
        std::wstringstream stream;
        stream << L"Fensterklasse konnte nicht registriert werden.\nFehler " << error << L":\n"
               << formatWindowsErrorMessage(error);
        DiagnosticsMonitor::instance().recordError(
            "Fensterklasse konnte nicht registriert werden. Fehler " + std::to_string(error));
        MessageBoxW(nullptr, stream.str().c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        return EXIT_FAILURE;
    }

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        className,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        840,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!window) {
        const DWORD error = GetLastError();
        std::wstringstream stream;
        stream << L"Fenster konnte nicht erstellt werden.\nFehler " << error << L":\n"
               << formatWindowsErrorMessage(error);
        DiagnosticsMonitor::instance().recordError(
            "Fenster konnte nicht erstellt werden. Fehler " + std::to_string(error));
        MessageBoxW(nullptr, stream.str().c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        return EXIT_FAILURE;
    }

    ShowWindow(window, nCmdShow);
    UpdateWindow(window);

    MSG msg{};
    int exitCode = EXIT_SUCCESS;
    while (true) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        if (result == 0) {
            exitCode = static_cast<int>(msg.wParam);
        } else {
            exitCode = EXIT_FAILURE;
        }
        break;
    }
    return exitCode;
}

LRESULT CALLBACK CashSlothGUI::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    CashSlothGUI* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        self = static_cast<CashSlothGUI*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = hwnd;
    } else {
        self = reinterpret_cast<CashSlothGUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_CREATE:
            self->onCreate();
            return 0;
        case WM_COMMAND:
            self->onCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_DRAWITEM:
            self->onDrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        case WM_KEYDOWN:
            if (wParam == VK_F11) {
                self->toggleFullscreen();
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(self->onCtlColorStatic(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(self->onCtlColorPanel(reinterpret_cast<HDC>(wParam)));
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->onPaint();
            return 0;
        case WM_NCHITTEST:
            return self->onNcHitTest(lParam);
        case WM_SIZE:
            self->calculateLayout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            self->onTimer(static_cast<UINT_PTR>(wParam));
            return 0;
        case WM_DESTROY:
            self->onDestroy();
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
void CashSlothGUI::onCreate() {
    DiagnosticsMonitor::ScopedTimer timer("onCreate", 60.0);
    initDpiAndResources();
    calculateLayout();
    createPanelTitles();
    createInfoAndSummary();
    createCartArea();
    createCreditPanel();
    createActionButtons();
    loadCatalogue();
    buildCategoryButtons();
    createCategoryFooter();
    rebuildProductButtons();
    refreshCart();
    refreshStatus();
    showInfo(infoText_);

    accentPulse_ = 0.5;
    animationTime_ = 0.0;
    lastAnimationTick_ = GetTickCount64();
    animationTimerActive_ = SetTimer(window_, kAnimationTimerId, 16, nullptr) != 0;
}

void CashSlothGUI::onDestroy() {
    if (animationTimerActive_) {
        KillTimer(window_, kAnimationTimerId);
        animationTimerActive_ = false;
    }
    releaseGdiResources();
    DiagnosticsMonitor::instance().flushSummary();
    PostQuitMessage(0);
}

void CashSlothGUI::onCommand(int controlId, int notificationCode) {
    if (minimalMode_) {
        return;
    }
    if (controlId >= ID_CATEGORY_BASE && controlId < ID_CATEGORY_BASE + static_cast<int>(categoryButtons_.size())) {
        if (notificationCode == BN_CLICKED) {
            selectedCategoryIndex_ = controlId - ID_CATEGORY_BASE;
            updateCategoryHighlight();
            rebuildProductButtons();
        }
        return;
    }

    if (controlId >= ID_PRODUCT_BASE && controlId < ID_PRODUCT_BASE + static_cast<int>(productButtons_.size())) {
        if (notificationCode == BN_CLICKED) {
            int index = controlId - ID_PRODUCT_BASE;
            if (index >= 0 && index < static_cast<int>(visibleProducts_.size())) {
                cart_.add(*visibleProducts_[static_cast<std::size_t>(index)]);
                refreshCart();
                showInfo(L"\"" + toWide(visibleProducts_[static_cast<std::size_t>(index)]->name) + L"\" hinzugefügt");
            }
        }
        return;
    }

    if (controlId >= ID_QUICK_AMOUNT_BASE && controlId < ID_QUICK_AMOUNT_BASE + static_cast<int>(quickAmountButtons_.size())) {
        if (notificationCode == BN_CLICKED) {
            int index = controlId - ID_QUICK_AMOUNT_BASE;
            if (index >= 0 && index < static_cast<int>(quickAmounts_.size())) {
                addCredit(quickAmounts_[static_cast<std::size_t>(index)]);
            }
        }
        return;
    }

    switch (controlId) {
        case ID_CART_LIST:
            if (notificationCode == LBN_DBLCLK) {
                onRemoveCartItem();
            }
            break;
        case ID_BUTTON_ADD_CREDIT:
            if (notificationCode == BN_CLICKED) {
                onAddCredit();
            }
            break;
        case ID_BUTTON_UNDO_CREDIT:
            if (notificationCode == BN_CLICKED) {
                onUndoCredit();
            }
            break;
        case ID_BUTTON_REMOVE_ITEM:
            if (notificationCode == BN_CLICKED) {
                onRemoveCartItem();
            }
            break;
        case ID_BUTTON_CLEAR_CART:
            if (notificationCode == BN_CLICKED) {
                cart_.clear();
                refreshCart();
                showInfo(L"Warenkorb geleert");
            }
            break;
        case ID_BUTTON_PAY:
            if (notificationCode == BN_CLICKED) {
                onPay();
            }
            break;
        default:
            break;
    }
}

void CashSlothGUI::onDrawItem(LPDRAWITEMSTRUCT dis) {
    if (minimalMode_) {
        return;
    }
    if (dis->CtlType != ODT_BUTTON) {
        return;
    }

    UINT id = dis->CtlID;
    if (id >= ID_CATEGORY_BASE && id < ID_CATEGORY_BASE + static_cast<UINT>(categoryButtons_.size())) {
        drawCategoryButton(dis);
    } else if (id >= ID_PRODUCT_BASE && id < ID_PRODUCT_BASE + static_cast<UINT>(productButtons_.size())) {
        drawProductButton(dis);
    } else if (id >= ID_QUICK_AMOUNT_BASE && id < ID_QUICK_AMOUNT_BASE + static_cast<UINT>(quickAmountButtons_.size())) {
        drawQuickAmountButton(dis);
    } else {
        drawActionButton(dis);
    }
}

HBRUSH CashSlothGUI::onCtlColorStatic(HDC dc, HWND hwnd) {
    if (minimalMode_) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return GetSysColorBrush(COLOR_WINDOW);
    }
    SetBkMode(dc, TRANSPARENT);
    if (hwnd == summaryLabel_) {
        SetTextColor(dc, style_.palette.accentSoft);
    } else {
        SetTextColor(dc, style_.palette.textPrimary);
    }
    return panelBrush_;
}

HBRUSH CashSlothGUI::onCtlColorPanel(HDC dc) {
    if (minimalMode_) {
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return GetSysColorBrush(COLOR_WINDOW);
    }
    SetBkColor(dc, style_.palette.panelBase);
    SetTextColor(dc, style_.palette.textPrimary);
    return panelBrush_;
}

void CashSlothGUI::onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window_, &ps);
    DiagnosticsMonitor::ScopedTimer timer("onPaint", 16.0);

    if (minimalMode_) {
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));
        EndPaint(window_, &ps);
        return;
    }

    RECT rcClient{};
    GetClientRect(window_, &rcClient);
    const int width = rcClient.right - rcClient.left;
    const int height = rcClient.bottom - rcClient.top;
    ensureBackBuffer(dc, width, height);
    HDC paintDC = backBufferDC_ ? backBufferDC_ : dc;

    drawBackdrop(paintDC);
    drawPanel(paintDC, layout_.rcCategoryPanel);
    drawPanel(paintDC, layout_.rcProductPanel);
    drawPanel(paintDC, layout_.rcCartPanel);
    drawPanel(paintDC, layout_.rcCartSummary);
    drawPanel(paintDC, layout_.rcCreditPanel);
    drawPanel(paintDC, layout_.rcActionPanel);
    drawCatalogueErrorBanner(paintDC);

    if (paintDC != dc) {
        BitBlt(dc, 0, 0, width, height, paintDC, 0, 0, SRCCOPY);
    }

    EndPaint(window_, &ps);
}

void CashSlothGUI::onTimer(UINT_PTR timerId) {
    if (timerId == kAnimationTimerId) {
        updateAnimation();
    }
}

LRESULT CashSlothGUI::onNcHitTest(LPARAM lParam) {
    if (fullscreen_) {
        return HTCLIENT;
    }

    const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT windowRect{};
    GetWindowRect(window_, &windowRect);

    const LONG frameX = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    const LONG frameY = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    const LONG scaledBorderX = std::max<LONG>(frameX, static_cast<LONG>(std::lround(10.0 * layout_.scale)));
    const LONG scaledBorderY = std::max<LONG>(frameY, static_cast<LONG>(std::lround(10.0 * layout_.scale)));

    const bool onLeft = pt.x < windowRect.left + scaledBorderX;
    const bool onRight = pt.x >= windowRect.right - scaledBorderX;
    const bool onTop = pt.y < windowRect.top + scaledBorderY;
    const bool onBottom = pt.y >= windowRect.bottom - scaledBorderY;

    if (onTop && onLeft) { return HTTOPLEFT; }
    if (onTop && onRight) { return HTTOPRIGHT; }
    if (onBottom && onLeft) { return HTBOTTOMLEFT; }
    if (onBottom && onRight) { return HTBOTTOMRIGHT; }
    if (onLeft) { return HTLEFT; }
    if (onRight) { return HTRIGHT; }
    if (onTop) { return HTTOP; }
    if (onBottom) { return HTBOTTOM; }

    return HTCLIENT;
}
void CashSlothGUI::initDpiAndResources() {
    HDC screen = GetDC(window_);
    dpiX_ = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
    dpiY_ = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
    ReleaseDC(window_, screen);

    backgroundBrush_ = CreateSolidBrush(style_.palette.background);
    panelBrush_ = CreateSolidBrush(style_.palette.panelBase);
}

void CashSlothGUI::releaseGdiResources() {
    if (headingFont_) { DeleteObject(headingFont_); headingFont_ = nullptr; }
    if (tileFont_) { DeleteObject(tileFont_); tileFont_ = nullptr; }
    if (buttonFont_) { DeleteObject(buttonFont_); buttonFont_ = nullptr; }
    if (smallFont_) { DeleteObject(smallFont_); smallFont_ = nullptr; }
    if (categoryFont_) { DeleteObject(categoryFont_); categoryFont_ = nullptr; }
    if (actionFont_) { DeleteObject(actionFont_); actionFont_ = nullptr; }
    if (moneyFont_) { DeleteObject(moneyFont_); moneyFont_ = nullptr; }
    if (productNameFont_) { DeleteObject(productNameFont_); productNameFont_ = nullptr; }
    if (panelBrush_) { DeleteObject(panelBrush_); panelBrush_ = nullptr; }
    if (backgroundBrush_) { DeleteObject(backgroundBrush_); backgroundBrush_ = nullptr; }
    if (panelBorderPen_) { DeleteObject(panelBorderPen_); panelBorderPen_ = nullptr; }
    releaseBackBuffer();
}

void CashSlothGUI::refreshFonts() {
    const double newScale = layout_.fontScale;
    if (headingFont_ && std::abs(newScale - currentFontScale_) < 0.01) {
        return;
    }

    if (headingFont_) { DeleteObject(headingFont_); headingFont_ = nullptr; }
    if (tileFont_) { DeleteObject(tileFont_); tileFont_ = nullptr; }
    if (buttonFont_) { DeleteObject(buttonFont_); buttonFont_ = nullptr; }
    if (smallFont_) { DeleteObject(smallFont_); smallFont_ = nullptr; }
    if (categoryFont_) { DeleteObject(categoryFont_); categoryFont_ = nullptr; }
    if (actionFont_) { DeleteObject(actionFont_); actionFont_ = nullptr; }
    if (moneyFont_) { DeleteObject(moneyFont_); moneyFont_ = nullptr; }
    if (productNameFont_) { DeleteObject(productNameFont_); productNameFont_ = nullptr; }
    if (panelBorderPen_) { DeleteObject(panelBorderPen_); panelBorderPen_ = nullptr; }

    headingFont_ = createFont(style_.typography.heading);
    tileFont_ = createFont(style_.typography.tile);
    buttonFont_ = createFont(style_.typography.button);
    smallFont_ = createFont(style_.typography.body);
    panelBorderPen_ = CreatePen(PS_SOLID, std::max(1, scale(1)), style_.palette.panelBorder);

    currentFontScale_ = newScale;
}

void CashSlothGUI::calculateLayout() {
    DiagnosticsMonitor::ScopedTimer timer("calculateLayout", 8.0);
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    if (width <= 0 || height <= 0) {
        return;
    }

    StyleSheet::Metrics tunedMetrics = style_.metrics;
    std::size_t categoryCount = catalogue_.categories().size();
    std::size_t productCount = 0;
    if (!visibleProducts_.empty()) {
        productCount = visibleProducts_.size();
    } else if (!catalogue_.categories().empty()) {
        const int clampedIndex = std::clamp(selectedCategoryIndex_, 0, static_cast<int>(catalogue_.categories().size()) - 1);
        productCount = catalogue_.categories()[static_cast<std::size_t>(clampedIndex)].articles.size();
    }
    layout_ = computeLayout(tunedMetrics, width, height, quickAmounts_.size(), categoryCount, productCount);
    refreshFonts();

    if (updateAdaptiveLayoutMetrics(tunedMetrics)) {
        layout_ = computeLayout(tunedMetrics, width, height, quickAmounts_.size(), categoryCount, productCount);
        refreshFonts();
    }

    updateAdaptiveFonts();
    if (!visibleProducts_.empty()) {
        updateProductNameFont(computeProductGrid());
    }
    applyLayout();
}

void CashSlothGUI::applyLayout() {
    if (!window_ || minimalMode_) {
        return;
    }

    auto moveToRect = [](HWND handle, const RECT& rect) {
        if (!handle) {
            return;
        }
        MoveWindow(handle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, FALSE);
    };

    auto applyFont = [](HWND handle, HFONT font) {
        if (!handle || !font) {
            return;
        }
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    };

    auto moveToRectOrHide = [&](HWND handle, const RECT& rect) {
        if (!handle) {
            return;
        }
        if (IsRectEmpty(&rect)) {
            ShowWindow(handle, SW_HIDE);
            return;
        }
        ShowWindow(handle, SW_SHOW);
        MoveWindow(handle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, FALSE);
    };

    moveToRect(cartTitle_, layout_.get("title_cart"));
    applyFont(cartTitle_, headingFont_);
    moveToRect(categoryTitle_, layout_.get("title_categories"));
    applyFont(categoryTitle_, headingFont_);
    moveToRect(productTitle_, layout_.get("title_products"));
    applyFont(productTitle_, headingFont_);
    moveToRect(creditTitle_, layout_.get("title_credit"));
    applyFont(creditTitle_, headingFont_);
    moveToRect(quickTitle_, layout_.get("title_quick"));
    applyFont(quickTitle_, headingFont_);
    moveToRect(manualLabel_, layout_.get("title_manual"));
    applyFont(manualLabel_, buttonFont_);

    moveToRect(summaryLabel_, layout_.get("summary_label"));
    applyFont(summaryLabel_, buttonFont_);
    moveToRect(infoLabel_, layout_.get("info_label"));
    applyFont(infoLabel_, smallFont_);

    moveToRect(cartList_, layout_.get("cart_list"));
    applyFont(cartList_, buttonFont_);

    moveToRect(manualEntry_, layout_.get("edit_manual"));
    applyFont(manualEntry_, buttonFont_);

    moveToRect(addCreditButton_, layout_.get("btn_add_credit"));
    moveToRect(undoCreditButton_, layout_.get("btn_undo_credit"));
    applyFont(addCreditButton_, moneyFont_ ? moneyFont_ : buttonFont_);
    applyFont(undoCreditButton_, moneyFont_ ? moneyFont_ : buttonFont_);

    moveToRect(editModeButton_, layout_.get("btn_edit_mode"));
    applyFont(editModeButton_, categoryFont_ ? categoryFont_ : buttonFont_);

    moveToRect(removeButton_, layout_.get("btn_remove"));
    moveToRect(clearButton_, layout_.get("btn_clear"));
    moveToRect(payButton_, layout_.get("btn_pay"));
    HFONT actionFont = actionFont_ ? actionFont_ : buttonFont_;
    applyFont(removeButton_, actionFont);
    applyFont(clearButton_, actionFont);
    applyFont(payButton_, actionFont);

    for (std::size_t i = 0; i < quickAmountButtons_.size(); ++i) {
        const std::string id = "quick_" + std::to_string(i);
        moveToRect(quickAmountButtons_[i], layout_.get(id.c_str()));
        applyFont(quickAmountButtons_[i], moneyFont_ ? moneyFont_ : buttonFont_);
    }

    for (std::size_t i = 0; i < categoryButtons_.size(); ++i) {
        const std::string id = "cat_" + std::to_string(i);
        moveToRectOrHide(categoryButtons_[i], layout_.get(id.c_str()));
        applyFont(categoryButtons_[i], categoryFont_ ? categoryFont_ : buttonFont_);
    }

    for (std::size_t i = 0; i < productButtons_.size(); ++i) {
        const std::string id = "prod_" + std::to_string(i);
        moveToRectOrHide(productButtons_[i], layout_.get(id.c_str()));
        applyFont(productButtons_[i], tileFont_);
    }

}

void CashSlothGUI::updateHeaderVisibility() {
    (void)minimalMode_;
}

void CashSlothGUI::createPanelTitles() {
    const RECT categoryRect = layout_.get("title_categories");
    const RECT productRect = layout_.get("title_products");

    if (!IsRectEmpty(&categoryRect)) {
        ensureSectionTitle(
            categoryTitle_,
            L"Kategorien",
            categoryRect.left,
            categoryRect.top,
            categoryRect.right - categoryRect.left);
    }
    if (!IsRectEmpty(&productRect)) {
        ensureSectionTitle(
            productTitle_,
            L"Produkte",
            productRect.left,
            productRect.top,
            productRect.right - productRect.left);
    }
}

void CashSlothGUI::createInfoAndSummary() {
    const RECT summaryRect = layout_.get("summary_label");
    const RECT infoRect = layout_.get("info_label");

    // Summary (Summe / Kundengeld / Rückgeld)
    summaryLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE,
        summaryRect.left,
        summaryRect.top,
        summaryRect.right - summaryRect.left,
        summaryRect.bottom - summaryRect.top,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    infoLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE,
        infoRect.left,
        infoRect.top,
        infoRect.right - infoRect.left,
        infoRect.bottom - infoRect.top,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(infoLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);
}

void CashSlothGUI::createCategoryFooter() {
    if (!window_) {
        return;
    }

    const RECT buttonRect = layout_.get("btn_edit_mode");

    if (!editModeButton_) {
        editModeButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Edit Mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            buttonRect.left,
            buttonRect.top,
            buttonRect.right - buttonRect.left,
            buttonRect.bottom - buttonRect.top,
            window_,
            nullptr,
            instance_,
            nullptr);
    } else {
        MoveWindow(
            editModeButton_,
            buttonRect.left,
            buttonRect.top,
            buttonRect.right - buttonRect.left,
            buttonRect.bottom - buttonRect.top,
            FALSE);
    }

    if (editModeButton_) {
        SendMessageW(editModeButton_, WM_SETFONT, reinterpret_cast<WPARAM>(categoryFont_ ? categoryFont_ : buttonFont_), FALSE);
    }
}

void CashSlothGUI::createCartArea() {
    const RECT titleRect = layout_.get("title_cart");
    ensureSectionTitle(
        cartTitle_,
        L"Warenkorb",
        titleRect.left,
        titleRect.top,
        titleRect.right - titleRect.left);

    const RECT listRect = layout_.get("cart_list");

    cartList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        listRect.left,
        listRect.top,
        listRect.right - listRect.left,
        listRect.bottom - listRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_CART_LIST),
        instance_,
        nullptr);
    SendMessageW(cartList_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
}

void CashSlothGUI::createCreditPanel() {
    const RECT creditTitleRect = layout_.get("title_credit");
    ensureSectionTitle(
        creditTitle_,
        L"Kundengeld",
        creditTitleRect.left,
        creditTitleRect.top,
        creditTitleRect.right - creditTitleRect.left);

    const RECT quickTitleRect = layout_.get("title_quick");
    ensureSectionTitle(
        quickTitle_,
        L"Schnellbeträge",
        quickTitleRect.left,
        quickTitleRect.top,
        quickTitleRect.right - quickTitleRect.left);

    if (!manualLabel_) {
        const RECT manualTitleRect = layout_.get("title_manual");
        manualLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Manuelle Eingabe",
            WS_CHILD | WS_VISIBLE,
            manualTitleRect.left,
            manualTitleRect.top,
            manualTitleRect.right - manualTitleRect.left,
            manualTitleRect.bottom - manualTitleRect.top,
            window_,
            nullptr,
            instance_,
            nullptr);
    }
    if (manualLabel_) {
        SendMessageW(manualLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }

    const RECT manualRect = layout_.get("edit_manual");
    manualEntry_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
        manualRect.left,
        manualRect.top,
        manualRect.right - manualRect.left,
        manualRect.bottom - manualRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_EDIT_CREDIT),
        instance_,
        nullptr);
    SendMessageW(manualEntry_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    const RECT addRect = layout_.get("btn_add_credit");
    const RECT undoRect = layout_.get("btn_undo_credit");

    addCreditButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Guthaben +",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        addRect.left,
        addRect.top,
        addRect.right - addRect.left,
        addRect.bottom - addRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_ADD_CREDIT),
        instance_,
        nullptr);
    SendMessageW(addCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(moneyFont_ ? moneyFont_ : buttonFont_), FALSE);

    undoCreditButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Rückgängig",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        undoRect.left,
        undoRect.top,
        undoRect.right - undoRect.left,
        undoRect.bottom - undoRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_UNDO_CREDIT),
        instance_,
        nullptr);
    SendMessageW(undoCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(moneyFont_ ? moneyFont_ : buttonFont_), FALSE);

    if (quickAmountButtons_.empty()) {
        for (std::size_t i = 0; i < quickAmounts_.size(); ++i) {
            const std::string id = "quick_" + std::to_string(i);
            const RECT quickRect = layout_.get(id.c_str());
            std::wstring text = L"+" + toWide(formatCurrency(quickAmounts_[i]));
            HWND button = CreateWindowExW(
                0,
                L"BUTTON",
                text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                quickRect.left,
                quickRect.top,
                quickRect.right - quickRect.left,
                quickRect.bottom - quickRect.top,
                window_,
                reinterpret_cast<HMENU>(ID_QUICK_AMOUNT_BASE + static_cast<int>(i)),
                instance_,
                nullptr);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(moneyFont_ ? moneyFont_ : buttonFont_), FALSE);
            quickAmountButtons_.push_back(button);
        }
    }
}

void CashSlothGUI::createActionButtons() {
    const RECT removeRect = layout_.get("btn_remove");
    const RECT clearRect = layout_.get("btn_clear");
    const RECT payRect = layout_.get("btn_pay");

    removeButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Artikel entfernen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        removeRect.left,
        removeRect.top,
        removeRect.right - removeRect.left,
        removeRect.bottom - removeRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_REMOVE_ITEM),
        instance_,
        nullptr);
    SendMessageW(removeButton_, WM_SETFONT, reinterpret_cast<WPARAM>(actionFont_ ? actionFont_ : buttonFont_), FALSE);

    clearButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Warenkorb leeren",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        clearRect.left,
        clearRect.top,
        clearRect.right - clearRect.left,
        clearRect.bottom - clearRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_CLEAR_CART),
        instance_,
        nullptr);
    SendMessageW(clearButton_, WM_SETFONT, reinterpret_cast<WPARAM>(actionFont_ ? actionFont_ : buttonFont_), FALSE);

    payButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Bezahlen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        payRect.left,
        payRect.top,
        payRect.right - payRect.left,
        payRect.bottom - payRect.top,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_PAY),
        instance_,
        nullptr);
    SendMessageW(payButton_, WM_SETFONT, reinterpret_cast<WPARAM>(actionFont_ ? actionFont_ : buttonFont_), FALSE);
}

void CashSlothGUI::toggleFullscreen() {
    if (!window_) {
        return;
    }

    if (!fullscreen_) {
        GetWindowRect(window_, &windowedRect_);

        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &mi)) {
            const DWORD style = GetWindowLongW(window_, GWL_STYLE);
            SetWindowLongW(window_, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(
                window_,
                HWND_TOP,
                mi.rcMonitor.left,
                mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            fullscreen_ = true;
        }
    } else {
        const DWORD style = GetWindowLongW(window_, GWL_STYLE);
        SetWindowLongW(window_, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPos(
            window_,
            nullptr,
            windowedRect_.left,
            windowedRect_.top,
            windowedRect_.right - windowedRect_.left,
            windowedRect_.bottom - windowedRect_.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullscreen_ = false;
    }

    calculateLayout();
}

void CashSlothGUI::loadCatalogue() {
    DiagnosticsMonitor::ScopedTimer timer("loadCatalogue", 50.0);
    const std::vector<std::filesystem::path> candidates = {
        exeDirectory_ / "assets" / "cash_sloth_catalog.json",
        exeDirectory_ / "assets" / "catalog.json",
        exeDirectory_ / "cash_sloth_catalog.json",
        exeDirectory_ / "cash_sloth_catalog_v25.11.json",
        exeDirectory_ / "cash_sloth_catalog_v25.10.json",
        exeDirectory_ / "konfiguration.json",
        exeDirectory_ / "Configs" / "konfiguration.json",
        exeDirectory_ / "configs" / "konfiguration.json"
    };

    bool loaded = false;
    for (const auto& candidate : candidates) {
        if (catalogue_.loadFromFile(candidate)) {
            infoText_ = std::wstring(L"Katalog geladen aus: ") + candidate.wstring();
            catalogueErrorMessage_.clear();
            DiagnosticsMonitor::instance().recordInfo(
                std::string("Katalog geladen aus: ") + candidate.string());
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        catalogue_.loadDefault();
        infoText_ = L"Standardkatalog geladen (assets/cash_sloth_catalog.json nicht gefunden).";
        catalogueErrorMessage_ = L"Produktkatalog konnte nicht geladen werden. Es wird ein Standardkatalog verwendet.";
        DiagnosticsMonitor::instance().recordWarning(
            "Produktkatalog konnte nicht geladen werden, Standardkatalog wird verwendet.");
    }

    updateHeaderVisibility();
    if (window_ && !minimalMode_) {
        calculateLayout();
    }
}

void CashSlothGUI::buildCategoryButtons() {
    DiagnosticsMonitor::ScopedTimer timer("buildCategoryButtons", 12.0);
    for (HWND button : categoryButtons_) {
        DestroyWindow(button);
    }
    categoryButtons_.clear();
    categoryOrder_.clear();

    const auto& categories = catalogue_.categories();
    categoryOrder_.reserve(categories.size());

    const RECT categoryTitleRect = layout_.get("title_categories");
    const RECT productTitleRect = layout_.get("title_products");
    ensureSectionTitle(
        categoryTitle_,
        L"Kategorien",
        categoryTitleRect.left,
        categoryTitleRect.top,
        categoryTitleRect.right - categoryTitleRect.left);
    ensureSectionTitle(
        productTitle_,
        L"Produkte",
        productTitleRect.left,
        productTitleRect.top,
        productTitleRect.right - productTitleRect.left);

    for (std::size_t i = 0; i < categories.size(); ++i) {
        const std::string id = "cat_" + std::to_string(categoryOrder_.size());
        const RECT buttonRect = layout_.get(id.c_str());
        if (IsRectEmpty(&buttonRect)) {
            break;
        }
        categoryOrder_.push_back(&categories[i]);
        std::wstring text = toWide(categories[i].name);
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            1,
            1,
            window_,
            reinterpret_cast<HMENU>(ID_CATEGORY_BASE + static_cast<int>(i)),
            instance_,
            nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(categoryFont_ ? categoryFont_ : buttonFont_), FALSE);
        categoryButtons_.push_back(button);
    }

    if (selectedCategoryIndex_ >= static_cast<int>(categoryButtons_.size())) {
        selectedCategoryIndex_ = 0;
    }

    updateCategoryHighlight();
    applyLayout();
}

void CashSlothGUI::rebuildProductButtons() {
    DiagnosticsMonitor::ScopedTimer timer("rebuildProductButtons", 14.0);
    for (HWND button : productButtons_) {
        DestroyWindow(button);
    }
    productButtons_.clear();
    visibleProducts_.clear();

    if (categoryOrder_.empty()) {
        return;
    }

    const Category* category = categoryOrder_[static_cast<std::size_t>(selectedCategoryIndex_)];
    visibleProducts_.reserve(category->articles.size());

    for (const Article& article : category->articles) {
        const std::string id = "prod_" + std::to_string(visibleProducts_.size());
        const RECT buttonRect = layout_.get(id.c_str());
        if (IsRectEmpty(&buttonRect)) {
            break;
        }
        visibleProducts_.push_back(&article);
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            1,
            1,
            window_,
            reinterpret_cast<HMENU>(ID_PRODUCT_BASE + static_cast<int>(visibleProducts_.size() - 1)),
            instance_,
            nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
        productButtons_.push_back(button);
    }

    const ProductGridMetrics grid = computeProductGrid();
    updateProductNameFont(grid);
    applyLayout();
}

void CashSlothGUI::updateCategoryHighlight() {
    for (HWND button : categoryButtons_) {
        InvalidateRect(button, nullptr, TRUE);
    }
}

void CashSlothGUI::refreshCart() {
    DiagnosticsMonitor::ScopedTimer timer("refreshCart", 6.0);
    if (minimalMode_) {
        return;
    }
    SendMessageW(cartList_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(cartList_, LB_RESETCONTENT, 0, 0);
    cartDisplayLines_.clear();

    const auto& items = cart_.items();
    std::size_t index = 1;
    for (const CartItem& item : items) {
        std::wstringstream ws;
        ws << index << L". " << toWide(item.article->name) << L"  x" << item.quantity
           << L"  " << toWide(formatCurrency(item.article->price * static_cast<double>(item.quantity)));
        const std::wstring line = ws.str();
        SendMessageW(cartList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        ++index;
    }
    SendMessageW(cartList_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(cartList_, nullptr, TRUE);
    refreshStatus();

    if (undoCreditButton_) {
        EnableWindow(undoCreditButton_, cart_.hasCreditHistory() ? TRUE : FALSE);
    }
}

void CashSlothGUI::refreshStatus() {
    if (minimalMode_) {
        return;
    }
    std::wstring summary = L"Summe: " + toWide(formatCurrency(cart_.total()));
    summary += L"    Kundengeld: " + toWide(formatCurrency(cart_.credit()));
    summary += L"    Rückgeld: " + toWide(formatCurrency(cart_.change()));
    summary += L"    Build " + std::wstring(kAppVersion);
    SetWindowTextW(summaryLabel_, summary.c_str());
}

void CashSlothGUI::showInfo(const std::wstring& text) {
    infoText_ = text;
    if (minimalMode_) {
        if (minimalMessageLabel_) {
            SetWindowTextW(minimalMessageLabel_, minimalMessage_.c_str());
        }
        return;
    }
    if (infoLabel_) {
        SetWindowTextW(infoLabel_, text.c_str());
    }
}

void CashSlothGUI::addCredit(double amount) {
    if (minimalMode_) {
        return;
    }
    cart_.addCredit(amount);
    refreshCart();
    std::wstring message = L"Kundengeld +" + toWide(formatCurrency(amount));
    showInfo(message);
}

void CashSlothGUI::onAddCredit() {
    if (minimalMode_) {
        return;
    }
    wchar_t buffer[64]{};
    GetWindowTextW(manualEntry_, buffer, static_cast<int>(std::size(buffer)));
    std::string text = toNarrow(buffer);
    const std::optional<double> amount = parseAmount(text);
    if (!amount.has_value() || amount.value() <= 0.0) {
        MessageBoxW(window_, L"Bitte einen gültigen Betrag eingeben.", L"Hinweis", MB_ICONWARNING | MB_OK);
        SetFocus(manualEntry_);
        return;
    }
    SetWindowTextW(manualEntry_, L"");
    addCredit(amount.value());
    SetFocus(manualEntry_);
}

void CashSlothGUI::onUndoCredit() {
    if (minimalMode_) {
        return;
    }
    const auto undone = cart_.undoCredit();
    if (!undone.has_value()) {
        MessageBoxW(window_, L"Keine Kundengeldbuchung vorhanden.", L"Hinweis", MB_ICONINFORMATION | MB_OK);
        return;
    }
    refreshCart();
    std::wstring message = L"Kundengeld -" + toWide(formatCurrency(undone.value()));
    showInfo(message);
}

void CashSlothGUI::onRemoveCartItem() {
    if (minimalMode_) {
        return;
    }
    const int selection = static_cast<int>(SendMessageW(cartList_, LB_GETCURSEL, 0, 0));
    if (selection == LB_ERR) {
        MessageBoxW(window_, L"Bitte eine Position im Warenkorb auswählen.", L"Hinweis", MB_ICONINFORMATION | MB_OK);
        return;
    }
    cart_.remove(static_cast<std::size_t>(selection));
    refreshCart();
    showInfo(L"Position entfernt");
}

void CashSlothGUI::onPay() {
    if (minimalMode_) {
        return;
    }
    if (cart_.empty()) {
        MessageBoxW(window_, L"Der Warenkorb ist leer.", L"Hinweis", MB_ICONINFORMATION | MB_OK);
        return;
    }
    const double total = cart_.total();
    if (cart_.credit() + 1e-9 < total) {
        std::wstring message = L"Kundengeld nicht ausreichend.\nFehlender Betrag: ";
        message += toWide(formatCurrency(total - cart_.credit()));
        MessageBoxW(window_, message.c_str(), L"Hinweis", MB_ICONWARNING | MB_OK);
        return;
    }
    double change = cart_.change();
    std::wstring message = L"Zahlung erfolgreich!\nRückgeld: " + toWide(formatCurrency(change));
    MessageBoxW(window_, message.c_str(), L"Bezahlen", MB_ICONINFORMATION | MB_OK);
    cart_.clear();
    refreshCart();
    showInfo(L"Vielen Dank! Zahlung abgeschlossen.");
}
void CashSlothGUI::drawCategoryButton(LPDRAWITEMSTRUCT dis) {
    int index = static_cast<int>(dis->CtlID - ID_CATEGORY_BASE);
    const bool selected = index == selectedCategoryIndex_;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    COLORREF base = selected ? style_.palette.accent : style_.palette.tileBase;
    if (pressed) {
        base = darken(base, selected ? 0.18 : 0.12);
    } else if (selected) {
        base = lighten(base, 0.04);
    }
    std::wstring text;
    if (index >= 0 && index < static_cast<int>(categoryOrder_.size())) {
        text = toWide(categoryOrder_[static_cast<std::size_t>(index)]->name);
    }
    HFONT categoryFont = categoryFont_ ? categoryFont_ : buttonFont_;
    drawRoundedButton(dis, base, style_.palette.textPrimary, text, categoryFont, true);
}

void CashSlothGUI::drawProductButton(LPDRAWITEMSTRUCT dis) {
    COLORREF base = style_.palette.tileRaised;
    if (dis->itemState & ODS_SELECTED) {
        base = darken(base, 0.12);
    }
    drawRoundedButton(dis, base, style_.palette.textPrimary, L"", tileFont_, false);

    int index = static_cast<int>(dis->CtlID - ID_PRODUCT_BASE);
    if (index < 0 || index >= static_cast<int>(visibleProducts_.size())) {
        return;
    }

    const Article* article = visibleProducts_[static_cast<std::size_t>(index)];
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    InflateRect(&rc, -scale(16), -scale(14));

    RECT nameRect = rc;
    nameRect.bottom -= scale(38);
    RECT priceRect = rc;
    priceRect.top = nameRect.bottom;

    HFONT nameFont = productNameFont_ ? productNameFont_ : tileFont_;
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, nameFont));
    SetTextColor(dc, style_.palette.textPrimary);
    SetBkMode(dc, TRANSPARENT);
    const std::wstring name = toWide(article->name);
    DrawTextW(dc, name.c_str(), -1, &nameRect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);

    SelectObject(dc, buttonFont_);
    SetTextColor(dc, style_.palette.accentSoft);
    const std::wstring price = toWide(formatCurrency(article->price));
    DrawTextW(dc, price.c_str(), -1, &priceRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, oldFont);
}

void CashSlothGUI::drawQuickAmountButton(LPDRAWITEMSTRUCT dis) {
    COLORREF base = style_.palette.quickBase;
    if (dis->itemState & ODS_SELECTED) {
        base = darken(base, 0.12);
    }
    wchar_t buffer[64]{};
    GetWindowTextW(dis->hwndItem, buffer, static_cast<int>(std::size(buffer)));
    HFONT moneyFont = moneyFont_ ? moneyFont_ : buttonFont_;
    drawRoundedButton(dis, base, style_.palette.textPrimary, buffer, moneyFont, true);
}

void CashSlothGUI::drawActionButton(LPDRAWITEMSTRUCT dis) {
    HWND hwnd = dis->hwndItem;
    COLORREF base = (hwnd == payButton_) ? style_.palette.success : style_.palette.actionBase;
    if (dis->itemState & ODS_SELECTED) {
        base = darken(base, hwnd == payButton_ ? 0.25 : 0.15);
    }
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    HFONT actionFont = actionFont_ ? actionFont_ : buttonFont_;
    drawRoundedButton(dis, base, style_.palette.textPrimary, buffer, actionFont, true);
}

void CashSlothGUI::drawRoundedButton(LPDRAWITEMSTRUCT dis, COLORREF baseColor, COLORREF textColor, const std::wstring& fallbackText, HFONT font, bool drawText) {
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const int radius = scale(style_.metrics.buttonRadius);

    const COLORREF topColor = lighten(baseColor, 0.08);
    const COLORREF bottomColor = darken(baseColor, 0.15);

    const int state = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectClipRgn(dc, clip);
    TRIVERTEX vertices[2] = {
        makeVertex(rc.left, rc.top, topColor),
        makeVertex(rc.right, rc.bottom, bottomColor),
    };
    GRADIENT_RECT gradientRect{0, 1};
    GradientFill(dc, vertices, 2, &gradientRect, 1, GRADIENT_FILL_RECT_V);
    RestoreDC(dc, state);
    DeleteObject(clip);

    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN outline = CreatePen(PS_SOLID, scale(1), darken(baseColor, 0.25));
    HGDIOBJ oldPen = SelectObject(dc, outline);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(outline);

    if (!drawText) {
        return;
    }

    const wchar_t* textPtr = nullptr;
    wchar_t buffer[256]{};
    if (!fallbackText.empty()) {
        textPtr = fallbackText.c_str();
    } else {
        GetWindowTextW(dis->hwndItem, buffer, static_cast<int>(std::size(buffer)));
        textPtr = buffer;
    }

    RECT textRect = rc;
    InflateRect(&textRect, -scale(16), -scale(6));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, textPtr, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

void CashSlothGUI::ensureBackBuffer(HDC referenceDC, int width, int height) {
    if (!referenceDC || width <= 0 || height <= 0) {
        releaseBackBuffer();
        return;
    }

    if (backBufferDC_ && width == backBufferWidth_ && height == backBufferHeight_) {
        return;
    }

    releaseBackBuffer();

    backBufferDC_ = CreateCompatibleDC(referenceDC);
    if (!backBufferDC_) {
        return;
    }

    backBufferBitmap_ = CreateCompatibleBitmap(referenceDC, width, height);
    if (!backBufferBitmap_) {
        DeleteDC(backBufferDC_);
        backBufferDC_ = nullptr;
        return;
    }

    backBufferOldBmp_ = SelectObject(backBufferDC_, backBufferBitmap_);
    backBufferWidth_ = width;
    backBufferHeight_ = height;
}

void CashSlothGUI::releaseBackBuffer() {
    if (backBufferDC_) {
        if (backBufferOldBmp_) {
            SelectObject(backBufferDC_, backBufferOldBmp_);
            backBufferOldBmp_ = nullptr;
        }
        if (backBufferBitmap_) {
            DeleteObject(backBufferBitmap_);
            backBufferBitmap_ = nullptr;
        }
        DeleteDC(backBufferDC_);
        backBufferDC_ = nullptr;
    }
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

void CashSlothGUI::drawPanel(HDC dc, const RECT& area) const {
    const int radius = scale(style_.metrics.panelRadius);
    const int state = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(area.left, area.top, area.right, area.bottom, radius, radius);
    SelectClipRgn(dc, clip);
    TRIVERTEX vertices[2] = {
        makeVertex(area.left, area.top, lighten(style_.palette.panelBase, style_.glassStrength)),
        makeVertex(area.right, area.bottom, darken(style_.palette.panelElevated, style_.glassStrength)),
    };
    GRADIENT_RECT gradientRect{0, 1};
    GradientFill(dc, vertices, 2, &gradientRect, 1, GRADIENT_FILL_RECT_V);
    RestoreDC(dc, state);
    DeleteObject(clip);

    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = SelectObject(dc, panelBorderPen_);
    RoundRect(dc, area.left, area.top, area.right, area.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void CashSlothGUI::updateAnimation() {
    if (!window_) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (lastAnimationTick_ == 0) {
        lastAnimationTick_ = now;
        return;
    }

    const double deltaSeconds = static_cast<double>(now - lastAnimationTick_) / 1000.0;
    lastAnimationTick_ = now;
    animationTime_ += deltaSeconds;

    constexpr double kTwoPi = 6.28318530717958647692;
    const double pulse = 0.5 + 0.5 * std::sin(animationTime_ * kTwoPi * 0.35);
    if (std::fabs(pulse - accentPulse_) < 0.001) {
        return;
    }

    accentPulse_ = std::clamp(pulse, 0.0, 1.0);

    RECT accentArea{
        std::max(layout_.rcClient.left, layout_.rcClient.right - scale(560)),
        layout_.rcClient.top,
        layout_.rcClient.right,
        layout_.rcClient.top + scale(360)
    };
    InvalidateRect(window_, &accentArea, FALSE);
}

void CashSlothGUI::drawBackdrop(HDC dc) const {
    FillRect(dc, &layout_.rcClient, backgroundBrush_);

    TRIVERTEX vertices[2] = {
        makeVertex(layout_.rcClient.left, layout_.rcClient.top, style_.palette.backgroundGlow),
        makeVertex(layout_.rcClient.right, layout_.rcClient.bottom, style_.palette.background),
    };
    GRADIENT_RECT rect{0, 1};
    GradientFill(dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_H);

    RECT accentRect = layout_.rcClient;
    accentRect.left = layout_.rcClient.right - scale(420);
    accentRect.bottom = layout_.rcClient.top + scale(260);
    const double easedPulse = accentPulse_ * accentPulse_ * (3.0 - 2.0 * accentPulse_);
    const int padLeft = scale(140 + static_cast<int>(easedPulse * 60.0));
    const int padTop = scale(140 + static_cast<int>(easedPulse * 80.0));
    const int padRight = scale(80 + static_cast<int>(easedPulse * 40.0));
    const int padBottom = scale(60 + static_cast<int>(easedPulse * 50.0));
    const int state = SaveDC(dc);
    HRGN clip = CreateEllipticRgn(
        accentRect.left - padLeft,
        accentRect.top - padTop,
        accentRect.right + padRight,
        accentRect.bottom + padBottom);
    SelectClipRgn(dc, clip);
    const double glowStrength = std::clamp(style_.accentGlow + (accentPulse_ - 0.5) * 0.25, 0.05, 0.75);
    const COLORREF accentCore = mixColor(style_.palette.accentStrong, style_.palette.accentSoft, easedPulse);
    const COLORREF accentFade = mixColor(accentCore, style_.palette.background, 1.0 - glowStrength);
    TRIVERTEX accentVerts[2] = {
        makeVertex(accentRect.left, accentRect.top, accentCore),
        makeVertex(accentRect.right, accentRect.bottom, accentFade),
    };
    GradientFill(dc, accentVerts, 2, &rect, 1, GRADIENT_FILL_RECT_H);
    RestoreDC(dc, state);
    DeleteObject(clip);
}

void CashSlothGUI::drawCatalogueErrorBanner(HDC dc) const {
    if (catalogueErrorMessage_.empty()) {
        return;
    }

    RECT banner = layout_.rcHeader;
    const int padding = layout_.metrics.gap;
    banner.left += padding;
    banner.right -= padding;
    banner.top += padding;
    banner.bottom -= padding;

    const COLORREF bannerColor = RGB(170, 34, 34);
    const HBRUSH brush = CreateSolidBrush(bannerColor);
    FillRect(dc, &banner, brush);
    DeleteObject(brush);

    RECT textRect = banner;
    const int textPadding = padding;
    textRect.left += textPadding;
    textRect.right -= textPadding;

    const int state = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    if (headingFont_) {
        SelectObject(dc, headingFont_);
    }
    DrawTextW(dc, catalogueErrorMessage_.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(dc, state);
}

HFONT CashSlothGUI::createFont(const StyleSheet::FontSpec& spec) const {
    const int scaledPointSize = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(spec.sizePt) * layout_.fontScale)),
        10,
        44);
    return createFont(spec, scaledPointSize);
}

HFONT CashSlothGUI::createFont(const StyleSheet::FontSpec& spec, int pointSize) const {
    const int clampedPointSize = std::clamp(pointSize, 8, 60);
    const int logicalHeight = -MulDiv(
        clampedPointSize,
        static_cast<int>(dpiY_),
        72);
    return CreateFontW(
        logicalHeight,
        0,
        0,
        0,
        spec.weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        style_.fontFamily.c_str());
}

void CashSlothGUI::ensureSectionTitle(HWND& handle, const std::wstring& text, int x, int y, int width) {
    int height = layout_.titleHeight;
    if (!handle) {
        handle = CreateWindowExW(
            0,
            L"STATIC",
            text.c_str(),
            WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            window_,
            nullptr,
            instance_,
            nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), FALSE);
    } else {
        SetWindowTextW(handle, text.c_str());
        MoveWindow(handle, x, y, width, height, FALSE);
    }
}

int CashSlothGUI::scale(int value) const {
    const double pixelScale = layout_.scale * static_cast<double>(dpiX_) / 96.0;
    return static_cast<int>(std::lround(static_cast<double>(value) * pixelScale));
}

int CashSlothGUI::measureTextWidth(HDC dc, HFONT font, const std::wstring& text) const {
    if (!dc || !font || text.empty()) {
        return 0;
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, oldFont);
    return static_cast<int>(size.cx);
}

int CashSlothGUI::measureFontHeight(HDC dc, HFONT font) const {
    if (!dc || !font) {
        return 0;
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, oldFont);
    return static_cast<int>(metrics.tmHeight);
}

double CashSlothGUI::computeSingleLineFontScale(
    HDC dc,
    const StyleSheet::FontSpec& spec,
    int availableWidth,
    int availableHeight,
    const std::vector<std::wstring>& texts,
    int minPointSize) const {
    if (!dc || texts.empty()) {
        return 1.0;
    }
    const int basePointSize = static_cast<int>(std::lround(static_cast<double>(spec.sizePt) * layout_.fontScale));
    HFONT baseFont = createFont(spec, basePointSize);
    int maxWidth = 0;
    for (const auto& text : texts) {
        maxWidth = std::max(maxWidth, measureTextWidth(dc, baseFont, text));
    }
    const int baseHeight = measureFontHeight(dc, baseFont);
    DeleteObject(baseFont);

    double widthScale = 1.0;
    if (maxWidth > 0 && availableWidth > 0) {
        widthScale = static_cast<double>(availableWidth) / static_cast<double>(maxWidth);
    }
    double heightScale = 1.0;
    if (baseHeight > 0 && availableHeight > 0) {
        heightScale = static_cast<double>(availableHeight) / static_cast<double>(baseHeight);
    }
    const double scale = std::min({1.0, widthScale, heightScale});
    const double minScaled = (minPointSize > 0)
        ? std::min<double>(static_cast<double>(basePointSize), static_cast<double>(minPointSize) * layout_.fontScale)
        : 0.0;
    const double requested = static_cast<double>(basePointSize) * scale;
    if (requested <= 0.0) {
        return 1.0;
    }
    if (requested < minScaled && minScaled > 0.0) {
        return minScaled / static_cast<double>(basePointSize);
    }
    return requested / static_cast<double>(basePointSize);
}

CashSlothGUI::ProductGridMetrics CashSlothGUI::computeProductGrid() const {
    ProductGridMetrics grid{};
    grid.padding = layout_.metrics.gap;
    const RECT area = layout_.get("product_buttons_area");
    const int panelWidth = static_cast<int>(area.right - area.left);
    const int availableWidth = std::max(0, panelWidth - grid.padding * 2);
    const int minTileWidth = scale(160);
    const int maxTileWidth = scale(240);
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
    grid.tileHeight = std::clamp(tileHeight, scale(120), scale(200));
    return grid;
}

void CashSlothGUI::updateAdaptiveFonts() {
    if (!window_) {
        return;
    }

    if (categoryFont_) { DeleteObject(categoryFont_); categoryFont_ = nullptr; }
    if (actionFont_) { DeleteObject(actionFont_); actionFont_ = nullptr; }
    if (moneyFont_) { DeleteObject(moneyFont_); moneyFont_ = nullptr; }

    constexpr int kMinButtonPointSize = 14;
    HDC dc = GetDC(window_);
    if (!dc) {
        return;
    }

    const int textPaddingX = scale(16);
    const int textPaddingY = scale(6);

    std::vector<std::wstring> categoryTexts;
    categoryTexts.reserve(catalogue_.categories().size() + 1);
    for (const auto& category : catalogue_.categories()) {
        categoryTexts.push_back(toWide(category.name));
    }
    categoryTexts.push_back(L"Edit Mode");

    const RECT categoryArea = layout_.get("category_buttons_area");
    const int categoryWidth = categoryArea.right - categoryArea.left;
    const int categoryAvailableWidth = std::max(1, categoryWidth - textPaddingX * 2);
    const int categoryAvailableHeight = std::max(1, layout_.metrics.categoryHeight - textPaddingY * 2);
    const double categoryScale = computeSingleLineFontScale(
        dc,
        style_.typography.button,
        categoryAvailableWidth,
        categoryAvailableHeight,
        categoryTexts,
        kMinButtonPointSize);
    const int categoryPointSize = static_cast<int>(std::lround(
        static_cast<double>(style_.typography.button.sizePt) * layout_.fontScale * categoryScale));
    categoryFont_ = createFont(style_.typography.button, categoryPointSize);

    const int actionPadding = layout_.metrics.gap;
    const int actionWidth = layout_.rcActionPanel.right - layout_.rcActionPanel.left - actionPadding * 2;
    const int actionGap = layout_.metrics.gap;
    const int actionHalfWidth = std::max(1, (actionWidth - actionGap) / 2);
    const int actionAvailableWidth = std::max(1, actionHalfWidth - textPaddingX * 2);
    const int actionAvailableHeight = std::max(1, layout_.metrics.actionButtonHeight - textPaddingY * 2);
    const std::vector<std::wstring> actionTexts = {
        L"Artikel entfernen",
        L"Warenkorb leeren",
        L"Bezahlen"
    };
    const double actionScale = computeSingleLineFontScale(
        dc,
        style_.typography.button,
        actionAvailableWidth,
        actionAvailableHeight,
        actionTexts,
        kMinButtonPointSize);
    const int actionPointSize = static_cast<int>(std::lround(
        static_cast<double>(style_.typography.button.sizePt) * layout_.fontScale * actionScale));
    actionFont_ = createFont(style_.typography.button, actionPointSize);

    const int creditPadding = layout_.metrics.gap;
    const int creditWidth = layout_.rcCreditPanel.right - layout_.rcCreditPanel.left - creditPadding * 2;
    const int creditGap = layout_.metrics.gap;
    const int creditHalfWidth = std::max(1, (creditWidth - creditGap) / 2);

    const int quickCols = (std::max)(1, layout_.metrics.quickColumns);
    const int quickGap = layout_.metrics.gap;
    const RECT quickArea = layout_.get("quick_grid_area");
    const int gridWidth = quickArea.right - quickArea.left;
    const int quickWidth = (quickCols > 0)
        ? std::max(1, (gridWidth - quickGap * (quickCols - 1)) / quickCols)
        : gridWidth;
    const int moneyButtonWidth = std::max(1, std::min(creditHalfWidth, quickWidth));
    const int moneyAvailableWidth = std::max(1, moneyButtonWidth - textPaddingX * 2);
    const int moneyAvailableHeight = std::max(1, layout_.metrics.quickButtonHeight - textPaddingY * 2);

    std::vector<std::wstring> moneyTexts;
    moneyTexts.reserve(quickAmounts_.size() + 2);
    for (double amount : quickAmounts_) {
        moneyTexts.push_back(L"+" + toWide(formatCurrency(amount)));
    }
    moneyTexts.push_back(L"Guthaben +");
    moneyTexts.push_back(L"Rückgängig");

    const double moneyScale = computeSingleLineFontScale(
        dc,
        style_.typography.button,
        moneyAvailableWidth,
        moneyAvailableHeight,
        moneyTexts,
        kMinButtonPointSize);
    const int moneyPointSize = static_cast<int>(std::lround(
        static_cast<double>(style_.typography.button.sizePt) * layout_.fontScale * moneyScale));
    moneyFont_ = createFont(style_.typography.button, moneyPointSize);

    ReleaseDC(window_, dc);
}

void CashSlothGUI::updateProductNameFont(const ProductGridMetrics& grid) {
    if (!window_) {
        return;
    }
    if (productNameFont_) { DeleteObject(productNameFont_); productNameFont_ = nullptr; }
    if (visibleProducts_.empty()) {
        productNameFont_ = createFont(style_.typography.tile);
        return;
    }

    constexpr int kMinTilePointSize = 15;
    const int paddingX = scale(16);
    const int paddingY = scale(14);
    const int priceHeight = scale(38);
    const int availableWidth = std::max(1, grid.tileWidth - paddingX * 2);
    const int availableHeight = std::max(1, grid.tileHeight - paddingY * 2 - priceHeight);

    const int basePointSize = static_cast<int>(std::lround(static_cast<double>(style_.typography.tile.sizePt) * layout_.fontScale));
    const int minPointSize = static_cast<int>(std::min<double>(basePointSize, static_cast<double>(kMinTilePointSize) * layout_.fontScale));

    HDC dc = GetDC(window_);
    if (!dc) {
        return;
    }
    int bestPointSize = basePointSize;
    for (int pointSize = basePointSize; pointSize >= minPointSize; --pointSize) {
        HFONT font = createFont(style_.typography.tile, pointSize);
        HGDIOBJ oldFont = SelectObject(dc, font);
        bool fits = true;
        for (const Article* article : visibleProducts_) {
            if (!article) {
                continue;
            }
            RECT rect{0, 0, availableWidth, availableHeight};
            const std::wstring name = toWide(article->name);
            DrawTextW(dc, name.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            const int height = rect.bottom - rect.top;
            if (height > availableHeight) {
                fits = false;
                break;
            }
        }
        SelectObject(dc, oldFont);
        DeleteObject(font);
        if (fits) {
            bestPointSize = pointSize;
            break;
        }
    }

    ReleaseDC(window_, dc);
    productNameFont_ = createFont(style_.typography.tile, bestPointSize);
}

bool CashSlothGUI::updateAdaptiveLayoutMetrics(StyleSheet::Metrics& metrics) {
    if (!window_ || !buttonFont_) {
        return false;
    }
    bool changed = false;
    HDC dc = GetDC(window_);
    if (!dc) {
        return false;
    }
    const int textPaddingX = scale(16);
    const int columnPadding = layout_.metrics.gap;

    std::vector<std::wstring> categoryTexts;
    categoryTexts.reserve(catalogue_.categories().size() + 1);
    for (const auto& category : catalogue_.categories()) {
        categoryTexts.push_back(toWide(category.name));
    }
    categoryTexts.push_back(L"Edit Mode");

    int maxCategoryWidth = 0;
    for (const auto& text : categoryTexts) {
        maxCategoryWidth = std::max(maxCategoryWidth, measureTextWidth(dc, buttonFont_, text));
    }
    if (maxCategoryWidth > 0) {
        const int requiredButtonWidth = maxCategoryWidth + textPaddingX * 2;
        const int requiredColumnWidthPx = requiredButtonWidth + columnPadding * 2;
        const int desiredLeftUnscaled = static_cast<int>(
            std::lround(static_cast<double>(requiredColumnWidthPx) / layout_.scale));
        const int clampedLeft = std::clamp(desiredLeftUnscaled, metrics.minLeftColumnWidth, metrics.maxLeftColumnWidth);
        if (clampedLeft > metrics.leftColumnWidth) {
            metrics.leftColumnWidth = clampedLeft;
            changed = true;
        }
    }

    const int actionGap = layout_.metrics.gap;
    const std::vector<std::wstring> actionTexts = {
        L"Artikel entfernen",
        L"Warenkorb leeren"
    };
    int maxActionWidth = 0;
    for (const auto& text : actionTexts) {
        maxActionWidth = std::max(maxActionWidth, measureTextWidth(dc, buttonFont_, text));
    }
    const int payWidth = measureTextWidth(dc, buttonFont_, L"Bezahlen");
    if (maxActionWidth > 0 || payWidth > 0) {
        const int actionButtonWidth = static_cast<int>(maxActionWidth + textPaddingX * 2);
        const int actionRowWidth = static_cast<int>(actionButtonWidth * 2 + actionGap);
        const int payButtonWidth = static_cast<int>(payWidth + textPaddingX * 2);
        const int requiredPanelUsableWidth = (std::max)(actionRowWidth, payButtonWidth);
        const int requiredPanelWidth = requiredPanelUsableWidth + columnPadding * 2;
        const int desiredCartListUnscaled = static_cast<int>(
            std::lround(static_cast<double>(requiredPanelWidth) / layout_.scale));
        if (desiredCartListUnscaled > metrics.minCartListWidth) {
            metrics.minCartListWidth = desiredCartListUnscaled;
            changed = true;
        }
    }

    std::vector<std::wstring> moneyTexts;
    moneyTexts.reserve(quickAmounts_.size() + 2);
    for (double amount : quickAmounts_) {
        moneyTexts.push_back(L"+" + toWide(formatCurrency(amount)));
    }
    moneyTexts.push_back(L"Guthaben +");
    moneyTexts.push_back(L"Rückgängig");
    int maxMoneyWidth = 0;
    for (const auto& text : moneyTexts) {
        maxMoneyWidth = std::max(maxMoneyWidth, measureTextWidth(dc, buttonFont_, text));
    }
    if (maxMoneyWidth > 0) {
        const int requiredPaymentUsableWidthPx = maxMoneyWidth + textPaddingX * 2;
        const int requiredPaymentWidthPx = requiredPaymentUsableWidthPx + columnPadding * 2;
        const int desiredPaymentUnscaled = static_cast<int>(
            std::lround(static_cast<double>(requiredPaymentWidthPx) / layout_.scale));
        if (desiredPaymentUnscaled > metrics.minPaymentWidth) {
            metrics.minPaymentWidth = desiredPaymentUnscaled;
            changed = true;
        }
    }

    ReleaseDC(window_, dc);
    return changed;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    try {
        CashSlothGUI app(hInstance);
        return app.run(nCmdShow);
    } catch (const std::exception& exc) {
        std::wstring message = L"Unbehandelte Ausnahme:\n" + toWide(std::string(exc.what()));
        MessageBoxW(nullptr, message.c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
    } catch (...) {
        MessageBoxW(nullptr, L"Unbekannter Fehler ist aufgetreten.", kWindowTitle, MB_ICONERROR | MB_OK);
    }
    return EXIT_FAILURE;
}

#if !defined(UNICODE) && !defined(_UNICODE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif

#endif  // !defined(_WIN32)
