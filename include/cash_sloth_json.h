#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

    JsonValue parse();

private:
    JsonValue parseValue();
    JsonValue parseObject();
    JsonValue parseArray();
    JsonValue parseNumber();
    JsonValue parseTrue();
    JsonValue parseFalse();
    JsonValue parseNull();
    std::string parseString();

    char peek() const { return text_[cursor_]; }
    void advance() { ++cursor_; }
    bool consume(char expected);
    void expect(char expected);
    void skipWhitespace();
    [[noreturn]] void error(const char* message) const;

    std::string_view text_;
    std::size_t cursor_ = 0;
};

} // namespace cashsloth

