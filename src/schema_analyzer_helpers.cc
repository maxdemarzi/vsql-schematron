#include "schema_analyzer_helpers.h"
#include "domain_specific_matching.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

/**
 * Returns true if the SQL data type represents a date/time format.
 */
bool isTemporalType(const std::string& type) {
    static const std::unordered_set<std::string> TEMPORAL_TYPES = {
        "date", "datetime", "timestamp", "time"
    };
    return TEMPORAL_TYPES.count(to_lower(type)) > 0;
}

/**
 * Returns true if the column represents technical metadata/auditing fields (like rowguid).
 */
bool isSystemColumn(const std::string& col_name) {
    static const std::unordered_set<std::string> SYSTEM_COLUMNS = {
        "rowguid", "row_guid", "_partneruuid", "partneruuid", "partner_uuid",
        "_revision", "revision", "revision_number", "modifieddate", "modified_date", "modifeddate",
        "_last_updated_on", "last_updated_on", "_created_on", "created_on",
        "createddate", "created_date", "updateddate", "updated_date",
        "created_by", "createdby", "creation_date", "creationdate", "creation_time", "creationtime",
        "last_update_by", "last_updated_by", "lastupdateby", "lastupdatedby",
        "last_update_date", "lastupdatedate", "last_update_time", "lastupdatetime",
        "updated_by", "updatedby"
    };
    return SYSTEM_COLUMNS.count(to_lower(col_name)) > 0;
}

/**
 * Returns true if the column stores statistical counters, percentages, or sums
 * to avoid linking them as foreign keys.
 */
bool isStatisticColumn(const std::string& col_name) {
    std::string s = to_lower(col_name);
    
    // If it ends with key/id suffix, it's not a simple statistic/count
    size_t last_sep = s.find_last_of("_ ");
    if (last_sep != std::string::npos && last_sep < s.length() - 1) {
        std::string suffix = s.substr(last_sep + 1);
        static const std::unordered_set<std::string> ID_SUFFIXES = {
            "id", "cd", "key", "uid", "uuid", "guid", "code"
        };
        if (ID_SUFFIXES.count(suffix) > 0) return false;
    }
    
    static const std::unordered_set<std::string> STAT_NAMES = {
        "num", "count", "cnt", "qty", "quantity", "total", "sum", "avg", "min", "max", "pct", "percent", "percentage"
    };
    if (STAT_NAMES.count(s) > 0) return true;
    
    size_t first_sep = s.find_first_of("_ ");
    if (first_sep != std::string::npos && first_sep > 0) {
        std::string prefix = s.substr(0, first_sep);
        static const std::unordered_set<std::string> STAT_PREFIXES = {
            "num", "number", "count", "cnt", "total", "tot", "sum", "avg", "min", "max", "qty", "quantity"
        };
        if (STAT_PREFIXES.count(prefix) > 0) return true;
    }
    
    if (last_sep != std::string::npos && last_sep < s.length() - 1) {
        std::string suffix = s.substr(last_sep + 1);
        static const std::unordered_set<std::string> STAT_SUFFIXES = {
            "count", "total", "cnt", "qty", "sum", "avg", "min", "max", "pct", "quantity", "percent", "percentage"
        };
        if (STAT_SUFFIXES.count(suffix) > 0) return true;
    }
    
    return false;
}

/**
 * Returns true if the prefix contains self-referential words like parent, child, prev, manager.
 */
