#ifndef STRING_HELPERS_H
#define STRING_HELPERS_H

#include <string>

std::string to_lower(std::string s);
std::string singularize(const std::string& w);
std::string expandAbbreviation(const std::string& word);
std::string expandAllAbbreviations(const std::string& s);
bool isKnown2CharAbbreviation(const std::string& s);
std::string stripTrailingUnderscore(const std::string& s);

#endif // STRING_HELPERS_H
