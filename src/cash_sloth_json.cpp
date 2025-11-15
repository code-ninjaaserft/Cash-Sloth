#include "cash_sloth_json.h"

#include <cctype>
#include <string>

namespace {

std::string encodeUtf8FromCodepoint(unsigned codepoint) {
    std::string encoded;
    auto appendByte = [&](unsigned char byte) {
        encoded.push_back(static_cast<char>(byte));
    };
    if (codepoint <= 0x7F) {
        appendByte(static_cast<unsigned char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        appendByte(static_cast<unsigned char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        appendByte(static_cast<unsigned char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        appendByte(static_cast<unsigned char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        appendByte(static_cast<unsigned char>(0x80 | ((codepoint >> 6) & 0x3F)));
        appendByte(static_cast<unsigned char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        appendByte(static_cast<unsigned char>(0xF0 | ((codepoint >> 18) & 0x07)));
        appendByte(static_cast<unsigned char>(0x80 | ((codepoint >> 12) & 0x3F)));
        appendByte(static_cast<unsigned char>(0x80 | ((codepoint >> 6) & 0x3F)));
        appendByte(static_cast<unsigned char>(0x80 | (codepoint & 0x3F)));
    } else {
        throw std::runtime_error("Unsupported Unicode codepoint in JSON escape");
    }
    return encoded;
}

}  // namespace

namespace cashsloth {

JsonValue JsonParser::parse() {
    skipBom();
    skipWhitespace();
    JsonValue value = parseValue();
    skipWhitespace();
    if (cursor_ != text_.size()) {
        error("Unexpected characters after JSON value");
    }
    return value;
}

JsonValue JsonParser::parseValue() {
    skipWhitespace();
    if (cursor_ >= text_.size()) {
        error("Unexpected end of input while parsing value");
    }
    const char ch = peek();
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
            if (ch == '-' || (ch >= '0' && ch <= '9')) {
                return parseNumber();
            }
            error("Unexpected character while parsing value");
    }
}

JsonValue JsonParser::parseObject() {
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

JsonValue JsonParser::parseArray() {
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

JsonValue JsonParser::parseNumber() {
    const std::size_t start = cursor_;
    if (peek() == '-') {
        advance();
    }
    while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    if (cursor_ < text_.size() && peek() == '.') {
        advance();
        while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    if (cursor_ < text_.size() && (peek() == 'e' || peek() == 'E')) {
        advance();
        if (cursor_ < text_.size() && (peek() == '+' || peek() == '-')) {
            advance();
        }
        while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    const double value = std::stod(std::string(text_.substr(start, cursor_ - start)));
    return JsonValue(value);
}

JsonValue JsonParser::parseTrue() {
    if (text_.substr(cursor_, 4) != "true") {
        error("Invalid literal, expected true");
    }
    cursor_ += 4;
    return JsonValue(true);
}

JsonValue JsonParser::parseFalse() {
    if (text_.substr(cursor_, 5) != "false") {
        error("Invalid literal, expected false");
    }
    cursor_ += 5;
    return JsonValue(false);
}

JsonValue JsonParser::parseNull() {
    if (text_.substr(cursor_, 4) != "null") {
        error("Invalid literal, expected null");
    }
    cursor_ += 4;
    return JsonValue(nullptr);
}

std::string JsonParser::parseString() {
    expect('"');
    std::string result;
    while (cursor_ < text_.size()) {
        const char ch = peek();
        advance();
        if (ch == '"') {
            break;
        }
        if (ch == '\\') {
            if (cursor_ >= text_.size()) {
                error("Unexpected end of escape sequence");
            }
            const char escape = peek();
            advance();
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (cursor_ + 4 > text_.size()) {
                        error("Incomplete unicode escape");
                    }
                    const std::string hex(text_.substr(cursor_, 4));
                    cursor_ += 4;
                    const unsigned codepoint = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                    if (codepoint <= 0x7F) {
                        result.push_back(static_cast<char>(codepoint));
                    } else {
                        result += encodeUtf8FromCodepoint(codepoint);
                    }
                    break;
                }
                default:
                    error("Invalid escape sequence in string");
            }
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

bool JsonParser::consume(char expected) {
    if (cursor_ < text_.size() && text_[cursor_] == expected) {
        ++cursor_;
        return true;
    }
    return false;
}

void JsonParser::expect(char expected) {
    if (!consume(expected)) {
        error("Unexpected token while parsing JSON");
    }
}

void JsonParser::skipBom() {
    if (cursor_ == 0 && text_.size() >= 3) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text_.data());
        if (bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            cursor_ = 3;
        }
    }
}

void JsonParser::skipWhitespace() {
    while (cursor_ < text_.size()) {
        const unsigned char ch = static_cast<unsigned char>(text_[cursor_]);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++cursor_;
            continue;
        }
        if (ch == 0xEF && cursor_ + 2 < text_.size()) {
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text_.data() + cursor_);
            if (bytes[1] == 0xBB && bytes[2] == 0xBF) {
                cursor_ += 3;
                continue;
            }
        }
        break;
    }
}

[[noreturn]] void JsonParser::error(const char* message) const {
    throw std::runtime_error(message);
}

} // namespace cashsloth

