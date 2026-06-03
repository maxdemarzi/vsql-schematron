#ifndef SCHEMA_ANALYZER_HELPERS_H
#define SCHEMA_ANALYZER_HELPERS_H

#include "schema_analyzer.h"
#include <string>
#include <vector>

void setDynamicPrefix(const std::string& prefix);
void clearDynamicPrefix();

std::string to_lower(std::string s);
std::string stripSchemaPrefix(const std::string& name);
std::string stripTablePrefix(const std::string& name);
std::string expandAbbreviation(const std::string& word);
std::string expandAllAbbreviations(const std::string& s);
std::string stripRolePrefix(const std::string& name);
std::string singularize(const std::string& w);

bool isGenericTableSuffix(const std::string& suffix);
bool isKnown2CharAbbreviation(const std::string& s);
bool matchCleanTableNames(const std::string& p, const std::string& t, bool allow_substring);
bool matchTableName(const std::string& col_prefix, const std::string& tbl_name, bool allow_substring = true);

bool isTemporalType(const std::string& type);
bool isSystemColumn(const std::string& col_name);
bool isStatisticColumn(const std::string& col_name);
bool isSelfReferentialPrefix(const std::string& prefix);

bool splitColumnName(const std::string& col, std::string& prefix, std::string& suffix);
std::vector<std::string> getEffectivePKs(const std::string& tbl_name, const TableInfo& info);
bool matchMiddleIdConvention(const std::string& col, const std::string& tbl_b, bool allow_substring = true);
std::string detectSharedTablePrefix(const std::vector<std::string>& table_names);
bool isSequenceOrSystemTable(const std::string& tbl_name);
bool isSubtypeTable(const std::string& tbl_a, const std::string& tbl_b);

std::string getTableAcronym(const std::string& tbl);
std::string stripAcronymPrefix(const std::string& col, const std::string& tbl);
bool isGenericIdentifier(const std::string& s);

#endif // SCHEMA_ANALYZER_HELPERS_H
