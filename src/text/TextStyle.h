#pragma once
#include "core/Document.h"
#include <string>
#include <string_view>

namespace compositor::text {
// UTF-16 code units, the same count the project reader uses for run offsets.
int utf16Length(std::string_view utf8);
// Sorted, non-overlapping runs that end inside the text. An empty run list is valid.
bool textRunsValid(const TextContent& text);
// An empty range, or one covering every unit, sets the base color and clears runs.
void setTextColor(TextContent& text, double red, double green, double blue, int location, int length);
// An empty range, or one covering every unit, sets the base face and clears runs.
// A run copies the layer font size so the saved record stays inside the format limits.
// The style name is an installed variant such as Regular, Bold, or Italic.
void setTextFont(TextContent& text, std::string family, int location, int length);
void setTextFont(TextContent& text, std::string family, std::string style, int location, int length);
void setTextSize(TextContent& text, double size);
// Replaces a UTF-16 range. New units inherit the color and face of the unit before them.
// Returns false when the result would exceed the text limit; the content is left unchanged.
bool replaceText(TextContent& text, int location, int length, std::string_view insertion);
std::string fontAt(const TextContent& text, int index);
std::string styleAt(const TextContent& text, int index);
// Empty when the range is empty or uses more than one family or style.
std::string uniformFont(const TextContent& text, int location, int length);
std::string uniformStyle(const TextContent& text, int location, int length);
void colorAt(const TextContent& text, int index, double& red, double& green, double& blue);
bool hasTextBox(const TextContent& text);
// Leading 0 is 120% of the font size. Otherwise the stored baseline-to-baseline distance.
double lineHeight(const TextContent& text);
}
