#include "data_analyzer.h"
#include "schema_analyzer.h"
#include <cctype>
#include <algorithm>
#include <sstream>

namespace {

std::string to_lower(std::string s) {
    for (char &c : s) c = std::tolower(c);
    return s;
}

bool areTypesCompatible(const std::string& t1, const std::string& t2) {
    std::string s1 = to_lower(t1);
    std::string s2 = to_lower(t2);
    if (s1 == s2) return true;

    auto is_numeric = [](const std::string& s) {
        return s.find("int") != std::string::npos || s == "decimal" || s == "double" || s == "float" || s == "real" || s == "numeric";
    };
    if (is_numeric(s1) && is_numeric(s2)) return true;

    auto is_string = [](const std::string& s) {
        return s.find("char") != std::string::npos || s.find("text") != std::string::npos;
    };
    if (is_string(s1) && is_string(s2)) return true;

    return false;
}

bool isSystemColumn(const std::string& col_name) {
    std::string s = to_lower(col_name);
    return s == "rowguid" || s == "row_guid" ||
           s == "_partneruuid" || s == "partneruuid" || s == "partner_uuid" ||
           s == "_revision" || s == "revision" || s == "revision_number" ||
           s == "modifieddate" || s == "modified_date" || s == "modifeddate" ||
           s == "_last_updated_on" || s == "last_updated_on" ||
           s == "_created_on" || s == "created_on" ||
           s == "createddate" || s == "created_date" ||
           s == "updateddate" || s == "updated_date";
}

// Returns true if the column data type is suitable for data relationship profiling.
// We exclude complex structures (JSON, geometry), binary blobs, and temporal types
// (date, time, datetime, timestamp) as they rarely serve as valid relational keys.
bool isProfileableType(const std::string& type, const std::string& col_name) {
    if (isSystemColumn(col_name)) {
        return false;
    }
    std::string s = to_lower(type);
    
    // Skip temporal types (rarely represent valid relational connections)
    if (s == "date" || s == "datetime" || s == "timestamp" || s == "time") {
        return false;
    }
    
    // Skip complex and binary types
    return s != "json" && 
           s != "blob" && s != "tinyblob" && s != "mediumblob" && s != "longblob" &&
           s != "geometry";
}

std::vector<std::string> getTableNames(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::vector<std::string> names;
    std::string tables_sql = "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = '" + db_name + "' AND TABLE_TYPE = 'BASE TABLE'";
    auto tables_res = session.sql(tables_sql).execute();
    if (tables_res && !tables_res.has_error()) {
        while (tables_res.next()) {
            std::string_view tbl = tables_res.column_str(0);
            if (!tbl.empty()) {
                names.push_back(std::string(tbl));
            }
        }
    }
    return names;
}

std::unordered_map<std::string, DataProfilerTableInfo> getTablesMetadata(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::unordered_map<std::string, DataProfilerTableInfo> metadata_map;
    std::string columns_sql = "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, COLUMN_KEY FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '" + db_name + "' ORDER BY TABLE_NAME, ORDINAL_POSITION";
    auto columns_res = session.sql(columns_sql).execute();
    if (columns_res && !columns_res.has_error()) {
        while (columns_res.next()) {
            std::string tbl = std::string(columns_res.column_str(0));
            std::string col = std::string(columns_res.column_str(1));
            std::string type = std::string(columns_res.column_str(2));
            std::string key = std::string(columns_res.column_str(3));

            auto& tbl_info = metadata_map[tbl];
            tbl_info.name = tbl;
            
            ColumnProfile col_profile;
            col_profile.name = col;
            col_profile.data_type = type;
            if (key == "PRI") {
                col_profile.is_pk_candidate = true;
            }
            tbl_info.columns[col] = col_profile;
        }
    }
    return metadata_map;
}

void profileTablesData(const std::string& db_name, vsql::preview_sql_query::Session& session, std::unordered_map<std::string, DataProfilerTableInfo>& metadata_map) {
    for (auto& pair : metadata_map) {
        const std::string& tbl_name = pair.first;
        auto& tbl_info = pair.second;
        
        std::vector<std::string> col_names;
        for (const auto& col_pair : tbl_info.columns) {
            if (isProfileableType(col_pair.second.data_type, col_pair.first)) {
                col_names.push_back(col_pair.first);
            }
        }
        
        if (col_names.empty()) continue;
        
        std::ostringstream oss;
        oss << "SELECT COUNT(*)";
        for (const auto& col : col_names) {
            oss << ", COUNT(`" << col << "`), COUNT(DISTINCT `" << col << "`)";
        }
        oss << " FROM `" << db_name << "`.`" << tbl_name << "`";
        
        auto res = session.sql(oss.str()).execute();
        if (res && !res.has_error() && res.next()) {
            tbl_info.total_rows = res.column_int(0);
            for (size_t i = 0; i < col_names.size(); ++i) {
                const std::string& col = col_names[i];
                auto& col_profile = tbl_info.columns[col];
                col_profile.total_rows = tbl_info.total_rows;
                col_profile.non_null_count = res.column_int(1 + 2 * i);
                col_profile.distinct_count = res.column_int(2 + 2 * i);
                
                if (col_profile.distinct_count > 0 && col_profile.distinct_count == col_profile.non_null_count) {
                    col_profile.is_pk_candidate = true;
                }
            }
        }
    }
}

} // namespace