bool isSelfReferentialPrefix(const std::string& prefix) {
    std::string p = to_lower(prefix);
    static const std::unordered_set<std::string> SELF_REF_WORDS = {
        "parent", "child", "prev", "previous", "next", "successor", "predecessor",
        "left", "right", "root", "manager", "mgr", "supervisor", "reports", "report",
        "master", "maternal", "paternal", "mother", "father", "spouse", "husband",
        "wife", "partner", "sibling", "brother", "sister", "twin", "head", "leader",
        "node", "ancestor", "descendant"
    };
    
    size_t start = 0;
    while (start < p.length()) {
        size_t end = p.find('_', start);
        std::string part = (end == std::string::npos) ? p.substr(start) : p.substr(start, end - start);
        if (SELF_REF_WORDS.count(part) > 0) {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

/**
 * Infers primary key columns for a table if they are not explicitly declared.
 */
std::vector<std::string> getEffectivePKs(
    const std::string& tbl_name,
    const TableInfo& info,
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info
) {
    if (!info.pk_columns.empty()) {
        return info.pk_columns;
    }
    
    auto isIdLikeColumn = [](const std::string& col) {
        std::string c = to_lower(col);
        static const std::unordered_set<std::string> ID_LIKE_COLUMNS = {
            "id", "uuid", "guid", "uid", "key", "code"
        };
        if (ID_LIKE_COLUMNS.count(c) > 0) return true;
        
        std::vector<std::string> suffixes = {"id", "uuid", "guid", "uid", "key", "code", "number", "num", "no"};
        for (const auto& sfx : suffixes) {
            if (c.length() > sfx.length() && c.rfind(sfx) == c.length() - sfx.length()) {
                char prev_char = col[col.length() - sfx.length() - 1];
                char boundary_char = col[col.length() - sfx.length()];
                if (prev_char == '_' || std::isupper(boundary_char)) {
                    return true;
                }
            }
        }
        return false;
    };

    std::vector<std::string> generic_pks;
    int id_like_count = 0;
    int non_sys_col_count = 0;
    for (const auto& col_pair : info.column_types) {
        std::string col_lower = to_lower(col_pair.first);
        static const std::unordered_set<std::string> EXACT_ID_COLUMNS = {
            "id", "uuid", "guid", "uid"
        };
        if (EXACT_ID_COLUMNS.count(col_lower) > 0) {
            generic_pks.push_back(col_pair.first);
        }
        if (isIdLikeColumn(col_pair.first)) {
            id_like_count++;
        }
        if (!isTemporalType(col_pair.second) && !isSystemColumn(col_pair.first) && !isStatisticColumn(col_pair.first)) {
            non_sys_col_count++;
        }
    }
    
    if (!generic_pks.empty()) {
        return generic_pks;
    }
    
    std::string tbl_clean = stripTablePrefix(tbl_name);
    std::string tbl_lower = to_lower(tbl_name);
    std::string tbl_sing = singularize(tbl_clean);
    std::string tbl_sing_lower = singularize(tbl_lower);

    bool has_table_specific_id = false;
    static const std::unordered_set<std::string> ID_SUFXS = {"id", "number", "code", "key"};
    if (!isPersonRole(tbl_sing_lower)) {
        for (const auto& col_pair : info.column_types) {
            std::string col_lower = to_lower(col_pair.first);
            for (const auto& sfx : ID_SUFXS) {
                if (col_lower == tbl_clean + "_" + sfx || col_lower == tbl_clean + sfx ||
                    col_lower == tbl_lower + "_" + sfx || col_lower == tbl_lower + sfx ||
                    col_lower == tbl_sing + "_" + sfx || col_lower == tbl_sing + sfx ||
                    col_lower == tbl_sing_lower + "_" + sfx || col_lower == tbl_sing_lower + sfx) {
                    
                    // This is a candidate for a table-specific ID.
                    // However, check if it matches a different table in the schema.
                    // We strip the suffix to get the entity prefix (e.g. "department" from "department_id")
                    std::string entity = col_lower.substr(0, col_lower.length() - sfx.length());
                    if (!entity.empty() && entity.back() == '_') entity.pop_back();

                    bool matches_other_table = false;
                    for (const auto& other_tbl : table_names) {
                        if (other_tbl != tbl_name) {
                            auto it_other = tables_info.find(other_tbl);
                            if (it_other != tables_info.end()) {
                                // If other_tbl has this column in its primary keys, and other_tbl is not a subtype of tbl_name
                                const auto& other_pks = it_other->second.pk_columns;
                                bool is_pk_in_other = false;
                                for (const auto& pk_oth : other_pks) {
                                    if (to_lower(pk_oth) == col_lower) {
                                        is_pk_in_other = true;
                                        break;
                                    }
                                }
                                if (is_pk_in_other && !isSubtypeTable(other_tbl, tbl_name)) {
                                    matches_other_table = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!matches_other_table) {
                        has_table_specific_id = true;
                        break;
                    }
                }
            }
            if (has_table_specific_id) break;
        }
    }

    if (id_like_count > 1 && non_sys_col_count <= 4 && !has_table_specific_id) {
        return {}; // Multiple ID-like columns and no generic ID PK -> junction/relationship table
    }

    std::vector<std::string> pks;
    
    // Split tbl_clean by underscore to find the last word
    std::vector<std::string> words;
    std::string word;
    std::istringstream tokenStream(tbl_clean);
    while (std::getline(tokenStream, word, '_')) {
        if (!word.empty()) words.push_back(word);
    }
    std::string last_word = words.empty() ? "" : words.back();
    
    std::vector<std::string> id_prefixes = {"id", "code", "cod", "cd", "no", "num", "number", "key", "nro", "nra", "nr"};
    std::vector<std::string> id_suffixes = {"id", "code", "cod", "cd", "no", "num", "number", "key"};
    
    // Pass 1: Look for ID-like columns
    for (const auto& col_pair : info.column_types) {
        std::string col_lower = to_lower(col_pair.first);
        std::string type_lower = to_lower(col_pair.second);
        
        static const std::unordered_set<std::string> EXACT_ID_COLUMNS = {
            "id", "uuid", "guid", "uid"
        };
        if (EXACT_ID_COLUMNS.count(col_lower) > 0) {
            pks.push_back(col_pair.first);
            continue;
        }
        
        // Skip large object types, JSON, and geometry for other primary keys
        if (type_lower.find("text") != std::string::npos ||
            type_lower.find("blob") != std::string::npos ||
            type_lower.find("clob") != std::string::npos ||
            type_lower.find("json") != std::string::npos ||
            type_lower.find("geometry") != std::string::npos) {
            continue;
        }
        
        bool is_pk = false;
        for (const auto& sfx : id_suffixes) {
            if (col_lower == tbl_clean + "_" + sfx || col_lower == tbl_clean + sfx ||
                col_lower == tbl_lower + "_" + sfx || col_lower == tbl_lower + sfx ||
                col_lower == tbl_sing + "_" + sfx || col_lower == tbl_sing + sfx ||
                col_lower == tbl_sing_lower + "_" + sfx || col_lower == tbl_sing_lower + sfx) {
                is_pk = true;
                break;
            }
        }
        if (is_pk) {
            pks.push_back(col_pair.first);
            continue;
        }
        
        for (const auto& pfx : id_prefixes) {
            if (col_lower == pfx + "_" + tbl_clean || col_lower == pfx + tbl_clean ||
                col_lower == pfx + "_" + tbl_lower || col_lower == pfx + tbl_lower ||
                col_lower == pfx + "_" + tbl_sing || col_lower == pfx + tbl_sing ||
                col_lower == pfx + "_" + tbl_sing_lower || col_lower == pfx + tbl_sing_lower) {
                is_pk = true;
                break;
            }
        }
        if (is_pk) {
            pks.push_back(col_pair.first);
            continue;
        }
        
        if (words.size() >= 2 && !last_word.empty()) {
            for (const auto& sfx : id_suffixes) {
                if (col_lower == last_word + "_" + sfx || col_lower == last_word + sfx) {
                    is_pk = true;
                    break;
                }
            }
            if (is_pk) {
                pks.push_back(col_pair.first);
                continue;
            }
        }
    }
    
    if (pks.empty() && id_like_count > 1) {
        return {};
    }
    
    // Pass 2: If no ID-like columns were found, look for name-like columns
    if (pks.empty() && info.column_types.size() <= 3) {
        for (const auto& col_pair : info.column_types) {
            std::string col_lower = to_lower(col_pair.first);
            std::string type_lower = to_lower(col_pair.second);
            
            if (type_lower.find("text") != std::string::npos ||
                type_lower.find("blob") != std::string::npos ||
                type_lower.find("clob") != std::string::npos ||
                type_lower.find("json") != std::string::npos ||
                type_lower.find("geometry") != std::string::npos) {
                continue;
            }
            
            if (col_lower == "name") {
                pks.push_back(col_pair.first);
                continue;
            }
            
            bool is_pk = false;
            if (col_lower == tbl_clean + "_name" || col_lower == tbl_clean + "name" ||
                col_lower == tbl_lower + "_name" || col_lower == tbl_lower + "name" ||
                col_lower == tbl_sing + "_name" || col_lower == tbl_sing + "name" ||
                col_lower == tbl_sing_lower + "_name" || col_lower == tbl_sing_lower + "name") {
                is_pk = true;
            }
            if (is_pk) {
                pks.push_back(col_pair.first);
                continue;
            }
            
            if (col_lower == "name_" + tbl_clean || col_lower == "name" + tbl_clean ||
                col_lower == "name_" + tbl_lower || col_lower == "name" + tbl_lower ||
                col_lower == "name_" + tbl_sing || col_lower == "name" + tbl_sing ||
                col_lower == "name_" + tbl_sing_lower || col_lower == "name" + tbl_sing_lower) {
                is_pk = true;
            }
            if (is_pk) {
                pks.push_back(col_pair.first);
                continue;
            }
            
            if (words.size() >= 2 && !last_word.empty()) {
                if (col_lower == last_word + "_name" || col_lower == last_word + "name") {
                    pks.push_back(col_pair.first);
                }
            }
        }
    }
    
    return pks;
}

/**
 * Automatically detects any shared table prefix across all tables in a schema.
 *
 * Example:
 *   Given {"ads_campaigns", "ads_clicks", "ads_adgroups"}:
 *   Returns "ads".
 */
std::string detectSharedTablePrefix(const std::vector<std::string>& table_names) {
    if (table_names.size() < 2) {
        return "";
    }
    std::unordered_set<std::string> stripped_table_names;
    for (const auto& name : table_names) {
        std::string clean = to_lower(stripSchemaPrefix(name));
        stripped_table_names.insert(clean);
        stripped_table_names.insert(singularize(clean));
    }
    
    std::unordered_map<std::string, int> prefix_counts;
    for (const auto& name : table_names) {
        std::string tbl = to_lower(name);
        size_t dot = tbl.rfind('.');
        if (dot != std::string::npos) {
            tbl = tbl.substr(dot + 1);
        }
        
        size_t underscore = tbl.find('_');
        while (underscore != std::string::npos && underscore > 0) {
            std::string prefix = tbl.substr(0, underscore);
            if (stripped_table_names.count(prefix) == 0) {
                prefix_counts[prefix]++;
            }
            underscore = tbl.find('_', underscore + 1);
        }
    }
    
    std::string best_prefix = "";
    int best_count = 0;
    for (const auto& pair : prefix_counts) {
        std::string prefix = pair.first;
        int count = pair.second;
        if (count >= 2) {
            if (count >= 3 || count == table_names.size()) {
                if (best_prefix.empty()) {
                    best_prefix = prefix;
                    best_count = count;
                } else {
                    if (count > best_count) {
                        best_prefix = prefix;
                        best_count = count;
                    } else if (count == best_count) {
                        if (prefix.length() > best_prefix.length()) {
                            best_prefix = prefix;
                        } else if (prefix.length() == best_prefix.length()) {
                            if (prefix < best_prefix) {
                                  best_prefix = prefix;
                            }
                        }
                    }
                }
            }
        }
    }
    return best_prefix;
}

/**
 * Returns true if the table is a system sequence, backup, or temporary table.
 */
bool isSequenceOrSystemTable(const std::string& tbl_name) {
    std::string tbl = to_lower(tbl_name);
    size_t dot = tbl.rfind('.');
    if (dot != std::string::npos) {
        tbl = tbl.substr(dot + 1);
    }
    if (tbl.rfind("seq_", 0) == 0 || tbl.rfind("sequence_", 0) == 0) {
        return true;
    }
    if (tbl.length() > 4 && tbl.rfind("_seq") == tbl.length() - 4) {
        return true;
    }
    if (tbl.length() > 9 && tbl.rfind("_sequence") == tbl.length() - 9) {
        return true;
    }
    static const std::unordered_set<std::string> SYSTEM_TABLES = {
        "sequelizemeta", "flyway_schema_history", "schema_version",
        "alembic_version", "databasechangelog", "databasechangeloglock"
    };
    if (SYSTEM_TABLES.count(tbl) > 0) {
        return true;
    }
    if (tbl.rfind("tmp_", 0) == 0 || tbl.rfind("temp_", 0) == 0 ||
        tbl.rfind("bak_", 0) == 0 || tbl.rfind("backup_", 0) == 0) {
        return true;
    }
    return false;
}

/**
 * Checks if table A represents a subtype/subclass of table B.
 *
 * Example:
 *   isSubtypeTable("customer_corporate", "customer") -> true
 */
bool isSubtypeTable(const std::string& tbl_a, const std::string& tbl_b) {
    std::string a = stripSchemaPrefix(to_lower(tbl_a));
    std::string b = stripSchemaPrefix(to_lower(tbl_b));
    std::string clean_a = stripTablePrefix(a);
    std::string clean_b = stripTablePrefix(b);
    if (clean_a == clean_b) return a.length() > b.length();
    if (clean_a.length() > clean_b.length()) {
        // Exclude catalog/lookup tables from being subtypes
        size_t last_underscore = clean_a.rfind('_');
        if (last_underscore != std::string::npos && last_underscore > 0) {
            std::string suffix = clean_a.substr(last_underscore + 1);
            static const std::unordered_set<std::string> CATALOG_SUFFIXES = {
                "type", "types", "status", "statuses", "cat", "cats", "category", "categories",
                "class", "classes", "group", "groups", "genre", "genres", "role", "roles",
                "state", "states", "level", "levels", "priority", "priorities", "lookup", "lookups",
                "code", "codes", "mode", "modes", "action", "actions", "tag", "tags", "master", "mstr", "dict",
                "version", "versions", "ver", "vers"
            };
            if (CATALOG_SUFFIXES.count(suffix)) {
                return false;
            }
        }

        size_t pos = clean_a.rfind(clean_b);
        if (pos != std::string::npos && pos == clean_a.length() - clean_b.length()) return true;
        
        // Prefix-based hierarchy (e.g. step_video -> steps)
        std::string sb = clean_b;
        if (sb.length() > 1 && sb.back() == 's') sb = sb.substr(0, sb.length() - 1);
        if (clean_a.rfind(sb + "_", 0) == 0 || clean_a.rfind(clean_b + "_", 0) == 0) {
            return true;
        }
    }
    return false;
}
