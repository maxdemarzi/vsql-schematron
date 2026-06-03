#ifndef IMPLIED_RELATIONSHIPS_H
#define IMPLIED_RELATIONSHIPS_H

#include "schema_analyzer.h"
#include <set>
#include <vector>
#include <unordered_map>
#include <string>

void findImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::set<std::pair<std::string, std::string>>& explicit_mapped_cols,
    std::set<Relationship>& relationships
);

#endif // IMPLIED_RELATIONSHIPS_H
