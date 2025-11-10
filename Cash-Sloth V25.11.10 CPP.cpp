#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")

namespace cashsloth {

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() : storage_(nullptr) {}
    JsonValue(std::nullptr_t) : storage_(nullptr) {}
    JsonValue(bool value) : storage_(value) {}
    JsonValue(double value) : storage_(value) {}
    JsonValue(std::string value) : storage_(std::move(value)) {}
    JsonValue(const char* value) : storage_(std::string(value)) {}
    JsonValue(Array value) : storage_(std::move(value)) {}
    JsonValue(Object value) : storage_(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
    bool isBool() const { return std::holds_alternative<bool>(storage_); }
    bool isNumber() const { return std::holds_alternative<double>(storage_); }
    bool isString() const { return std::holds_alternative<std::string>(storage_); }
    bool isArray() const { return std::holds_alternative<Array>(storage_); }
    bool isObject() const { return std::holds_alternative<Object>(storage_); }

    bool asBool(bool fallback = false) const { return isBool() ? std::get<bool>(storage_) : fallback; }
    double asNumber(double fallback = 0.0) const { return isNumber() ? std::get<double>(storage_) : fallback; }
    const std::string& asString() const { return std::get<std::string>(storage_); }
    const Array& asArray() const { return std::get<Array>(storage_); }
    const Object& asObject() const { return std::get<Object>(storage_); }

    Array& asArray() { return std::get<Array>(storage_); }
    Object& asObject() { return std::get<Object>(storage_); }

private:
    Storage storage_;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (cursor_ != text_.size()) {
            error("Unexpected characters after JSON value");
        }
        return value;
    }

private:
    JsonValue parseValue() {
        skipWhitespace();
        if (cursor_ >= text_.size()) {
            error("Unexpected end of input while parsing value");
        }
        char ch = peek();
        switch (ch) {
            case '{':
                return parseObject();
            case '[':
                return parseArray();
            case '"':
                return JsonValue(parseString());
            case 't':
                return parseTrue();
            case 'f':
                return parseFalse();
            case 'n':
                return parseNull();
            default:
                if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
                    return parseNumber();
                }
                error("Unexpected character while parsing value");
        }
    }

    JsonValue parseObject() {
        expect('{');
        skipWhitespace();
        JsonValue::Object object;
        if (consume('}')) {
            return JsonValue(std::move(object));
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                error("Expected string key inside JSON object");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            JsonValue value = parseValue();
            object.emplace(std::move(key), std::move(value));
            skipWhitespace();
            if (consume('}')) {
                break;
            }
            expect(',');
        }
        return JsonValue(std::move(object));
    }

    JsonValue parseArray() {
        expect('[');
        skipWhitespace();
        JsonValue::Array array;
        if (consume(']')) {
            return JsonValue(std::move(array));
        }
        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            expect(',');
        }
        return JsonValue(std::move(array));
    }

    JsonValue parseNumber() {
        const std::size_t start = cursor_;
        if (peek() == '-') {
            advance();
        }
        if (cursor_ >= text_.size()) {
            error("Incomplete number literal");
        }

        if (peek() == '0') {
            advance();
        } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else {
            error("Invalid number literal");
        }

        if (consume('.')) {
            if (cursor_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                error("Expected digit after decimal point");
            }
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (cursor_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                error("Expected digit after exponent marker");
            }
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        const std::string number{text_.substr(start, cursor_ - start)};
        char* end_ptr = nullptr;
        const double value = std::strtod(number.c_str(), &end_ptr);
        if (end_ptr == number.c_str()) {
            error("Failed to convert number literal");
        }
        return JsonValue(value);
    }

    JsonValue parseTrue() {
        expectSequence("true");
        return JsonValue(true);
    }

    JsonValue parseFalse() {
        expectSequence("false");
        return JsonValue(false);
    }

    JsonValue parseNull() {
        expectSequence("null");
        return JsonValue(nullptr);
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (cursor_ < text_.size()) {
            char ch = advance();
            if (ch == '"') {
                break;
            }
            if (ch == '\\') {
                if (cursor_ >= text_.size()) {
                    error("Unterminated escape sequence inside string");
                }
                char esc = advance();
                switch (esc) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        const char32_t codepoint = parseUnicodeEscape();
                        appendUtf8(result, codepoint);
                        break;
                    }
                    default:
                        error("Unknown escape character inside string");
                }
            } else {
                result.push_back(ch);
            }
        }
        return result;
    }

    char32_t parseUnicodeEscape() {
        if (cursor_ + 4 > text_.size()) {
            error("Incomplete unicode escape sequence");
        }
        char32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = advance();
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<char32_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<char32_t>(10 + (ch - 'a'));
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<char32_t>(10 + (ch - 'A'));
            } else {
                error("Invalid hex digit in unicode escape");
            }
        }

        if (value >= 0xD800 && value <= 0xDBFF) {
            if (cursor_ + 2 > text_.size() || text_[cursor_] != '\\' || text_[cursor_ + 1] != 'u') {
                error("Expected low surrogate after high surrogate");
            }
            cursor_ += 2;
            char32_t low = parseUnicodeEscape();
            if (low < 0xDC00 || low > 0xDFFF) {
                error("Invalid low surrogate following high surrogate");
            }
            value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
        }
        return value;
    }

    void appendUtf8(std::string& out, char32_t codepoint) {
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    void skipWhitespace() {
        while (cursor_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[cursor_]))) {
            ++cursor_;
        }
    }

    void expect(char expected) {
        if (cursor_ >= text_.size() || text_[cursor_] != expected) {
            std::ostringstream oss;
            oss << "Expected '" << expected << "' while parsing JSON";
            error(oss.str());
        }
        ++cursor_;
    }

    bool consume(char expected) {
        if (cursor_ < text_.size() && text_[cursor_] == expected) {
            ++cursor_;
            return true;
        }
        return false;
    }

    void expectSequence(const char* literal) {
        while (*literal) {
            expect(*literal++);
        }
    }

    char peek() const {
        if (cursor_ >= text_.size()) {
            return '\0';
        }
        return text_[cursor_];
    }

    char advance() {
        if (cursor_ >= text_.size()) {
            error("Unexpected end of input while reading JSON");
        }
        return text_[cursor_++];
    }

    [[noreturn]] void error(const std::string& message) const {
        std::ostringstream oss;
        oss << "JSON parse error near position " << cursor_ << ": " << message;
        throw std::runtime_error(oss.str());
    }

    std::string_view text_;
    std::size_t cursor_ = 0;
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
inline std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

