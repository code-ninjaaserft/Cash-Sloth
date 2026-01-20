#pragma once

#include <windows.h>

#include <string>

namespace cashsloth::ui {

SIZE MeasureText(HDC hdc, const std::wstring& text, HFONT font);
bool FitsSingleLine(HDC hdc, const std::wstring& text, HFONT font, int maxW, int maxH);
bool FitsTwoLinesWrap(HDC hdc, const std::wstring& text, HFONT font, int maxW, int maxH);

}  // namespace cashsloth::ui
