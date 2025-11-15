#pragma once

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cashsloth {

inline std::string trim(std::string_view view) {
    const auto begin = view.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return std::string();
    }
    const auto end = view.find_last_not_of(" \t\r\n");
    return std::string(view.substr(begin, end - begin + 1));
}

inline std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

inline std::wstring toWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

inline std::string toNarrow(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

inline std::string formatCurrency(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount << " CHF";
    return oss.str();
}

inline std::wstring formatWindowsErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message;
    if (length != 0 && buffer) {
        message.assign(buffer, length);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
            message.pop_back();
        }
    } else {
        std::wostringstream stream;
        stream << L"Unbekannter Fehler (" << error << L")";
        message = stream.str();
    }
    return message;
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

} // namespace cashsloth

