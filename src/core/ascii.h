#pragma once

// Locale-independent ASCII character tests. The <cctype> functions follow
// the process locale, which Qt sets from the system; in a UTF-8 locale they
// treat single bytes of multi-byte characters as Latin-1 letters or spaces
// (tolower(0xC3) is 0xE3, isspace(0xA0) is true), corrupting the text.
// Every byte at or above 0x80 is "other" here.

namespace jsontitan::core::ascii {

constexpr auto isUpper(char c) -> bool { return c >= 'A' && c <= 'Z'; }
constexpr auto isAlpha(char c) -> bool { return isUpper(c) || (c >= 'a' && c <= 'z'); }
constexpr auto isDigit(char c) -> bool { return c >= '0' && c <= '9'; }
constexpr auto isAlnum(char c) -> bool { return isAlpha(c) || isDigit(c); }

constexpr auto isSpace(char c) -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

constexpr auto toLower(char c) -> char {
    return isUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace jsontitan::core::ascii
