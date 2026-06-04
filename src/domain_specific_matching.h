#ifndef DOMAIN_SPECIFIC_MATCHING_H
#define DOMAIN_SPECIFIC_MATCHING_H

#include "schema_analyzer.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

bool matchDomainSpecificKeys(
    const std::string& tbl_a,
    const std::string& col_a,
    const std::string& type_a,
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    std::set<Relationship>& relationships
);

bool isPersonTable(const std::string& tbl);
bool isPersonRole(const std::string& role);
bool isPersonMatch(const std::string& prefix_a, const std::string& tbl_b);
bool isLookupMatch(const std::string& prefix_a, const std::string& tbl_b, const std::vector<std::string>& matched_tables);
bool isLookupTable(const std::string& tbl);
bool matchLastWord(const std::string& prefix_a, const std::string& tbl_b);
bool isGenericPkFkMatch(const std::string& col_a, const std::string& col_b, const std::vector<std::string>& pks_b);

#endif // DOMAIN_SPECIFIC_MATCHING_H
