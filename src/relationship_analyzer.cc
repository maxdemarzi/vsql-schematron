#include "relationship_analyzer.h"
#include <cctype>
#include <algorithm>
#include <sstream>

bool isSystemDatabase(const std::string& db) {
    auto to_lower = [](std::string s) {
        for (char &c : s) c = std::tolower(c);
        return s;
    };
    std::string name = to_lower(db);
    return name == "information_schema" || name == "performance_schema" ||
           name == "mysql" || name == "sys";
}

bool matchTableName(const std::string& col_prefix, const std::string& tbl_name) {
    auto to_lower = [](std::string s) {
        for (char &c : s) c = std::tolower(c);
        return s;
    };
    std::string prefix = to_lower(col_prefix);
    std::string tbl = to_lower(tbl_name);
    if (prefix == tbl) return true;
    if (tbl == prefix + "s") return true;
    if (tbl == prefix + "es") return true;
    if (prefix.length() > 1 && prefix.back() == 'y') {
        std::string ies = prefix.substr(0, prefix.length() - 1) + "ies";
        if (tbl == ies) return true;
    }
    return false;
}

bool typeMatches(const std::string& t1, const std::string& t2) {
    auto to_lower = [](std::string s) {
        for (char &c : s) c = std::tolower(c);
        return s;
    };
    return to_lower(t1) == to_lower(t2);
}

std::string analyzeRelationships(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::string tables_sql = "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = '" + db_name + "' AND TABLE_TYPE = 'BASE TABLE'";
    auto tables_res = session.sql(tables_sql).execute();
    if (!tables_res || tables_res.has_error()) {
        return "";
    }

    std::vector<std::string> table_names;
    while (tables_res.next()) {
        std::string_view tbl = tables_res.column_str(0);
        if (!tbl.empty()) {
            table_names.push_back(std::string(tbl));
        }
    }

    if (table_names.empty()) {
        std::ostringstream oss;
        oss << "Database: " << db_name << "\n\n";
        oss << "Table Connections:\n";
        oss << "------------------\n";
        oss << "(No relationships found)\n";
        return oss.str();
    }

    std::string columns_sql = "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, COLUMN_KEY FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '" + db_name + "' ORDER BY TABLE_NAME, ORDINAL_POSITION";
    auto columns_res = session.sql(columns_sql).execute();
    if (!columns_res || columns_res.has_error()) {
        return "";
    }

    std::unordered_map<std::string, TableInfo> tables_info;
    while (columns_res.next()) {
        std::string tbl = std::string(columns_res.column_str(0));
        std::string col = std::string(columns_res.column_str(1));
        std::string type = std::string(columns_res.column_str(2));
        std::string key = std::string(columns_res.column_str(3));

        auto& info = tables_info[tbl];
        info.name = tbl;
        info.column_types[col] = type;
        if (key == "PRI") {
            info.pk_columns.push_back(col);
        }
    }

    std::string keys_sql = "SELECT TABLE_NAME, COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA = '" + db_name + "' AND REFERENCED_TABLE_NAME IS NOT NULL";
    auto keys_res = session.sql(keys_sql).execute();
    if (!keys_res || keys_res.has_error()) {
        return "";
    }

    std::set<Relationship> relationships;
    std::set<std::pair<std::string, std::string>> explicit_mapped_cols;

    while (keys_res.next()) {
        Relationship rel;
        rel.from_table = std::string(keys_res.column_str(0));
        rel.from_column = std::string(keys_res.column_str(1));
        rel.to_table = std::string(keys_res.column_str(2));
        rel.to_column = std::string(keys_res.column_str(3));
        rel.is_explicit = true;

        relationships.insert(rel);
        explicit_mapped_cols.insert({rel.from_table, rel.from_column});
    }

    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            auto to_lower = [](std::string s) {
                for (char &c : s) c = std::tolower(c);
                return s;
            };

            // Phase 1: Exact Name Match with a different table
            bool relationship_found = false;
            if (to_lower(col_a) != "id") {
                for (const auto& tbl_b : table_names) {
                    if (tbl_a == tbl_b) continue;

                    auto it_b = tables_info.find(tbl_b);
                    if (it_b == tables_info.end()) continue;
                    const auto& info_b = it_b->second;

                    for (const auto& pk_b : info_b.pk_columns) {
                        if (col_a == pk_b) {
                            auto it_b_col = info_b.column_types.find(pk_b);
                            if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                                break;
                            }
                        }
                    }
                    if (relationship_found) break;
                }
            }

            if (relationship_found) continue;

            // Phase 2: Suffix matching with different tables
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;

                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;

                if (info_b.pk_columns.size() == 1) {
                    const std::string& pk_b = info_b.pk_columns[0];
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                        size_t underscore_pos_a = col_a.rfind('_');
                        if (underscore_pos_a != std::string::npos) {
                            std::string prefix_a = col_a.substr(0, underscore_pos_a);
                            std::string suffix_a = col_a.substr(underscore_pos_a + 1);

                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == to_lower(pk_b)) {
                                suffix_matches = true;
                            } else {
                                size_t underscore_pos_b = pk_b.rfind('_');
                                if (underscore_pos_b != std::string::npos) {
                                    std::string suffix_b = pk_b.substr(underscore_pos_b + 1);
                                    if (to_lower(suffix_a) == to_lower(suffix_b)) {
                                        suffix_matches = true;
                                    }
                                }
                            }

                            if (suffix_matches && matchTableName(prefix_a, tbl_b)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (relationship_found) continue;

            // Phase 3: Self-referencing suffix matching (tbl_a == tbl_b)
            do {
                auto it_b = tables_info.find(tbl_a);
                if (it_b == tables_info.end()) break;
                const auto& info_b = it_b->second;

                if (info_b.pk_columns.size() == 1) {
                    const std::string& pk_b = info_b.pk_columns[0];
                    if (col_a == pk_b) {
                        break;
                    }
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                        size_t underscore_pos_a = col_a.rfind('_');
                        if (underscore_pos_a != std::string::npos) {
                            std::string prefix_a = col_a.substr(0, underscore_pos_a);
                            std::string suffix_a = col_a.substr(underscore_pos_a + 1);

                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == to_lower(pk_b)) {
                                suffix_matches = true;
                            } else {
                                size_t underscore_pos_b = pk_b.rfind('_');
                                if (underscore_pos_b != std::string::npos) {
                                    std::string suffix_b = pk_b.substr(underscore_pos_b + 1);
                                    if (to_lower(suffix_a) == to_lower(suffix_b)) {
                                        suffix_matches = true;
                                    }
                                }
                            }

                            if (suffix_matches) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_a;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            } while (false);
        }
    }

    std::ostringstream oss;
    oss << "Database: " << db_name << "\n\n";
    oss << "Table Connections:\n";
    oss << "------------------\n";

    if (relationships.empty()) {
        oss << "(No relationships found)\n";
    } else {
        for (const auto& rel : relationships) {
            oss << (rel.is_explicit ? "[Explicit] " : "[Implied]  ")
                << rel.from_table << "." << rel.from_column << " -> "
                << rel.to_table << "." << rel.to_column << "\n";
        }
    }

    return oss.str();
}
