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
    RECT client{};
    RECT infoArea{};
    RECT summaryArea{};
    RECT categoryArea{};
    RECT creditPanelArea{};
    RECT productArea{};
    RECT cartArea{};
    RECT actionArea{};
    StyleSheet::Metrics metrics{};
    double uniformScale = 1.0;
    double fontScale = 1.0;
    int heroWidth = 0;
    int badgeWidth = 0;
    int infoLabelWidth = 0;
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




Layout computeLayout(const StyleSheet::Metrics& metrics, int windowWidth, int windowHeight) {
    Layout layout{};
    layout.client = {0, 0, windowWidth, windowHeight};

    constexpr double kBaseWidth = 1280.0;
    constexpr double kBaseHeight = 720.0;
    layout.uniformScale = std::clamp(
        std::min(windowWidth / kBaseWidth, windowHeight / kBaseHeight),
        0.5,
        3.0);
    layout.fontScale = std::clamp(layout.uniformScale, 0.8, 1.35);

    auto scaled = [&](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.uniformScale));
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
    const int contentWidth = windowWidth - margin * 2;

    layout.infoArea = {margin, margin, windowWidth - margin, margin + infoHeight};
    layout.summaryArea = {margin, windowHeight - margin - summaryHeight, windowWidth - margin, windowHeight - margin};

    layout.heroWidth = contentWidth - scaled(260);
    layout.badgeWidth = scaled(220);
    layout.infoLabelWidth = scaled(360);

    const int leftWidth = layout.metrics.leftColumnWidth;
    const int rightWidth = layout.metrics.rightColumnWidth;

    const int contentTop = margin + infoHeight + gap;
    const int contentBottom = windowHeight - margin - summaryHeight - gap;
    const int centerWidth = windowWidth - margin * 2 - leftWidth - rightWidth - gap * 2;

    const int reservedCredit = std::max(scaled(metrics.quickButtonHeight * 2 + metrics.gap * 4 + 140), scaled(180));
    layout.categoryArea = {margin, contentTop, margin + leftWidth, contentBottom - reservedCredit};
    layout.creditPanelArea = {margin, layout.categoryArea.bottom + gap, margin + leftWidth, contentBottom};
    layout.productArea = {layout.categoryArea.right + gap, contentTop, layout.categoryArea.right + gap + centerWidth, contentBottom};
    const int reservedAction = std::max(scaled(metrics.actionButtonHeight + metrics.gap * 4 + 80), scaled(160));
    layout.cartArea = {layout.productArea.right + gap, contentTop, windowWidth - margin, contentBottom - reservedAction};
    layout.actionArea = {layout.productArea.right + gap, layout.cartArea.bottom + gap, windowWidth - margin, contentBottom};

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
    void drawPanel(HDC dc, const RECT& area) const;
    void drawBackdrop(HDC dc) const;
    RECT categoryPanelRect() const;
    RECT productPanelRect() const;
    RECT cartPanelRect() const;
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

    Layout layout_{};

    RECT clientRect_{};
    RECT categoryArea_{};
    RECT creditPanelArea_{};
    RECT productArea_{};
    RECT cartArea_{};
    RECT actionArea_{};

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
    WINDOWPLACEMENT windowPlacement_{};
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

    const int windowWidth = 1280;
    const int windowHeight = 840;

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        className,
        kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
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
        case WM_SIZE:
            self->calculateLayout();
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
    return backgroundBrush_;
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

    drawBackdrop(dc);

    drawPanel(dc, categoryPanelRect());
    drawPanel(dc, productPanelRect());
    drawPanel(dc, cartPanelRect());

    EndPaint(window_, &ps);
}

