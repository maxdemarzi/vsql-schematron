#ifndef RELATIONSHIP_ANALYZER_H
#define RELATIONSHIP_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <villagesql/preview/sql_query.h>

struct Relationship {
    std::string from_table;
    std::string from_column;
    std::string to_table;
    std::string to_column;
    bool is_explicit;

    bool operator<(const Relationship& other) const {
        if (from_table != other.from_table) return from_table < other.from_table;
        if (from_column != other.from_column) return from_column < other.from_column;
        if (to_table != other.to_table) return to_table < other.to_table;
        return to_column < other.to_column;
    }
};

struct TableInfo {
    std::string name;
    std::vector<std::string> pk_columns;
    std::unordered_map<std::string, std::string> column_types;
};

bool isSystemDatabase(const std::string& db);

std::string analyzeRelationships(const std::string& db_name, vsql::preview_sql_query::Session& session);

#endif // RELATIONSHIP_ANALYZER_H
