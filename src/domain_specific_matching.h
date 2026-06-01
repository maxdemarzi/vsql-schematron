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

#endif // DOMAIN_SPECIFIC_MATCHING_H