void CashSlothGUI::onTimer(UINT_PTR timerId) {
    if (timerId == kAnimationTimerId) {
        updateAnimation();
    }
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

    layout_ = computeLayout(style_.metrics, width, height);
    clientRect_ = layout_.client;
    categoryArea_ = layout_.categoryArea;
    creditPanelArea_ = layout_.creditPanelArea;
    productArea_ = layout_.productArea;
    cartArea_ = layout_.cartArea;
    actionArea_ = layout_.actionArea;

    refreshFonts();
    applyLayout();
}

void CashSlothGUI::applyLayout() {
    if (!window_ || minimalMode_) {
        return;
    }

    const int margin = layout_.metrics.margin;
    const int infoHeight = layout_.metrics.infoHeight;
    const int summaryHeight = layout_.metrics.summaryHeight;
    const int contentWidth = layout_.client.right - layout_.client.left - margin * 2;

    if (heroTitleLabel_) {
        MoveWindow(heroTitleLabel_, margin, margin, layout_.heroWidth, infoHeight / 2, FALSE);
        SendMessageW(heroTitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), FALSE);
    }
    if (heroSubtitleLabel_) {
        MoveWindow(heroSubtitleLabel_, margin, margin + infoHeight / 2 - scale(6), layout_.heroWidth, infoHeight / 2, FALSE);
        SendMessageW(heroSubtitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);
    }
    if (heroBadgeLabel_) {
        MoveWindow(heroBadgeLabel_, layout_.client.right - margin - layout_.badgeWidth, margin, layout_.badgeWidth, infoHeight / 2, FALSE);
        SendMessageW(heroBadgeLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }
    if (infoLabel_) {
        MoveWindow(infoLabel_, layout_.client.right - margin - layout_.infoLabelWidth, margin + infoHeight / 2 - scale(4), layout_.infoLabelWidth, infoHeight / 2, FALSE);
        SendMessageW(infoLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);
    }
    if (summaryLabel_) {
        MoveWindow(summaryLabel_, margin, layout_.client.bottom - margin - summaryHeight, contentWidth, summaryHeight, FALSE);
        SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }

    ensureSectionTitle(cartTitle_, L"Warenkorb", cartArea_.left, cartArea_.top - scale(32), cartArea_.right - cartArea_.left);
    ensureSectionTitle(categoryTitle_, L"Kategorien", categoryArea_.left, categoryArea_.top - scale(32), categoryArea_.right - categoryArea_.left);
    ensureSectionTitle(productTitle_, L"Produkte", productArea_.left, productArea_.top - scale(32), productArea_.right - productArea_.left);
    ensureSectionTitle(creditTitle_, L"Kundengeld", creditPanelArea_.left, creditPanelArea_.top, creditPanelArea_.right - creditPanelArea_.left);
    ensureSectionTitle(actionTitle_, L"Aktionen", actionArea_.left, actionArea_.top - scale(32), actionArea_.right - actionArea_.left);

    if (cartList_) {
        MoveWindow(cartList_, cartArea_.left, cartArea_.top, cartArea_.right - cartArea_.left, cartArea_.bottom - cartArea_.top, FALSE);
        SendMessageW(cartList_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
    }

    if (manualEntry_) {
        const int editTop = creditPanelArea_.top + scale(40);
        const int editHeight = layout_.metrics.quickButtonHeight;
        const int width = creditPanelArea_.right - creditPanelArea_.left;
        MoveWindow(manualEntry_, creditPanelArea_.left, editTop, width, editHeight, FALSE);
        SendMessageW(manualEntry_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);

        const int buttonGap = scale(std::max(8, style_.metrics.gap / 2));
        const int halfWidth = (width - buttonGap) / 2;
        const int buttonHeight = layout_.metrics.quickButtonHeight;

        if (addCreditButton_) {
            MoveWindow(addCreditButton_, creditPanelArea_.left, editTop + editHeight + buttonGap, halfWidth, buttonHeight, FALSE);
            SendMessageW(addCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        }
        if (undoCreditButton_) {
            MoveWindow(undoCreditButton_, creditPanelArea_.left + halfWidth + buttonGap, editTop + editHeight + buttonGap, halfWidth, buttonHeight, FALSE);
            SendMessageW(undoCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        }

        ensureSectionTitle(quickTitle_, L"Schnellbeträge", creditPanelArea_.left, editTop + editHeight + buttonHeight + buttonGap * 2, width);

        const int quickTop = editTop + editHeight + buttonHeight + buttonGap * 3 + scale(24);
        const int quickCols = std::max(1, style_.metrics.quickColumns);
        const int quickGap = buttonGap;
        const int quickWidth = (width - quickGap * (quickCols - 1)) / quickCols;
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
                int x = creditPanelArea_.left + col * (quickWidth + quickGap);
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
                int x = creditPanelArea_.left + col * (quickWidth + quickGap);
                int y = quickTop + row * (quickHeight + quickGap);
                MoveWindow(quickAmountButtons_[i], x, y, quickWidth, quickHeight, FALSE);
                SendMessageW(quickAmountButtons_[i], WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
            }
        }
    }

    if (removeButton_ && clearButton_ && payButton_) {
        const int width = actionArea_.right - actionArea_.left;
        const int buttonHeight = layout_.metrics.actionButtonHeight;
        const int gap = scale(std::max(10, style_.metrics.gap / 2));
        const int halfWidth = (width - gap) / 2;
        const int top = actionArea_.top;

        MoveWindow(removeButton_, actionArea_.left, top, halfWidth, buttonHeight, FALSE);
        MoveWindow(clearButton_, actionArea_.left + halfWidth + gap, top, halfWidth, buttonHeight, FALSE);
        MoveWindow(payButton_, actionArea_.left, top + buttonHeight + gap, width, buttonHeight, FALSE);

        SendMessageW(removeButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        SendMessageW(clearButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
        SendMessageW(payButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
    }

    if (!categoryButtons_.empty()) {
        int buttonHeight = layout_.metrics.categoryHeight;
        int buttonSpacing = layout_.metrics.categorySpacing;
        int width = categoryArea_.right - categoryArea_.left;
        int y = categoryArea_.top;
        for (HWND button : categoryButtons_) {
            MoveWindow(button, categoryArea_.left, y, width, buttonHeight, FALSE);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
            y += buttonHeight + buttonSpacing;
        }
    }

    if (!productButtons_.empty() && !visibleProducts_.empty()) {
        int availableWidth = productArea_.right - productArea_.left;
        int tileGap = layout_.metrics.tileGap;
        int tileHeight = layout_.metrics.productTileHeight;
        int columns = std::max(1, (availableWidth + tileGap) / (scale(220) + tileGap));
        int tileWidth = (availableWidth - tileGap * (columns - 1)) / columns;

        int x = productArea_.left;
        int y = productArea_.top;
        int column = 0;
        for (HWND button : productButtons_) {
            MoveWindow(button, x, y, tileWidth, tileHeight, FALSE);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
            ++column;
            if (column >= columns) {
                column = 0;
                x = productArea_.left;
                y += tileHeight + tileGap;
            } else {
                x += tileWidth + tileGap;
            }
        }
    }

    InvalidateRect(window_, nullptr, TRUE);
}

void CashSlothGUI::createInfoAndSummary() {
    const int margin = scale(style_.metrics.margin);
    const int infoHeight = scale(style_.metrics.infoHeight);
    const int summaryHeight = scale(style_.metrics.summaryHeight);
    const int width = clientRect_.right - clientRect_.left;
    const int contentWidth = width - margin * 2;

    const int heroWidth = contentWidth - scale(260);
    heroTitleLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        style_.hero.title.c_str(),
        WS_CHILD | WS_VISIBLE,
        margin,
        margin,
        heroWidth,
        infoHeight / 2,
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
        margin,
        margin + infoHeight / 2 - scale(6),
        heroWidth,
        infoHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(heroSubtitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), FALSE);

    std::wstring badge = style_.hero.badge;
    badge.append(L"  ·  v");
    badge.append(kAppVersion);
    const int badgeWidth = scale(220);
    heroBadgeLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        badge.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        clientRect_.right - margin - badgeWidth,
        margin,
        badgeWidth,
        infoHeight / 2,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(heroBadgeLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    const int infoLabelWidth = scale(360);
    infoLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        clientRect_.right - margin - infoLabelWidth,
        margin + infoHeight / 2 - scale(4),
        infoLabelWidth,
        infoHeight / 2,
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
        margin,
        clientRect_.bottom - margin - summaryHeight,
        contentWidth,
        summaryHeight,
        window_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);
}

void CashSlothGUI::createCartArea() {
    ensureSectionTitle(cartTitle_, L"Warenkorb", cartArea_.left, cartArea_.top - scale(32), cartArea_.right - cartArea_.left);

    cartList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        cartArea_.left,
        cartArea_.top,
        cartArea_.right - cartArea_.left,
        cartArea_.bottom - cartArea_.top,
        window_,
        reinterpret_cast<HMENU>(ID_CART_LIST),
        instance_,
        nullptr);
    SendMessageW(cartList_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);
}

void CashSlothGUI::createCreditPanel() {
    ensureSectionTitle(creditTitle_, L"Kundengeld", creditPanelArea_.left, creditPanelArea_.top, creditPanelArea_.right - creditPanelArea_.left);

    int width = creditPanelArea_.right - creditPanelArea_.left;
    int editTop = creditPanelArea_.top + scale(40);
    int editHeight = scale(style_.metrics.quickButtonHeight);

    manualEntry_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
        creditPanelArea_.left,
        editTop,
        width,
        editHeight,
        window_,
        reinterpret_cast<HMENU>(ID_EDIT_CREDIT),
        instance_,
        nullptr);
    SendMessageW(manualEntry_, WM_SETFONT, reinterpret_cast<WPARAM>(tileFont_), FALSE);

    int buttonHeight = scale(style_.metrics.quickButtonHeight);
    int buttonGap = scale(std::max(8, style_.metrics.gap / 2));
    int halfWidth = (width - buttonGap) / 2;

    addCreditButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Guthaben +",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        creditPanelArea_.left,
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
        creditPanelArea_.left + halfWidth + buttonGap,
        editTop + editHeight + buttonGap,
        halfWidth,
        buttonHeight,
        window_,
        reinterpret_cast<HMENU>(ID_BUTTON_UNDO_CREDIT),
        instance_,
        nullptr);
    SendMessageW(undoCreditButton_, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont_), FALSE);

    ensureSectionTitle(quickTitle_, L"Schnellbeträge", creditPanelArea_.left, editTop + editHeight + buttonHeight + buttonGap * 2, width);

    for (HWND button : quickAmountButtons_) {
        DestroyWindow(button);
    }
    quickAmountButtons_.clear();

    int quickTop = editTop + editHeight + buttonHeight + buttonGap * 3 + scale(24);
    int quickCols = std::max(1, style_.metrics.quickColumns);
    int quickGap = buttonGap;
    int quickWidth = (width - quickGap * (quickCols - 1)) / quickCols;
    int quickHeight = scale(style_.metrics.quickButtonHeight);

    for (std::size_t i = 0; i < quickAmounts_.size(); ++i) {
        int col = static_cast<int>(i % quickCols);
        int row = static_cast<int>(i / quickCols);
        int x = creditPanelArea_.left + col * (quickWidth + quickGap);
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
    ensureSectionTitle(actionTitle_, L"Aktionen", actionArea_.left, actionArea_.top - scale(32), actionArea_.right - actionArea_.left);

    int width = actionArea_.right - actionArea_.left;
    int buttonHeight = scale(style_.metrics.actionButtonHeight);
    int gap = scale(std::max(10, style_.metrics.gap / 2));
    int halfWidth = (width - gap) / 2;
    int top = actionArea_.top;

    removeButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Artikel entfernen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        actionArea_.left,
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
        actionArea_.left + halfWidth + gap,
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
        actionArea_.left,
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
        windowPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window_, &windowPlacement_);

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
        SetWindowPlacement(window_, &windowPlacement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
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

    ensureSectionTitle(categoryTitle_, L"Kategorien", categoryArea_.left, categoryArea_.top - scale(32), categoryArea_.right - categoryArea_.left);
    ensureSectionTitle(productTitle_, L"Produkte", productArea_.left, productArea_.top - scale(32), productArea_.right - productArea_.left);

    int buttonHeight = scale(style_.metrics.categoryHeight);
    int buttonSpacing = scale(style_.metrics.categorySpacing);
    int width = categoryArea_.right - categoryArea_.left;
    int y = categoryArea_.top;

    for (std::size_t i = 0; i < categories.size(); ++i) {
        categoryOrder_.push_back(&categories[i]);
        std::wstring text = toWide(categories[i].name);
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            categoryArea_.left,
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

    int availableWidth = productArea_.right - productArea_.left;
    int tileGap = scale(style_.metrics.tileGap);
    int tileHeight = scale(style_.metrics.productTileHeight);
    int columns = std::max(1, (availableWidth + tileGap) / (scale(220) + tileGap));
    int tileWidth = (availableWidth - tileGap * (columns - 1)) / columns;

    int x = productArea_.left;
    int y = productArea_.top;
    int column = 0;

    for (const Article& article : category->articles) {
        visibleProducts_.push_back(&article);
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

        ++column;
        if (column >= columns) {
            column = 0;
            x = productArea_.left;
            y += tileHeight + tileGap;
        } else {
            x += tileWidth + tileGap;
        }
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
        std::max(clientRect_.left, clientRect_.right - scale(560)),
        clientRect_.top,
        clientRect_.right,
        clientRect_.top + scale(360)
    };
    InvalidateRect(window_, &accentArea, FALSE);
}

void CashSlothGUI::drawBackdrop(HDC dc) const {
    FillRect(dc, &clientRect_, backgroundBrush_);

    TRIVERTEX vertices[2] = {
        makeVertex(clientRect_.left, clientRect_.top, style_.palette.backgroundGlow),
        makeVertex(clientRect_.right, clientRect_.bottom, style_.palette.background),
    };
    GRADIENT_RECT rect{0, 1};
    GradientFill(dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_H);

    RECT accentRect = clientRect_;
    accentRect.left = clientRect_.right - scale(420);
    accentRect.bottom = clientRect_.top + scale(260);
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

RECT CashSlothGUI::categoryPanelRect() const {
    RECT rect = {
        categoryArea_.left - scale(12),
        categoryArea_.top - scale(40),
        categoryArea_.right + scale(12),
        creditPanelArea_.bottom + scale(20)
    };
    return rect;
}

RECT CashSlothGUI::productPanelRect() const {
    RECT rect = {
        productArea_.left - scale(12),
        productArea_.top - scale(40),
        productArea_.right + scale(12),
        productArea_.bottom + scale(20)
    };
    return rect;
}

RECT CashSlothGUI::cartPanelRect() const {
    RECT rect = {
        cartArea_.left - scale(12),
        cartArea_.top - scale(40),
        cartArea_.right + scale(12),
        actionArea_.bottom + scale(20)
    };
    return rect;
}

HFONT CashSlothGUI::createFont(const StyleSheet::FontSpec& spec) const {
    const int logicalHeight = -MulDiv(
        static_cast<int>(std::lround(static_cast<double>(spec.sizePt) * layout_.fontScale)),
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
    int height = scale(28);
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
    const double pixelScale = layout_.uniformScale * static_cast<double>(dpiX_) / 96.0;
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