inline std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

inline std::string formatCurrency(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount << " CHF";
    return oss.str();
}

inline std::optional<double> parseAmount(const std::string& text) {
    std::string cleaned = trim(text);
    cleaned.erase(
        std::remove_if(cleaned.begin(), cleaned.end(), [](unsigned char ch) { return std::isspace(ch); }),
        cleaned.end());
    std::replace(cleaned.begin(), cleaned.end(), ',', '.');
    if (cleaned.empty()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        const double value = std::stod(cleaned, &consumed);
        if (consumed == cleaned.size()) {
            return value;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

inline std::wstring toWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }
    int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

inline std::string toNarrow(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }
    int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

struct StyleSheet {
    struct Palette {
        COLORREF background = RGB(10, 13, 23);
        COLORREF backgroundGlow = RGB(18, 24, 40);
        COLORREF panelBase = RGB(22, 29, 45);
        COLORREF panelElevated = RGB(27, 35, 55);
        COLORREF panelBorder = RGB(41, 52, 79);
        COLORREF accent = RGB(130, 110, 255);
        COLORREF accentStrong = RGB(108, 88, 255);
        COLORREF accentSoft = RGB(176, 190, 255);
        COLORREF textPrimary = RGB(244, 247, 255);
        COLORREF textSecondary = RGB(140, 151, 183);
        COLORREF success = RGB(90, 214, 165);
        COLORREF danger = RGB(244, 128, 144);
        COLORREF tileBase = RGB(35, 44, 67);
        COLORREF tileRaised = RGB(42, 52, 78);
        COLORREF quickBase = RGB(37, 45, 69);
        COLORREF quickPressed = RGB(30, 37, 57);
        COLORREF actionBase = RGB(39, 48, 72);
    } palette;

    struct Metrics {
        int margin = 26;
        int infoHeight = 60;
        int summaryHeight = 52;
        int gap = 20;
        int leftColumnWidth = 280;
        int rightColumnWidth = 340;
        int categoryHeight = 86;
        int categorySpacing = 14;
        int productTileHeight = 148;
        int tileGap = 18;
        int quickButtonHeight = 58;
        int quickColumns = 3;
        int actionButtonHeight = 66;
        int panelRadius = 30;
        int buttonRadius = 22;
    } metrics;

    struct FontSpec {
        int sizePt = 24;
        int weight = FW_NORMAL;
    };

    struct Typography {
        FontSpec heading{30, FW_SEMIBOLD};
        FontSpec tile{26, FW_BOLD};
        FontSpec button{22, FW_SEMIBOLD};
        FontSpec body{18, FW_NORMAL};
    } typography;

    struct HeroCopy {
        std::wstring title = L"Cash-Sloth Aurora Touch";
        std::wstring subtitle = L"Smooth POS Experience inspired by V25.10 Python";
        std::wstring badge = L"Build 25.11.10";
    } hero;

    std::vector<double> quickAmounts{0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0};
    double glassStrength = 0.18;
    double accentGlow = 0.24;
    std::wstring fontFamily = L"Segoe UI";

    static StyleSheet load(const std::filesystem::path& baseDir);

private:
    static std::optional<COLORREF> parseColorValue(const JsonValue& value);
    static std::optional<COLORREF> parseHexColor(const std::string& text);
    static int parseFontWeightToken(const std::string& token);
    static FontSpec parseFontSpec(const JsonValue& node, FontSpec fallback);
};

inline COLORREF mixColor(COLORREF start, COLORREF target, double factor) {
    factor = std::clamp(factor, 0.0, 1.0);
    auto blendChannel = [&](int a, int b) -> int {
        double value = static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * factor;
        return static_cast<int>(std::clamp(std::round(value), 0.0, 255.0));
    };
    return RGB(
        blendChannel(GetRValue(start), GetRValue(target)),
        blendChannel(GetGValue(start), GetGValue(target)),
        blendChannel(GetBValue(start), GetBValue(target)));
}

inline COLORREF lighten(COLORREF color, double factor) {
    return mixColor(color, RGB(255, 255, 255), factor);
}

inline COLORREF darken(COLORREF color, double factor) {
    return mixColor(color, RGB(0, 0, 0), factor);
}

inline TRIVERTEX makeVertex(LONG x, LONG y, COLORREF color) {
    TRIVERTEX vertex{};
    vertex.x = x;
    vertex.y = y;
    vertex.Red = static_cast<COLOR16>(GetRValue(color) << 8);
    vertex.Green = static_cast<COLOR16>(GetGValue(color) << 8);
    vertex.Blue = static_cast<COLOR16>(GetBValue(color) << 8);
    vertex.Alpha = 0;
    return vertex;
}

StyleSheet StyleSheet::load(const std::filesystem::path& baseDir) {
    StyleSheet sheet;
    const auto stylePath = baseDir / "cash_sloth_styles_v25.11.json";
    std::ifstream input(stylePath);
    if (!input.is_open()) {
        return sheet;
    }
    const std::string payload{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    try {
        JsonParser parser(payload);
        const JsonValue root = parser.parse();
        if (!root.isObject()) {
            return sheet;
        }
        const auto& object = root.asObject();

        const auto paletteIt = object.find("palette");
        if (paletteIt != object.end() && paletteIt->second.isObject()) {
            const auto& paletteObj = paletteIt->second.asObject();
            auto colorOr = [&](const char* key, COLORREF fallback) {
                const auto it = paletteObj.find(key);
                if (it == paletteObj.end()) {
                    return fallback;
                }
                const auto parsed = parseColorValue(it->second);
                return parsed.value_or(fallback);
            };
            sheet.palette.background = colorOr("background", sheet.palette.background);
            sheet.palette.backgroundGlow = colorOr("background_glow", sheet.palette.backgroundGlow);
            sheet.palette.panelBase = colorOr("panel_base", sheet.palette.panelBase);
            sheet.palette.panelElevated = colorOr("panel_elevated", sheet.palette.panelElevated);
            sheet.palette.panelBorder = colorOr("panel_border", sheet.palette.panelBorder);
            sheet.palette.accent = colorOr("accent", sheet.palette.accent);
            sheet.palette.accentStrong = colorOr("accent_strong", sheet.palette.accentStrong);
            sheet.palette.accentSoft = colorOr("accent_soft", sheet.palette.accentSoft);
            sheet.palette.textPrimary = colorOr("text_primary", sheet.palette.textPrimary);
            sheet.palette.textSecondary = colorOr("text_secondary", sheet.palette.textSecondary);
            sheet.palette.success = colorOr("success", sheet.palette.success);
            sheet.palette.danger = colorOr("danger", sheet.palette.danger);
            sheet.palette.tileBase = colorOr("tile_base", sheet.palette.tileBase);
            sheet.palette.tileRaised = colorOr("tile_raised", sheet.palette.tileRaised);
            sheet.palette.quickBase = colorOr("quick_base", sheet.palette.quickBase);
            sheet.palette.quickPressed = colorOr("quick_pressed", sheet.palette.quickPressed);
            sheet.palette.actionBase = colorOr("action_base", sheet.palette.actionBase);
        }

        const auto metricsIt = object.find("metrics");
        if (metricsIt != object.end() && metricsIt->second.isObject()) {
            const auto& metricsObj = metricsIt->second.asObject();
            auto intOr = [&](const char* key, int fallback) {
                const auto it = metricsObj.find(key);
                if (it == metricsObj.end() || !it->second.isNumber()) {
                    return fallback;
                }
                return static_cast<int>(std::round(it->second.asNumber()));
            };
            sheet.metrics.margin = intOr("margin", sheet.metrics.margin);
            sheet.metrics.infoHeight = intOr("info_height", sheet.metrics.infoHeight);
            sheet.metrics.summaryHeight = intOr("summary_height", sheet.metrics.summaryHeight);
            sheet.metrics.gap = intOr("gap", sheet.metrics.gap);
            sheet.metrics.leftColumnWidth = intOr("left_column_width", sheet.metrics.leftColumnWidth);
            sheet.metrics.rightColumnWidth = intOr("right_column_width", sheet.metrics.rightColumnWidth);
            sheet.metrics.categoryHeight = intOr("category_height", sheet.metrics.categoryHeight);
            sheet.metrics.categorySpacing = intOr("category_spacing", sheet.metrics.categorySpacing);
            sheet.metrics.productTileHeight = intOr("product_tile_height", sheet.metrics.productTileHeight);
            sheet.metrics.tileGap = intOr("tile_gap", sheet.metrics.tileGap);
            sheet.metrics.quickButtonHeight = intOr("quick_button_height", sheet.metrics.quickButtonHeight);
            sheet.metrics.quickColumns = std::max(1, intOr("quick_columns", sheet.metrics.quickColumns));
            sheet.metrics.actionButtonHeight = intOr("action_button_height", sheet.metrics.actionButtonHeight);
            sheet.metrics.panelRadius = intOr("panel_radius", sheet.metrics.panelRadius);
            sheet.metrics.buttonRadius = intOr("button_radius", sheet.metrics.buttonRadius);
        }

        const auto typographyIt = object.find("typography");
        if (typographyIt != object.end() && typographyIt->second.isObject()) {
            const auto& typoObj = typographyIt->second.asObject();
            auto fontIt = typoObj.find("heading");
            if (fontIt != typoObj.end()) {
                sheet.typography.heading = parseFontSpec(fontIt->second, sheet.typography.heading);
            }
            fontIt = typoObj.find("tile");
            if (fontIt != typoObj.end()) {
                sheet.typography.tile = parseFontSpec(fontIt->second, sheet.typography.tile);
            }
            fontIt = typoObj.find("button");
            if (fontIt != typoObj.end()) {
                sheet.typography.button = parseFontSpec(fontIt->second, sheet.typography.button);
            }
            fontIt = typoObj.find("body");
            if (fontIt != typoObj.end()) {
                sheet.typography.body = parseFontSpec(fontIt->second, sheet.typography.body);
            }
            const auto familyIt = typoObj.find("font_family");
            if (familyIt != typoObj.end() && familyIt->second.isString()) {
                sheet.fontFamily = toWide(familyIt->second.asString());
            }
        }

        const auto quickIt = object.find("quick_amounts");
        if (quickIt != object.end() && quickIt->second.isArray()) {
            std::vector<double> amounts;
            for (const JsonValue& entry : quickIt->second.asArray()) {
                if (entry.isNumber()) {
                    const double value = entry.asNumber();
                    if (value > 0.0) {
                        amounts.push_back(value);
                    }
                }
            }
            if (!amounts.empty()) {
                sheet.quickAmounts = std::move(amounts);
            }
        }

        const auto heroIt = object.find("hero");
        if (heroIt != object.end() && heroIt->second.isObject()) {
            const auto& heroObj = heroIt->second.asObject();
            const auto titleIt = heroObj.find("title");
            if (titleIt != heroObj.end() && titleIt->second.isString()) {
                sheet.hero.title = toWide(titleIt->second.asString());
            }
            const auto subtitleIt = heroObj.find("subtitle");
            if (subtitleIt != heroObj.end() && subtitleIt->second.isString()) {
                sheet.hero.subtitle = toWide(subtitleIt->second.asString());
            }
            const auto badgeIt = heroObj.find("badge");
            if (badgeIt != heroObj.end() && badgeIt->second.isString()) {
                sheet.hero.badge = toWide(badgeIt->second.asString());
            }
        }

        const auto glassIt = object.find("glass_strength");
        if (glassIt != object.end() && glassIt->second.isNumber()) {
            sheet.glassStrength = std::clamp(glassIt->second.asNumber(), 0.05, 0.5);
        }
        const auto glowIt = object.find("accent_glow");
        if (glowIt != object.end() && glowIt->second.isNumber()) {
            sheet.accentGlow = std::clamp(glowIt->second.asNumber(), 0.05, 0.6);
        }
    } catch (const std::exception& exc) {
        std::cerr << "Warnung: Stylesheet konnte nicht geladen werden: " << exc.what() << '\n';
    }
    return sheet;
}

std::optional<COLORREF> StyleSheet::parseColorValue(const JsonValue& value) {
    if (value.isString()) {
        return parseHexColor(value.asString());
    }
    if (value.isArray()) {
        const auto& arr = value.asArray();
        if (arr.size() >= 3 && arr[0].isNumber() && arr[1].isNumber() && arr[2].isNumber()) {
            auto clampChannel = [](double v) -> int {
                return static_cast<int>(std::clamp(std::round(v), 0.0, 255.0));
            };
            return RGB(
                clampChannel(arr[0].asNumber()),
                clampChannel(arr[1].asNumber()),
                clampChannel(arr[2].asNumber()));
        }
    }
    return std::nullopt;
}

std::optional<COLORREF> StyleSheet::parseHexColor(const std::string& text) {
    std::string raw = trim(text);
    if (!raw.empty() && raw.front() == '#') {
        raw.erase(raw.begin());
    }
    if (raw.size() != 6 && raw.size() != 8) {
        return std::nullopt;
    }
    try {
        unsigned long value = std::stoul(raw, nullptr, 16);
        if (raw.size() == 8) {
            value &= 0x00FFFFFF;
        }
        const int r = static_cast<int>((value >> 16) & 0xFF);
        const int g = static_cast<int>((value >> 8) & 0xFF);
        const int b = static_cast<int>(value & 0xFF);
        return RGB(r, g, b);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int StyleSheet::parseFontWeightToken(const std::string& token) {
    const std::string lower = toLower(trim(token));
    if (lower == "thin") {
        return FW_THIN;
    }
    if (lower == "light") {
        return FW_LIGHT;
    }
    if (lower == "medium") {
        return FW_MEDIUM;
    }
    if (lower == "semibold" || lower == "demibold") {
        return FW_SEMIBOLD;
    }
    if (lower == "bold") {
        return FW_BOLD;
    }
    if (lower == "heavy" || lower == "black") {
        return FW_HEAVY;
    }
    return FW_NORMAL;
}

StyleSheet::FontSpec StyleSheet::parseFontSpec(const JsonValue& node, FontSpec fallback) {
    FontSpec spec = fallback;
    if (!node.isObject()) {
        return spec;
    }
    const auto& obj = node.asObject();
    const auto sizeIt = obj.find("size");
    if (sizeIt != obj.end() && sizeIt->second.isNumber()) {
        spec.sizePt = static_cast<int>(std::round(sizeIt->second.asNumber()));
    }
    const auto weightIt = obj.find("weight");
    if (weightIt != obj.end()) {
        if (weightIt->second.isString()) {
            spec.weight = parseFontWeightToken(weightIt->second.asString());
        } else if (weightIt->second.isNumber()) {
            spec.weight = static_cast<int>(std::round(weightIt->second.asNumber()));
        }
    }
    return spec;
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
    void calculateLayout();
    void createInfoAndSummary();
    void createCartArea();
    void createCreditPanel();
    void createActionButtons();
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

    std::wstring infoText_;

    UINT dpiX_ = 96;
    UINT dpiY_ = 96;

    int selectedCategoryIndex_ = 0;

    double accentPulse_ = 0.5;
    double animationTime_ = 0.0;
    ULONGLONG lastAnimationTick_ = 0;
    bool animationTimerActive_ = false;
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
        return 0;
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
        return 0;
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
    SetBkColor(dc, style_.palette.panelBase);
    SetTextColor(dc, style_.palette.textPrimary);
    return panelBrush_;
}

void CashSlothGUI::onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window_, &ps);

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

    headingFont_ = createFont(style_.typography.heading);
    tileFont_ = createFont(style_.typography.tile);
    buttonFont_ = createFont(style_.typography.button);
    smallFont_ = createFont(style_.typography.body);

    backgroundBrush_ = CreateSolidBrush(style_.palette.background);
    panelBrush_ = CreateSolidBrush(style_.palette.panelBase);
    panelBorderPen_ = CreatePen(PS_SOLID, scale(1), style_.palette.panelBorder);
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

void CashSlothGUI::calculateLayout() {
    GetClientRect(window_, &clientRect_);
    const auto& metrics = style_.metrics;
    const int margin = scale(metrics.margin);
    const int infoHeight = scale(metrics.infoHeight);
    const int summaryHeight = scale(metrics.summaryHeight);
    const int gap = scale(metrics.gap);

    int width = clientRect_.right - clientRect_.left;
    int height = clientRect_.bottom - clientRect_.top;

    const int leftWidth = scale(metrics.leftColumnWidth);
    const int rightWidth = scale(metrics.rightColumnWidth);
    int contentTop = margin + infoHeight + gap;
    int contentBottom = height - margin - summaryHeight - gap;
    int contentHeight = contentBottom - contentTop;
    int centerWidth = width - margin * 2 - leftWidth - rightWidth - gap * 2;

    const int reservedCredit = std::max(scale(metrics.quickButtonHeight * 2 + metrics.gap * 4 + 140), scale(180));
    categoryArea_ = {margin, contentTop, margin + leftWidth, contentBottom - reservedCredit};
    creditPanelArea_ = {margin, categoryArea_.bottom + gap, margin + leftWidth, contentBottom};
    productArea_ = {categoryArea_.right + gap, contentTop, categoryArea_.right + gap + centerWidth, contentBottom};
    const int reservedAction = std::max(scale(metrics.actionButtonHeight + metrics.gap * 4 + 80), scale(160));
    cartArea_ = {productArea_.right + gap, contentTop, width - margin, contentBottom - reservedAction};
    actionArea_ = {productArea_.right + gap, cartArea_.bottom + gap, width - margin, contentBottom};
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

void CashSlothGUI::loadCatalogue() {
    const std::vector<std::filesystem::path> candidates = {
        exeDirectory_ / "cash_sloth_catalog_v25.11.json",
        exeDirectory_ / "cash_sloth_catalog_v25.10.json",
        exeDirectory_ / "cash_sloth_catalog.json",
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
        infoText_ = L"Standardkatalog geladen (cash_sloth_catalog_v25.11.json nicht gefunden).";
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
    SendMessageW(cartList_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(cartList_, LB_RESETCONTENT, 0, 0);

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
}

void CashSlothGUI::refreshStatus() {
    std::wstring summary = L"Summe: " + toWide(formatCurrency(cart_.total()));
    summary += L"    Kundengeld: " + toWide(formatCurrency(cart_.credit()));
    summary += L"    Rückgeld: " + toWide(formatCurrency(cart_.change()));
    summary += L"    Build " + std::wstring(kAppVersion);
    SetWindowTextW(summaryLabel_, summary.c_str());
}

void CashSlothGUI::showInfo(const std::wstring& text) {
    infoText_ = text;
    SetWindowTextW(infoLabel_, text.c_str());
}

void CashSlothGUI::addCredit(double amount) {
    cart_.addCredit(amount);
    refreshCart();
    std::wstring message = L"Kundengeld +" + toWide(formatCurrency(amount));
    showInfo(message);
}

void CashSlothGUI::onAddCredit() {
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
    const int logicalHeight = -MulDiv(spec.sizePt, static_cast<int>(dpiY_), 72);
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
    return MulDiv(value, static_cast<int>(dpiX_), 96);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CashSlothGUI app(hInstance);
    return app.run(nCmdShow);
}

#if !defined(UNICODE) && !defined(_UNICODE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif
