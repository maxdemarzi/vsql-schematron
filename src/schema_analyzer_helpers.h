#ifndef SCHEMA_ANALYZER_HELPERS_H
#define SCHEMA_ANALYZER_HELPERS_H

#include "schema_analyzer.h"
#include "string_helpers.h"
#include "name_matching.h"
#include <string>
#include <vector>

bool isTemporalType(const std::string& type);
bool isSystemColumn(const std::string& col_name);
bool isStatisticColumn(const std::string& col_name);
bool isSelfReferentialPrefix(const std::string& prefix);

std::vector<std::string> getEffectivePKs(
    const std::string& tbl_name,
    const TableInfo& info,
    const std::vector<std::string>& table_names = {},
    const std::unordered_map<std::string, TableInfo>& tables_info = {},
    const std::unordered_map<std::string, std::vector<std::string>>& pk_column_to_tables = {}
);
std::string detectSharedTablePrefix(const std::vector<std::string>& table_names);
bool isSequenceOrSystemTable(const std::string& tbl_name);
bool isSubtypeTable(const std::string& tbl_a, const std::string& tbl_b);

#endif // SCHEMA_ANALYZER_HELPERS_H