std::string analyzeDataRelationships(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::vector<std::string> table_names = getTableNames(db_name, session);
    if (table_names.empty()) {
        std::ostringstream oss;
        oss << "Database: " << db_name << "\n\n";
        oss << "Data Connections:\n";
        oss << "------------------\n";
        oss << "(No relationships found)\n";
        return oss.str();
    }

    auto tables_metadata = getTablesMetadata(db_name, session);
    profileTablesData(db_name, session, tables_metadata);

    std::set<DataRelationship> relationships;

    for (const auto& tbl_a_name : table_names) {
        auto it_a = tables_metadata.find(tbl_a_name);
        if (it_a == tables_metadata.end()) continue;
        const auto& tbl_a = it_a->second;
        if (tbl_a.total_rows == 0) continue;

        for (const auto& col_a_pair : tbl_a.columns) {
            const auto& col_a = col_a_pair.second;
            if (col_a.distinct_count < 3) continue;

            for (const auto& tbl_b_name : table_names) {
                if (tbl_a_name == tbl_b_name) continue;

                auto it_b = tables_metadata.find(tbl_b_name);
                if (it_b == tables_metadata.end()) continue;
                const auto& tbl_b = it_b->second;
                if (tbl_b.total_rows == 0) continue;

                for (const auto& col_b_pair : tbl_b.columns) {
                    const auto& col_b = col_b_pair.second;
                    
                    if (!col_b.is_pk_candidate) continue;
                    if (!areTypesCompatible(col_a.data_type, col_b.data_type)) continue;

                    std::ostringstream q_oss;
                    q_oss << "SELECT COUNT(DISTINCT `" << col_a.name << "`) FROM `" 
                          << db_name << "`.`" << tbl_a_name 
                          << "` WHERE `" << col_a.name << "` IS NOT NULL AND `" << col_a.name 
                          << "` NOT IN (SELECT `" << col_b.name << "` FROM `" 
                          << db_name << "`.`" << tbl_b_name << "` WHERE `" << col_b.name << "` IS NOT NULL)";
                    
                    auto res = session.sql(q_oss.str()).execute();
                    if (res && !res.has_error() && res.next()) {
                        long long not_contained = res.column_int(0);
                        double ratio = static_cast<double>(col_a.distinct_count - not_contained) / col_a.distinct_count;
                        if (ratio >= 0.95) {
                            DataRelationship rel;
                            rel.from_table = tbl_a_name;
                            rel.from_column = col_a.name;
                            rel.to_table = tbl_b_name;
                            rel.to_column = col_b.name;
                            rel.containment_ratio = ratio;
                            relationships.insert(rel);
                        }
                    }
                }
            }
        }
    }

    std::ostringstream oss;
    oss << "Database: " << db_name << "\n\n";
    oss << "Data Connections:\n";
    oss << "------------------\n";

    if (relationships.empty()) {
        oss << "(No relationships found)\n";
    } else {
        for (const auto& rel : relationships) {
            int pct = static_cast<int>(rel.containment_ratio * 100);
            oss << "[Data-Implied] " << rel.from_table << "." << rel.from_column << " -> "
                << rel.to_table << "." << rel.to_column << " (containment: " << pct << "%)\n";
        }
    }

    return oss.str();
}
