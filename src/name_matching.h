#ifndef NAME_MATCHING_H
#define NAME_MATCHING_H

#include <string>
#include <vector>

void setDynamicPrefix(const std::string& prefix);
void clearDynamicPrefix();

std::string stripSchemaPrefix(const std::string& name);
std::string stripTablePrefix(const std::string& name);
std::string stripRolePrefix(const std::string& name);

bool matchCleanTableNames(const std::string& p, const std::string& t, bool allow_substring);
bool matchTableName(const std::string& col_prefix, const std::string& tbl_name, bool allow_substring = true);
bool matchMiddleIdConvention(const std::string& col, const std::string& tbl_b, bool allow_substring = true);

bool splitColumnName(const std::string& col, std::string& prefix, std::string& suffix);
std::string getTableAcronym(const std::string& tbl);
std::string stripAcronymPrefix(const std::string& col, const std::string& tbl);

bool isGenericTableSuffix(const std::string& suffix);
bool isGenericIdentifier(const std::string& s);

#endif // NAME_MATCHING_H
