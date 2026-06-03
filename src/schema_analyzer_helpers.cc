#include "schema_analyzer_helpers.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

/**
 * Returns true if the SQL data type represents a date/time format.
 */
bool isTemporalType(const std::string& type) {
    std::string s = to_lower(type);
    return s == "date" || s == "datetime" || s == "timestamp" || s == "time";
}

/**
 * Returns true if the column represents technical metadata/auditing fields (like rowguid).
 */
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

/**
 * Returns true if the column stores statistical counters, percentages, or sums
 * to avoid linking them as foreign keys.
 */
bool isStatisticColumn(const std::string& col_name) {
    std::string s = to_lower(col_name);
    if (s == "num" || s == "count" || s == "cnt" || s == "qty" || s == "quantity" || s == "total" || s == "sum" || s == "avg" || s == "min" || s == "max" || s == "pct" || s == "percent" || s == "percentage") return true;
    if (s.rfind("num_", 0) == 0 || s.rfind("number_", 0) == 0 || s.rfind("count_", 0) == 0 || s.rfind("cnt_", 0) == 0 || s.rfind("total_", 0) == 0 || s.rfind("tot_", 0) == 0 || s.rfind("sum_", 0) == 0 || s.rfind("avg_", 0) == 0 || s.rfind("min_", 0) == 0 || s.rfind("max_", 0) == 0 || s.rfind("qty_", 0) == 0 || s.rfind("quantity_", 0) == 0) return true;
    if (s.length() > 6 && (s.rfind("_count") == s.length() - 6 || s.rfind("_total") == s.length() - 6)) return true;
    if (s.length() > 4 && (s.rfind("_cnt") == s.length() - 4 || s.rfind("_qty") == s.length() - 4 || s.rfind("_sum") == s.length() - 4 || s.rfind("_avg") == s.length() - 4 || s.rfind("_min") == s.length() - 4 || s.rfind("_max") == s.length() - 4 || s.rfind("_pct") == s.length() - 4)) return true;
    if (s.length() > 9 && s.rfind("_quantity") == s.length() - 9) return true;
    if (s.length() > 8 && s.rfind("_percent") == s.length() - 8) return true;
    if (s.length() > 11 && s.rfind("_percentage") == s.length() - 11) return true;
    
    // If it ends with key/id suffix, it's not a simple statistic/count
    if (s.length() > 3 && (s.rfind("_id") == s.length() - 3 || s.rfind("_cd") == s.length() - 3)) return false;
    if (s.length() > 4 && (s.rfind("_key") == s.length() - 4 || s.rfind("_uid") == s.length() - 4)) return false;
    if (s.length() > 5) {
        std::vector<std::string> suffixes = {"_uuid", "_guid", "_code"};
        for (const auto& sfx : suffixes) {
            if (s.rfind(sfx) == s.length() - sfx.length()) {
                return false;
            }
        }
    }
    return false;
}

/**
 * Returns true if the prefix contains self-referential words like parent, child, prev, manager.
 */
bool isSelfReferentialPrefix(const std::string& prefix) {
    std::string p = to_lower(prefix);
    std::vector<std::string> self_ref_words = {
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
        for (const auto& word : self_ref_words) {
            if (part == word) {
                return true;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

/**
 * Infers primary key columns for a table if they are not explicitly declared.
 */
std::vector<std::string> getEffectivePKs(const std::string& tbl_name, const TableInfo& info) {
    if (!info.pk_columns.empty()) {
        return info.pk_columns;
    }
    std::vector<std::string> pks;
    std::string tbl_clean = stripTablePrefix(tbl_name);
    std::string tbl_lower = to_lower(tbl_name);
    
    // Split tbl_clean by underscore to find the last word
    std::vector<std::string> words;
    std::string word;
    std::istringstream tokenStream(tbl_clean);
    while (std::getline(tokenStream, word, '_')) {
        if (!word.empty()) words.push_back(word);
    }
    std::string last_word = words.empty() ? "" : words.back();
    
    std::vector<std::string> pk_prefixes = {"id", "code", "cod", "cd", "no", "num", "number", "key", "nro", "nra", "nr"};
    std::vector<std::string> pk_suffixes = {"id", "code", "cod", "cd", "no", "num", "number", "key"};
    
    for (const auto& col_pair : info.column_types) {
        std::string col_lower = to_lower(col_pair.first);
        
        if (col_lower == "id" || col_lower == "uuid" || col_lower == "guid" || col_lower == "uid") {
            pks.push_back(col_pair.first);
            continue;
        }
        
        bool is_pk = false;
        for (const auto& sfx : pk_suffixes) {
            if (col_lower == tbl_clean + "_" + sfx || col_lower == tbl_clean + sfx ||
                col_lower == tbl_lower + "_" + sfx || col_lower == tbl_lower + sfx) {
                is_pk = true;
                break;
            }
        }
        if (is_pk) {
            pks.push_back(col_pair.first);
            continue;
        }
        
        for (const auto& pfx : pk_prefixes) {
            if (col_lower == pfx + "_" + tbl_clean || col_lower == pfx + tbl_clean ||
                col_lower == pfx + "_" + tbl_lower || col_lower == pfx + tbl_lower) {
                is_pk = true;
                break;
            }
        }
        if (is_pk) {
            pks.push_back(col_pair.first);
            continue;
        }
        
        if (words.size() >= 2 && !last_word.empty()) {
            for (const auto& sfx : pk_suffixes) {
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
