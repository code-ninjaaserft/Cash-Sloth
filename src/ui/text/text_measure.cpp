#include "ui/text/text_measure.h"

namespace cashsloth::ui {

SIZE MeasureText(HDC hdc, const std::wstring& text, HFONT font) {
    SIZE size{};
    if (!hdc || !font || text.empty()) {
        return size;
    }
    HGDIOBJ oldFont = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(hdc, oldFont);
    return size;
}

bool FitsSingleLine(HDC hdc, const std::wstring& text, HFONT font, int maxW, int maxH) {
    if (!hdc || !font || maxW <= 0 || maxH <= 0) {
        return false;
    }
    RECT rect{0, 0, maxW, maxH};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextW(hdc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
    return rect.right <= maxW && rect.bottom <= maxH;
}

bool FitsTwoLinesWrap(HDC hdc, const std::wstring& text, HFONT font, int maxW, int maxH) {
    if (!hdc || !font || maxW <= 0 || maxH <= 0) {
        return false;
    }
    TEXTMETRICW metrics{};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    GetTextMetricsW(hdc, &metrics);
    const int lineHeight = metrics.tmHeight;
    RECT rect{0, 0, maxW, maxH};
    DrawTextW(
        hdc,
        text.c_str(),
        -1,
        &rect,
        DT_WORDBREAK | DT_EDITCONTROL | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
    const int textHeight = rect.bottom - rect.top;
    if (textHeight > maxH) {
        return false;
    }
    return textHeight <= lineHeight * 2 + 2;
}

}  // namespace cashsloth::ui
