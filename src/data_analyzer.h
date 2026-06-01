#ifndef DATA_ANALYZER_H
#define DATA_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <villagesql/preview/sql_query.h>

struct DataRelationship {
    std::string from_table;
    std::string from_column;
    std::string to_table;
    std::string to_column;
    double containment_ratio;

    bool operator<(const DataRelationship& other) const {
        if (from_table != other.from_table) return from_table < other.from_table;
        if (from_column != other.from_column) return from_column < other.from_column;
        if (to_table != other.to_table) return to_table < other.to_table;
        if (to_column != other.to_column) return to_column < other.to_column;
        return containment_ratio > other.containment_ratio; // prioritize higher containment
    }
};

struct ColumnProfile {
    std::string name;
    std::string data_type;
    long long total_rows = 0;
    long long non_null_count = 0;
    long long distinct_count = 0;
    bool is_pk_candidate = false;
};

struct DataProfilerTableInfo {
    std::string name;
    long long total_rows = 0;
    std::unordered_map<std::string, ColumnProfile> columns;
};

std::string analyzeDataRelationships(const std::string& db_name, vsql::preview_sql_query::Session& session);

#endif // DATA_ANALYZER_H
