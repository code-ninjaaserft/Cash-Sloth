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
#include "cash_sloth_utils.h"

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
    RECT rcFooter{};

    RECT rcCategoryPanel{};
    RECT rcProductPanel{};
    RECT rcCartPanel{};
    RECT rcCartSummary{};
    RECT rcCreditPanel{};
    RECT rcActionPanel{};
    RECT rcQuickGrid{};

    StyleSheet::Metrics metrics{};
    double scale = 1.0;
    double fontScale = 1.0;
    int heroWidth = 0;
    int badgeWidth = 0;
    int infoLabelWidth = 0;
    int titleHeight = 0;
    int titleGap = 0;
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




Layout computeLayout(const StyleSheet::Metrics& metrics, int windowWidth, int windowHeight, std::size_t quickAmountCount) {
    Layout layout{};
    layout.rcClient = {0, 0, windowWidth, windowHeight};

    constexpr double kBaseWidth = 1600.0;
    constexpr double kBaseHeight = 900.0;
    const double sx = static_cast<double>(windowWidth) / kBaseWidth;
    const double sy = static_cast<double>(windowHeight) / kBaseHeight;
    layout.scale = std::clamp(std::min(sx, sy), 0.5, 2.0);
    layout.fontScale = layout.scale;

    auto scaled = [&](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };

    layout.metrics = metrics;
    layout.metrics.margin = scaled(metrics.margin);
    layout.metrics.infoHeight = scaled(metrics.infoHeight);
    layout.metrics.summaryHeight = scaled(metrics.summaryHeight);
    layout.metrics.gap = scaled(metrics.gap);
    layout.metrics.leftColumnWidth = scaled(metrics.leftColumnWidth);
    layout.metrics.rightColumnWidth = scaled(metrics.rightColumnWidth);
    layout.metrics.categoryHeight = scaled(metrics.categoryHeight);
    layout.metrics.categorySpacing = scaled(metrics.categorySpacing);
    layout.metrics.productTileHeight = scaled(metrics.productTileHeight);
    layout.metrics.tileGap = scaled(metrics.tileGap);
    layout.metrics.quickButtonHeight = scaled(metrics.quickButtonHeight);
    layout.metrics.actionButtonHeight = scaled(metrics.actionButtonHeight);
    layout.metrics.panelRadius = scaled(metrics.panelRadius);
    layout.metrics.buttonRadius = scaled(metrics.buttonRadius);

    const int margin = layout.metrics.margin;
    const int infoHeight = layout.metrics.infoHeight;
    const int summaryHeight = layout.metrics.summaryHeight;
    const int gap = layout.metrics.gap;

    layout.rcHeader = {margin, margin, windowWidth - margin, margin + infoHeight};
    layout.rcFooter = {margin, windowHeight - margin - summaryHeight, windowWidth - margin, windowHeight - margin};

    const int headerWidth = layout.rcHeader.right - layout.rcHeader.left;
    layout.badgeWidth = scaled(220);
    layout.infoLabelWidth = scaled(360);
    layout.heroWidth = std::max(scaled(420), headerWidth - layout.badgeWidth - layout.infoLabelWidth - gap * 2);

    layout.titleHeight = scaled(26);
    layout.titleGap = std::max(2, gap / 2);

    const int titleSpace = layout.titleHeight + layout.titleGap;
    const int contentTop = layout.rcHeader.bottom + gap + titleSpace;
    const int contentBottom = layout.rcFooter.top - gap;

    const int availableWidth = std::max(0, windowWidth - margin * 2);
    const int columnGap = gap;
    const int minCategoryWidth = std::max(scaled(200), layout.metrics.leftColumnWidth);
    const int usableWidth = std::max(0, availableWidth - columnGap * 2);
    const double baseUnit = usableWidth / 6.0;

    const int desiredCategory = static_cast<int>(std::lround(baseUnit));
    const int categoriesWidth = std::clamp(desiredCategory, minCategoryWidth, usableWidth);
    const int remainingWidth = std::max(0, usableWidth - categoriesWidth);

    const double remainingUnit = remainingWidth / 5.0;
    const int productsWidth = static_cast<int>(std::lround(remainingUnit * 3.0));
    const int cartWidth = std::max(0, remainingWidth - productsWidth);

    const int quickColumns = (std::max)(1, layout.metrics.quickColumns);
    const int quickRows = (std::max)(1, static_cast<int>((quickAmountCount + quickColumns - 1) / quickColumns));
    const int quickGridHeight = quickRows * layout.metrics.quickButtonHeight + (quickRows - 1) * gap;

    const int creditPadding = gap;
    const int creditHeight = creditPadding
        + layout.metrics.quickButtonHeight  // edit height
        + gap
        + layout.metrics.quickButtonHeight  // add/undo buttons
        + gap
        + layout.titleHeight
        + gap
        + quickGridHeight
        + creditPadding;

    const int actionPadding = gap;
    const int actionHeight = actionPadding
        + layout.metrics.actionButtonHeight
        + gap
        + layout.metrics.actionButtonHeight
        + actionPadding;
    const int summaryHeight = layout.metrics.summaryHeight;
    const int columnStart = margin;
    const int cartLeft = columnStart + categoriesWidth + columnGap + productsWidth + columnGap;
    const int cartRight = cartLeft + cartWidth;
    const int contentHeight = std::max(0, contentBottom - contentTop);

    int cartBottom = contentTop + contentHeight;

    auto consumeSpace = [&](int amount) {
        cartBottom = std::max(contentTop, cartBottom - amount);
    };

    // Place sections from bottom to top so controls stay visible while the list shrinks first.
    RECT rcAction{};
    RECT rcCredit{};
    RECT rcSummary{};

    int actionBottom = cartBottom;
    consumeSpace(actionHeight);
    rcAction = {cartLeft, cartBottom, cartRight, actionBottom};
    consumeSpace(titleSpace + columnGap);

    int creditBottom = cartBottom;
    consumeSpace(creditHeight);
    rcCredit = {cartLeft, cartBottom, cartRight, creditBottom};
    consumeSpace(titleSpace + columnGap);

    int summaryBottom = cartBottom;
    consumeSpace(summaryHeight);
    rcSummary = {cartLeft, cartBottom, cartRight, summaryBottom};
    consumeSpace(columnGap);

    layout.rcCategoryPanel = {columnStart, contentTop, columnStart + categoriesWidth, contentBottom};
    layout.rcProductPanel = {layout.rcCategoryPanel.right + columnGap, contentTop, layout.rcCategoryPanel.right + columnGap + productsWidth, contentBottom};
    layout.rcCartPanel = {cartLeft, contentTop, cartRight, cartBottom};
    layout.rcCartSummary = rcSummary;
    layout.rcCreditPanel = rcCredit;
    layout.rcActionPanel = rcAction;

    layout.rcQuickGrid = {
        layout.rcCreditPanel.left + creditPadding,
        layout.rcCreditPanel.bottom - creditPadding - quickGridHeight,
        layout.rcCreditPanel.right - creditPadding,
        layout.rcCreditPanel.bottom - creditPadding
    };

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
    HFONT createFont(const StyleSheet::FontSpec& spec) const;
    void ensureSectionTitle(HWND& handle, const std::wstring& text, int x, int y, int width);
    int scale(int value) const;
    void updateAnimation();

    HINSTANCE instance_;
    HWND window_ = nullptr;

    StyleSheet style_;
    Catalogue catalogue_;
    Cart cart_;
    std::vector<const Category*> categoryOrder_;
    std::vector<const Article*> visibleProducts_;
    std::filesystem::path exeDirectory_;

    HFONT headingFont_ = nullptr;
    HFONT tileFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HFONT smallFont_ = nullptr;

    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HPEN panelBorderPen_ = nullptr;

    HDC backBufferDC_ = nullptr;
    HBITMAP backBufferBitmap_ = nullptr;
    HGDIOBJ backBufferOldBmp_ = nullptr;
    int backBufferWidth_ = 0;
    int backBufferHeight_ = 0;

    Layout layout_{};

    HWND heroTitleLabel_ = nullptr;
    HWND heroSubtitleLabel_ = nullptr;
    HWND heroBadgeLabel_ = nullptr;
    HWND infoLabel_ = nullptr;
    HWND summaryLabel_ = nullptr;
    HWND categoryTitle_ = nullptr;
    HWND productTitle_ = nullptr;
    HWND cartTitle_ = nullptr;
    HWND creditTitle_ = nullptr;
    HWND quickTitle_ = nullptr;
    HWND actionTitle_ = nullptr;

    HWND cartList_ = nullptr;
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
    initDpiAndResources();
    calculateLayout();
    createInfoAndSummary();
    createCartArea();
    createCreditPanel();
    createActionButtons();
    loadCatalogue();
    buildCategoryButtons();
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
    } else if (hwnd == infoLabel_ || hwnd == heroSubtitleLabel_) {
        SetTextColor(dc, style_.palette.textSecondary);
    } else if (hwnd == heroBadgeLabel_) {
        SetTextColor(dc, style_.palette.accent);
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
    drawPanel(paintDC, layout_.rcHeader);
    drawPanel(paintDC, layout_.rcFooter);
    drawPanel(paintDC, layout_.rcCategoryPanel);
    drawPanel(paintDC, layout_.rcProductPanel);
    drawPanel(paintDC, layout_.rcCartPanel);
    drawPanel(paintDC, layout_.rcCartSummary);
    drawPanel(paintDC, layout_.rcCreditPanel);
    drawPanel(paintDC, layout_.rcActionPanel);

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
    if (panelBorderPen_) { DeleteObject(panelBorderPen_); panelBorderPen_ = nullptr; }

    headingFont_ = createFont(style_.typography.heading);
    tileFont_ = createFont(style_.typography.tile);
    buttonFont_ = createFont(style_.typography.button);
    smallFont_ = createFont(style_.typography.body);
    panelBorderPen_ = CreatePen(PS_SOLID, std::max(1, scale(1)), style_.palette.panelBorder);

    currentFontScale_ = newScale;
}

void CashSlothGUI::calculateLayout() {
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    if (width <= 0 || height <= 0) {
        return;
    }

    layout_ = computeLayout(style_.metrics, width, height, quickAmounts_.size());

    refreshFonts();
    applyLayout();
}

void CashSlothGUI::applyLayout() {
    if (!window_ || minimalMode_) {
        return;
    }

    const int headerHeight = layout_.rcHeader.bottom - layout_.rcHeader.top;
    const int headerPadding = layout_.metrics.gap;
    const int footerPadding = layout_.metrics.gap;
    const int headerInnerLeft = layout_.rcHeader.left + headerPadding;
    const int headerInnerRight = layout_.rcHeader.right - headerPadding;
    const int headerInnerHeight = headerHeight - headerPadding * 2;

    if (heroTitleLabel_) {
        MoveWindow(heroTitleLabel_, headerInnerLeft, layout_.rcHeader.top + headerPadding, layout_.heroWidth, headerInnerHeight / 2, FALSE);
        SendMessageW(heroTitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), FALSE);
    }
    if (heroSubtitleLabel_) {
        MoveWindow(heroSubtitleLabel_, headerInnerLeft, layout_.rcHeader.top + headerPadding + headerInnerHeight / 2 - scale(4), layout_.heroWidth, headerInnerHeight / 2, FALSE);
        SendMessageW(heroSubtitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);
    }
    if (heroBadgeLabel_) {
        MoveWindow(heroBadgeLabel_, headerInnerRight - layout_.badgeWidth, layout_.rcHeader.top + headerPadding, layout_.badgeWidth, headerInnerHeight / 2, FALSE);
        SendMessageW(heroBadgeLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }
    if (infoLabel_) {
        MoveWindow(infoLabel_, headerInnerRight - layout_.infoLabelWidth, layout_.rcHeader.top + headerPadding + headerInnerHeight / 2 - scale(2), layout_.infoLabelWidth, headerInnerHeight / 2, FALSE);
        SendMessageW(infoLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);
    }
    if (summaryLabel_) {
        MoveWindow(
            summaryLabel_,
            layout_.rcCartSummary.left + footerPadding,
            layout_.rcCartSummary.top + footerPadding,
            layout_.rcCartSummary.right - layout_.rcCartSummary.left - footerPadding * 2,
            layout_.rcCartSummary.bottom - layout_.rcCartSummary.top - footerPadding * 2,
            FALSE);
        SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }

    const int titleHeight = layout_.titleHeight;
    const int titleGap = layout_.titleGap;
    ensureSectionTitle(cartTitle_, L"Warenkorb", layout_.rcCartPanel.left, layout_.rcCartPanel.top - titleHeight - titleGap, layout_.rcCartPanel.right - layout_.rcCartPanel.left);
    ensureSectionTitle(categoryTitle_, L"Kategorien", layout_.rcCategoryPanel.left, layout_.rcCategoryPanel.top - titleHeight - titleGap, layout_.rcCategoryPanel.right - layout_.rcCategoryPanel.left);
    ensureSectionTitle(productTitle_, L"Produkte", layout_.rcProductPanel.left, layout_.rcProductPanel.top - titleHeight - titleGap, layout_.rcProductPanel.right - layout_.rcProductPanel.left);
    ensureSectionTitle(creditTitle_, L"Kundengeld", layout_.rcCreditPanel.left, layout_.rcCreditPanel.top - titleHeight - titleGap, layout_.rcCreditPanel.right - layout_.rcCreditPanel.left);
    ensureSectionTitle(actionTitle_, L"Aktionen", layout_.rcActionPanel.left, layout_.rcActionPanel.top - titleHeight - titleGap, layout_.rcActionPanel.right - layout_.rcActionPanel.left);

    if (cartList_) {
        const int padding = layout_.metrics.gap;
        MoveWindow(cartList_, layout_.rcCartPanel.left + padding, layout_.rcCartPanel.top + padding, layout_.rcCartPanel.right - layout_.rcCartPanel.left - padding * 2, layout_.rcCartPanel.bottom - layout_.rcCartPanel.top - padding * 2, FALSE);
        SendMessageW(cartList_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
    }

    if (manualEntry_) {
        const int padding = layout_.metrics.gap;
        const int editTop = layout_.rcCreditPanel.top + padding;
        const int editHeight = layout_.metrics.quickButtonHeight;
        const int width = layout_.rcCreditPanel.right - layout_.rcCreditPanel.left - padding * 2;
        MoveWindow(manualEntry_, layout_.rcCreditPanel.left + padding, editTop, width, editHeight, FALSE);
        SendMessageW(manualEntry_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);

        const int buttonGap = layout_.metrics.gap;
        const int halfWidth = (width - buttonGap) / 2;
        const int buttonHeight = layout_.metrics.quickButtonHeight;

        if (addCreditButton_) {
            MoveWindow(addCreditButton_, layout_.rcCreditPanel.left + padding, editTop + editHeight + buttonGap, halfWidth, buttonHeight, FALSE);
            SendMessageW(addCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        }
        if (undoCreditButton_) {
            MoveWindow(undoCreditButton_, layout_.rcCreditPanel.left + padding + halfWidth + buttonGap, editTop + editHeight + buttonGap, halfWidth, buttonHeight, FALSE);
            SendMessageW(undoCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        }

        const int quickTitleHeight = layout_.titleHeight;
        ensureSectionTitle(quickTitle_, L"Schnellbeträge", layout_.rcCreditPanel.left + padding, layout_.rcQuickGrid.top - quickTitleHeight - buttonGap, width);

        const int quickTop = layout_.rcQuickGrid.top;
        const int quickCols = std::max(1, layout_.metrics.quickColumns);
        const int quickGap = layout_.metrics.gap;
        const int quickWidth = (layout_.rcQuickGrid.right - layout_.rcQuickGrid.left - quickGap * (quickCols - 1)) / quickCols;
        const int quickHeight = layout_.metrics.quickButtonHeight;

        if (quickAmountButtons_.size() != quickAmounts_.size()) {
            for (HWND button : quickAmountButtons_) {
                DestroyWindow(button);
            }
            quickAmountButtons_.clear();
        }

        if (quickAmountButtons_.empty()) {
            for (std::size_t i = 0; i < quickAmounts_.size(); ++i) {
                int col = static_cast<int>(i % quickCols);
                int row = static_cast<int>(i / quickCols);
                int x = layout_.rcQuickGrid.left + col * (quickWidth + quickGap);
                int y = quickTop + row * (quickHeight + quickGap);
                std::wstring text = L"+" + toWide(formatCurrency(quickAmounts_[i]));
                HWND button = CreateWindowExW(
                    0,
                    L"BUTTON",
                    text.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    x,
                    y,
                    quickWidth,
                    quickHeight,
                    window_,
                    reinterpret_cast<HMENU>(ID_QUICK_AMOUNT_BASE + static_cast<int>(i)),
                    instance_,
                    nullptr);
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
                quickAmountButtons_.push_back(button);
            }
        } else {
            for (std::size_t i = 0; i < quickAmountButtons_.size(); ++i) {
                int col = static_cast<int>(i % quickCols);
                int row = static_cast<int>(i / quickCols);
                int x = layout_.rcQuickGrid.left + col * (quickWidth + quickGap);
                int y = quickTop + row * (quickHeight + quickGap);
                MoveWindow(quickAmountButtons_[i], x, y, quickWidth, quickHeight, FALSE);
                SendMessageW(quickAmountButtons_[i], WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
            }
        }
    }

    if (removeButton_ && clearButton_ && payButton_) {
        const int padding = layout_.metrics.gap;
        const int width = layout_.rcActionPanel.right - layout_.rcActionPanel.left - padding * 2;
        const int buttonHeight = layout_.metrics.actionButtonHeight;
        const int gap = layout_.metrics.gap;
        const int halfWidth = (width - gap) / 2;
        const int top = layout_.rcActionPanel.top + padding;

        MoveWindow(removeButton_, layout_.rcActionPanel.left + padding, top, halfWidth, buttonHeight, FALSE);
        MoveWindow(clearButton_, layout_.rcActionPanel.left + padding + halfWidth + gap, top, halfWidth, buttonHeight, FALSE);
        MoveWindow(payButton_, layout_.rcActionPanel.left + padding, top + buttonHeight + gap, width, buttonHeight, FALSE);

        SendMessageW(removeButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        SendMessageW(clearButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        SendMessageW(payButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }

    if (!categoryButtons_.empty()) {
        int buttonHeight = layout_.metrics.categoryHeight;
        int buttonSpacing = layout_.metrics.categorySpacing;
        const int padding = layout_.metrics.gap;
        int width = layout_.rcCategoryPanel.right - layout_.rcCategoryPanel.left - padding * 2;
        int y = layout_.rcCategoryPanel.top + padding;
        for (HWND button : categoryButtons_) {
            MoveWindow(button, layout_.rcCategoryPanel.left + padding, y, width, buttonHeight, FALSE);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
            y += buttonHeight + buttonSpacing;
        }
    }

    if (!productButtons_.empty() && !visibleProducts_.empty()) {
        const int tilePadding = layout_.metrics.gap;
        const int availableWidth = layout_.rcProductPanel.right - layout_.rcProductPanel.left - tilePadding * 2;
        const int maxTileWidth = scale(260);
        const int baseTileWidth = std::max(scale(140), std::min(maxTileWidth, (availableWidth - tilePadding * 3) / 3));
        int columns = std::max(1, availableWidth / (baseTileWidth + tilePadding));
        columns = std::clamp(columns, 1, 4);
        const int tileWidth = std::max(scale(120), std::min(maxTileWidth, (availableWidth - tilePadding * (columns + 1)) / columns));
        int tileHeight = static_cast<int>(std::round(static_cast<double>(tileWidth) * 0.75));
        tileHeight = std::clamp(tileHeight, scale(100), scale(180));

        const int startX = layout_.rcProductPanel.left + tilePadding;
        const int startY = layout_.rcProductPanel.top + tilePadding;
        for (std::size_t i = 0; i < productButtons_.size(); ++i) {
            const int row = static_cast<int>(i / columns);
            const int col = static_cast<int>(i % columns);
            const int x = startX + col * (tileWidth + tilePadding);
            const int y = startY + row * (tileHeight + tilePadding);
            MoveWindow(productButtons_[i], x, y, tileWidth, tileHeight, FALSE);
            SendMessageW(productButtons_[i], WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
        }
    }

}

void CashSlothGUI::createInfoAndSummary() {
    const int headerHeight = layout_.rcHeader.bottom - layout_.rcHeader.top;

    const int heroWidth = layout_.heroWidth;
    heroTitleLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        style_.hero.title.c_str(),
        WS_CHILD | WS_VISIBLE,
        layout_.rcHeader.left,
        layout_.rcHeader.top,
        heroWidth,
        headerHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(heroTitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), FALSE);

    heroSubtitleLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        style_.hero.subtitle.c_str(),
        WS_CHILD | WS_VISIBLE,
        layout_.rcHeader.left,
        layout_.rcHeader.top + headerHeight / 2 - scale(6),
        heroWidth,
        headerHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(heroSubtitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);

    std::wstring badge = style_.hero.badge;
    badge.append(L"  ·  v");
    badge.append(kAppVersion);
    const int badgeWidth = layout_.badgeWidth;
    heroBadgeLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        badge.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        layout_.rcHeader.right - badgeWidth,
        layout_.rcHeader.top,
        badgeWidth,
        headerHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(heroBadgeLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    const int infoLabelWidth = layout_.infoLabelWidth;
    infoLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        layout_.rcHeader.right - infoLabelWidth,
        layout_.rcHeader.top + headerHeight / 2 - scale(4),
        infoLabelWidth,
        headerHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(infoLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);

    summaryLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE,
        layout_.rcCartSummary.left,
        layout_.rcCartSummary.top,
        layout_.rcCartSummary.right - layout_.rcCartSummary.left,
        layout_.rcCartSummary.bottom - layout_.rcCartSummary.top,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
}

void CashSlothGUI::createCartArea() {
    ensureSectionTitle(cartTitle_, L"Warenkorb", layout_.rcCartPanel.left, layout_.rcCartPanel.top - layout_.titleHeight - layout_.titleGap, layout_.rcCartPanel.right - layout_.rcCartPanel.left);

    cartList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        layout_.rcCartPanel.left,
        layout_.rcCartPanel.top,
        layout_.rcCartPanel.right - layout_.rcCartPanel.left,
        layout_.rcCartPanel.bottom - layout_.rcCartPanel.top,
        window_,
        reinterpret_cast<HMENU>(ID_CART_LIST),
        instance_,
        nullptr);
    SendMessageW(cartList_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
}

void CashSlothGUI::createCreditPanel() {
    ensureSectionTitle(creditTitle_, L"Kundengeld", layout_.rcCreditPanel.left, layout_.rcCreditPanel.top - layout_.titleHeight - layout_.titleGap, layout_.rcCreditPanel.right - layout_.rcCreditPanel.left);

    const int padding = layout_.metrics.gap;
    int width = layout_.rcCreditPanel.right - layout_.rcCreditPanel.left - padding * 2;
    int editTop = layout_.rcCreditPanel.top + padding;
    int editHeight = layout_.metrics.quickButtonHeight;

    manualEntry_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
        layout_.rcCreditPanel.left + padding,
        editTop,
        width,
        editHeight,
        window_,
        reinterpret_cast<HMENU>(ID_EDIT_CREDIT),
        instance_,
        nullptr);
    SendMessageW(manualEntry_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);

    int buttonHeight = layout_.metrics.quickButtonHeight;
    int buttonGap = layout_.metrics.gap;
    int halfWidth = (width - buttonGap) / 2;

    addCreditButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Guthaben +",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        layout_.rcCreditPanel.left + padding,
        editTop + editHeight + buttonGap,
        halfWidth,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_ADD_CREDIT),
        instance_,
        nullptr);
    SendMessageW(addCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    undoCreditButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Rückgängig",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        layout_.rcCreditPanel.left + padding + halfWidth + buttonGap,
        editTop + editHeight + buttonGap,
        halfWidth,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_UNDO_CREDIT),
        instance_,
        nullptr);
    SendMessageW(undoCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    int quickTitleHeight = layout_.titleHeight;
    ensureSectionTitle(quickTitle_, L"Schnellbeträge", layout_.rcCreditPanel.left + padding, layout_.rcQuickGrid.top - quickTitleHeight - buttonGap, width);

    for (HWND button : quickAmountButtons_) {
        DestroyWindow(button);
    }
    quickAmountButtons_.clear();

    int quickTop = layout_.rcQuickGrid.top;
    int quickCols = std::max(1, layout_.metrics.quickColumns);
    int quickGap = layout_.metrics.gap;
    int quickWidth = (layout_.rcQuickGrid.right - layout_.rcQuickGrid.left - quickGap * (quickCols - 1)) / quickCols;
    int quickHeight = layout_.metrics.quickButtonHeight;

    for (std::size_t i = 0; i < quickAmounts_.size(); ++i) {
        int col = static_cast<int>(i % quickCols);
        int row = static_cast<int>(i / quickCols);
        int x = layout_.rcQuickGrid.left + col * (quickWidth + quickGap);
        int y = quickTop + row * (quickHeight + quickGap);
        std::wstring text = L"+" + toWide(formatCurrency(quickAmounts_[i]));
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            x,
            y,
            quickWidth,
            quickHeight,
            window_,
            reinterpret_cast<HMENU>(ID_QUICK_AMOUNT_BASE + static_cast<int>(i)),
            instance_,
            nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        quickAmountButtons_.push_back(button);
    }
}

void CashSlothGUI::createActionButtons() {
    ensureSectionTitle(actionTitle_, L"Aktionen", layout_.rcActionPanel.left, layout_.rcActionPanel.top - layout_.titleHeight - layout_.titleGap, layout_.rcActionPanel.right - layout_.rcActionPanel.left);

    int padding = layout_.metrics.gap;
    int width = layout_.rcActionPanel.right - layout_.rcActionPanel.left - padding * 2;
    int buttonHeight = layout_.metrics.actionButtonHeight;
    int gap = layout_.metrics.gap;
    int halfWidth = (width - gap) / 2;
    int top = layout_.rcActionPanel.top + padding;

    removeButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Artikel entfernen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        layout_.rcActionPanel.left + padding,
        top,
        halfWidth,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_REMOVE_ITEM),
        instance_,
        nullptr);
    SendMessageW(removeButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    clearButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Warenkorb leeren",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        layout_.rcActionPanel.left + padding + halfWidth + gap,
        top,
        halfWidth,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_CLEAR_CART),
        instance_,
        nullptr);
    SendMessageW(clearButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    payButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Bezahlen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        layout_.rcActionPanel.left + padding,
        top + buttonHeight + gap,
        width,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_PAY),
        instance_,
        nullptr);
    SendMessageW(payButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
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
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        catalogue_.loadDefault();
        infoText_ = L"Standardkatalog geladen (assets/cash_sloth_catalog.json nicht gefunden).";
    }
}

void CashSlothGUI::buildCategoryButtons() {
    for (HWND button : categoryButtons_) {
        DestroyWindow(button);
    }
    categoryButtons_.clear();
    categoryOrder_.clear();

    const auto& categories = catalogue_.categories();
    categoryOrder_.reserve(categories.size());

    ensureSectionTitle(categoryTitle_, L"Kategorien", layout_.rcCategoryPanel.left, layout_.rcCategoryPanel.top - layout_.titleHeight - layout_.titleGap, layout_.rcCategoryPanel.right - layout_.rcCategoryPanel.left);
    ensureSectionTitle(productTitle_, L"Produkte", layout_.rcProductPanel.left, layout_.rcProductPanel.top - layout_.titleHeight - layout_.titleGap, layout_.rcProductPanel.right - layout_.rcProductPanel.left);

    int buttonHeight = layout_.metrics.categoryHeight;
    int buttonSpacing = layout_.metrics.categorySpacing;
    int width = layout_.rcCategoryPanel.right - layout_.rcCategoryPanel.left - layout_.metrics.gap * 2;
    int y = layout_.rcCategoryPanel.top + layout_.metrics.gap;
    int x = layout_.rcCategoryPanel.left + layout_.metrics.gap;

    for (std::size_t i = 0; i < categories.size(); ++i) {
        categoryOrder_.push_back(&categories[i]);
        std::wstring text = toWide(categories[i].name);
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            x,
            y,
            width,
            buttonHeight,
            window_,
            reinterpret_cast<HMENU>(ID_CATEGORY_BASE + static_cast<int>(i)),
            instance_,
            nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        categoryButtons_.push_back(button);
        y += buttonHeight + buttonSpacing;
    }

    if (selectedCategoryIndex_ >= static_cast<int>(categoryButtons_.size())) {
        selectedCategoryIndex_ = 0;
    }

    updateCategoryHighlight();
}

void CashSlothGUI::rebuildProductButtons() {
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

    const int tilePadding = layout_.metrics.gap;
    const int availableWidth = layout_.rcProductPanel.right - layout_.rcProductPanel.left - tilePadding * 2;
    const int maxTileWidth = scale(260);
    const int baseTileWidth = std::max(scale(140), std::min(maxTileWidth, (availableWidth - tilePadding * 3) / 3));
    int columns = std::max(1, availableWidth / (baseTileWidth + tilePadding));
    columns = std::clamp(columns, 1, 4);
    const int tileWidth = std::max(scale(120), std::min(maxTileWidth, (availableWidth - tilePadding * (columns + 1)) / columns));
    int tileHeight = static_cast<int>(std::round(static_cast<double>(tileWidth) * 0.75));
    tileHeight = std::clamp(tileHeight, scale(100), scale(180));

    const int startX = layout_.rcProductPanel.left + tilePadding;
    const int startY = layout_.rcProductPanel.top + tilePadding;

    for (const Article& article : category->articles) {
        visibleProducts_.push_back(&article);
        const std::size_t index = visibleProducts_.size() - 1;
        const int row = static_cast<int>(index / static_cast<std::size_t>(columns));
        const int col = static_cast<int>(index % static_cast<std::size_t>(columns));
        const int x = startX + col * (tileWidth + tilePadding);
        const int y = startY + row * (tileHeight + tilePadding);

        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            x,
            y,
            tileWidth,
            tileHeight,
            window_,
            reinterpret_cast<HMENU>(ID_PRODUCT_BASE + static_cast<int>(visibleProducts_.size() - 1)),
            instance_,
            nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
        productButtons_.push_back(button);
    }
}

void CashSlothGUI::updateCategoryHighlight() {
    for (HWND button : categoryButtons_) {
        InvalidateRect(button, nullptr, TRUE);
    }
}

void CashSlothGUI::refreshCart() {
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
    SetWindowTextW(infoLabel_, text.c_str());
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
    drawRoundedButton(dis, base, style_.palette.textPrimary, text, buttonFont_, true);
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

    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, tileFont_));
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
    drawRoundedButton(dis, base, style_.palette.textPrimary, buffer, buttonFont_, true);
}

void CashSlothGUI::drawActionButton(LPDRAWITEMSTRUCT dis) {
    HWND hwnd = dis->hwndItem;
    COLORREF base = (hwnd == payButton_) ? style_.palette.success : style_.palette.actionBase;
    if (dis->itemState & ODS_SELECTED) {
        base = darken(base, hwnd == payButton_ ? 0.25 : 0.15);
    }
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    drawRoundedButton(dis, base, style_.palette.textPrimary, buffer, buttonFont_, true);
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

HFONT CashSlothGUI::createFont(const StyleSheet::FontSpec& spec) const {
    const int scaledPointSize = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(spec.sizePt) * layout_.fontScale)),
        10,
        44);
    const int logicalHeight = -MulDiv(
        scaledPointSize,
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
