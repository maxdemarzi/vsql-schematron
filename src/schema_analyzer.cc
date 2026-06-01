#include "schema_analyzer.h"
#include "domain_specific_matching.h"
#include <cctype>
#include <algorithm>
#include <sstream>

namespace {

std::string to_lower(std::string s) {
    for (char &c : s) c = std::tolower(c);
    return s;
}

std::string stripSchemaPrefix(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        return name.substr(dot + 1);
    }
    return name;
}

std::string stripTablePrefix(const std::string& name) {
    std::string n = to_lower(name);
    if (n.rfind("tbl_", 0) == 0) n = n.substr(4);
    if (n.rfind("ref", 0) == 0) n = n.substr(3);
    
    size_t underscore = n.find('_');
    while (underscore != std::string::npos && underscore > 0) {
        std::string prefix = n.substr(0, underscore);
        if (prefix == "idn" || prefix == "oauth" || prefix == "comtn" || 
            prefix == "vsql" || prefix == "sys" || prefix == "db" || 
            prefix == "tbl" || prefix == "ref" || prefix.length() <= 3) {
            n = n.substr(underscore + 1);
            underscore = n.find('_');
        } else {
            break;
        }
    }
    return n;
}

std::string expandAbbreviation(const std::string& word) {
    std::string w = to_lower(word);
    if (w == "dept") return "department";
    if (w == "cust") return "customer";
    if (w == "emp") return "employee";
    if (w == "mgr") return "manager";
    if (w == "org") return "organization";
    if (w == "src") return "source";
    if (w == "dest") return "destination";
    if (w == "addr") return "address";
    if (w == "desc") return "description";
    if (w == "prod") return "product";
    if (w == "cat") return "category";
    if (w == "msg") return "message";
    if (w == "pos") return "position";
    if (w == "usr") return "user";
    if (w == "grp") return "group";
    if (w == "auth") return "authority";
    if (w == "info") return "information";
    if (w == "spec") return "specification";
    if (w == "dev") return "development";
    if (w == "pkg") return "package";
    if (w == "txn") return "transaction";
    if (w == "tx") return "transaction";
    if (w == "doc") return "document";
    if (w == "ref") return "reference";
    if (w == "rel") return "relationship";
    if (w == "std") return "student";
    if (w == "sch") return "school";
    if (w == "loc") return "location";
    if (w == "lang") return "language";
    if (w == "qty") return "quantity";
    if (w == "amt") return "amount";
    if (w == "val") return "value";
    if (w == "num") return "number";
    if (w == "no") return "number";
    if (w == "pct") return "percent";
    if (w == "dt") return "date";
    if (w == "cd") return "code";
    if (w == "cl") return "client";
    if (w == "cli") return "client";
    if (w == "srv") return "server";
    if (w == "app") return "application";
    if (w == "cfg") return "configuration";
    if (w == "ctx") return "context";
    if (w == "db") return "database";
    if (w == "dir") return "directory";
    if (w == "env") return "environment";
    if (w == "err") return "error";
    if (w == "fun") return "function";
    if (w == "gen") return "generator";
    if (w == "hdr") return "header";
    if (w == "impl") return "implementation";
    if (w == "lib") return "library";
    if (w == "log") return "logical";
    if (w == "max") return "maximum";
    if (w == "min") return "minimum";
    if (w == "opt") return "option";
    if (w == "param") return "parameter";
    if (w == "prop") return "property";
    if (w == "req") return "request";
    if (w == "res") return "resource";
    if (w == "resp") return "response";
    if (w == "stat") return "status";
    if (w == "sys") return "system";
    if (w == "tmp") return "temporary";
    if (w == "temp") return "temporary";
    if (w == "util") return "utility";
    if (w == "ver") return "version";
    if (w == "vol") return "volume";
    if (w == "yr") return "year";
    if (w == "mth") return "month";
    if (w == "wk") return "week";
    if (w == "hr") return "hour";
    if (w == "min") return "minute";
    if (w == "sec") return "second";
    if (w == "ms") return "millisecond";
    if (w == "int") return "intersection";
    if (w == "nm") return "name";
    if (w == "st") return "status";
    if (w == "nb") return "number";
    if (w == "flg") return "flag";
    if (w == "fk") return "key";
    if (w == "pk") return "key";
    return w;
}

std::string expandAllAbbreviations(const std::string& s) {
    std::string result;
    std::string token;
    std::istringstream tokenStream(to_lower(s));
    bool first = true;
    while (std::getline(tokenStream, token, '_')) {
        if (!first) {
            result += "_";
        }
        result += expandAbbreviation(token);
        first = false;
    }
    return result;
}

std::string stripRolePrefix(const std::string& name) {
    std::string n = to_lower(name);
    std::vector<std::string> roles = {
        "first", "second", "third", "fourth", "one", "two", "primary", "secondary",
        "main", "sub", "old", "new", "parent", "child", "prev", "next"
    };
    for (const auto& role : roles) {
        if (n.rfind(role + "_", 0) == 0) {
            return n.substr(role.length() + 1);
        }
    }
    return n;
}

bool matchCleanTableNames(const std::string& p, const std::string& t) {
    std::string ep = expandAllAbbreviations(p);
    std::string et = expandAllAbbreviations(t);
    if (ep == et) return true;
    if (et == ep + "s" || et == ep + "es") return true;
    if (ep.length() > 1 && ep.back() == 'y') {
        std::string ies = ep.substr(0, ep.length() - 1) + "ies";
        if (et == ies) return true;
    }
    if (ep == et + "s" || ep == et + "es") return true;
    if (et.length() > 1 && et.back() == 'y') {
        std::string ies = et.substr(0, et.length() - 1) + "ies";
        if (ep == ies) return true;
    }
    
    std::string p_clean = ep;
    std::string t_clean = et;
    p_clean.erase(std::remove(p_clean.begin(), p_clean.end(), '_'), p_clean.end());
    t_clean.erase(std::remove(t_clean.begin(), t_clean.end(), '_'), t_clean.end());
    if (p_clean == t_clean) return true;
    if (t_clean == p_clean + "s" || t_clean == p_clean + "es") return true;
    if (p_clean == t_clean + "s" || p_clean == t_clean + "es") return true;
    
    if (p_clean.length() >= 3) {
        if (t_clean.length() >= p_clean.length()) {
            if (t_clean.find(p_clean) == 0 || t_clean.rfind(p_clean) == t_clean.length() - p_clean.length()) return true;
        }
        if (p_clean.length() >= t_clean.length()) {
            if (p_clean.find(t_clean) == 0 || p_clean.rfind(t_clean) == p_clean.length() - t_clean.length()) return true;
        }
    }
    return false;
}

bool matchTableName(const std::string& col_prefix, const std::string& tbl_name) {
    std::string prefix = to_lower(col_prefix);
    std::string tbl = stripSchemaPrefix(to_lower(tbl_name));
    
    std::string clean_tbl = stripTablePrefix(tbl);
    std::string clean_prefix = stripTablePrefix(prefix);
    
    std::string prefix_norole = stripRolePrefix(prefix);
    std::string clean_prefix_norole = stripRolePrefix(clean_prefix);
    
    if (matchCleanTableNames(prefix, tbl)) return true;
    if (matchCleanTableNames(clean_prefix, clean_tbl)) return true;
    if (matchCleanTableNames(prefix, clean_tbl)) return true;
    if (matchCleanTableNames(clean_prefix, tbl)) return true;
    
    if (matchCleanTableNames(prefix_norole, tbl)) return true;
    if (matchCleanTableNames(clean_prefix_norole, clean_tbl)) return true;
    
    return false;
}

bool isTemporalType(const std::string& type) {
    std::string s = to_lower(type);
    return s == "date" || s == "datetime" || s == "timestamp" || s == "time";
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

bool splitColumnName(const std::string& col, std::string& prefix, std::string& suffix) {
    size_t underscore_pos = col.rfind('_');
    if (underscore_pos != std::string::npos && underscore_pos > 0 && underscore_pos < col.length() - 1) {
        prefix = col.substr(0, underscore_pos);
        suffix = col.substr(underscore_pos + 1);
        return true;
    }
    if (col.length() > 1) {
        for (size_t i = col.length() - 1; i > 0; --i) {
            if (std::isupper(col[i]) && (!std::isupper(col[i-1]) || (i + 1 < col.length() && std::islower(col[i+1])))) {
                prefix = col.substr(0, i);
                suffix = col.substr(i);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> getEffectivePKs(const std::string& tbl_name, const TableInfo& info) {
    if (!info.pk_columns.empty()) {
        return info.pk_columns;
    }
    std::vector<std::string> pks;
    std::string tbl_clean = stripTablePrefix(tbl_name);
    std::string tbl_lower = to_lower(tbl_name);
    for (const auto& col_pair : info.column_types) {
        std::string col_lower = to_lower(col_pair.first);
        if (col_lower == "id" || col_lower == tbl_clean + "_id" || col_lower == tbl_clean + "id" || 
            col_lower == tbl_lower + "_id" || col_lower == tbl_lower + "id") {
            pks.push_back(col_pair.first);
        }
    }
    return pks;
}

bool matchMiddleIdConvention(const std::string& col, const std::string& tbl_b) {
    std::string c = to_lower(col);
    size_t pos = c.find("_id_");
    if (pos != std::string::npos && pos > 0) {
        std::string entity = c.substr(0, pos);
        if (matchTableName(entity, tbl_b)) return true;
    }
    if (c.rfind("id_", 0) == 0) {
        std::string entity = c.substr(3);
        if (matchTableName(entity, tbl_b)) return true;
    }
    if (c.rfind("id", 0) == 0 && col.length() > 2 && std::isupper(col[2])) {
        std::string entity = c.substr(2);
        if (matchTableName(entity, tbl_b)) return true;
    }
    return false;
}

bool isSubtypeTable(const std::string& tbl_a, const std::string& tbl_b) {
    std::string a = stripSchemaPrefix(to_lower(tbl_a));
    std::string b = stripSchemaPrefix(to_lower(tbl_b));
    std::string clean_a = stripTablePrefix(a);
    std::string clean_b = stripTablePrefix(b);
    if (clean_a == clean_b) return true;
    if (clean_a.length() > clean_b.length() && clean_a.rfind(clean_b) == clean_a.length() - clean_b.length()) {
        return true;
    }
    if (clean_b.length() > clean_a.length() && clean_b.rfind(clean_a) == clean_b.length() - clean_a.length()) {
        return true;
    }
    return false;
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

std::unordered_map<std::string, TableInfo> getTablesInfo(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::unordered_map<std::string, TableInfo> info_map;
    std::string columns_sql = "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, COLUMN_KEY FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '" + db_name + "' ORDER BY TABLE_NAME, ORDINAL_POSITION";
    auto columns_res = session.sql(columns_sql).execute();
    if (columns_res && !columns_res.has_error()) {
        while (columns_res.next()) {
            std::string tbl = std::string(columns_res.column_str(0));
            std::string col = std::string(columns_res.column_str(1));
            std::string type = std::string(columns_res.column_str(2));
            std::string key = std::string(columns_res.column_str(3));

            auto& info = info_map[tbl];
            info.name = tbl;
            info.column_types[col] = type;
            if (key == "PRI") {
                info.pk_columns.push_back(col);
            }
        }
    }
    return info_map;
}

std::set<Relationship> getExplicitRelationships(
    const std::string& db_name, 
    vsql::preview_sql_query::Session& session, 
    std::set<std::pair<std::string, std::string>>& explicit_mapped_cols) {
    
    std::set<Relationship> relationships;
    std::string keys_sql = "SELECT TABLE_NAME, COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA = '" + db_name + "' AND REFERENCED_TABLE_NAME IS NOT NULL";
    auto keys_res = session.sql(keys_sql).execute();
    if (keys_res && !keys_res.has_error()) {
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
    }
    return relationships;
}

void findImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::set<std::pair<std::string, std::string>>& explicit_mapped_cols,
    std::set<Relationship>& relationships) {

    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (isTemporalType(type_a) || isSystemColumn(col_a)) {
                continue;
            }

            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            // Phase 1: Exact Name Match with a different table (Case-Insensitive)
            if (to_lower(col_a) != "id") {
                for (const auto& tbl_b : table_names) {
                    if (tbl_a == tbl_b) continue;

                    auto it_b = tables_info.find(tbl_b);
                    if (it_b == tables_info.end()) continue;
                    const auto& info_b = it_b->second;

                    std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                    for (const auto& pk_b : pks_b) {
                        if (to_lower(col_a) == to_lower(pk_b)) {
                            auto it_b_col = info_b.column_types.find(pk_b);
                            if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                            }
                        }
                    }
                }
            }

            // Heuristic: Column name matches target table
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                if (matchTableName(col_a, tbl_b)) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                        for (const auto& pk_b : pks_b) {
                            auto it_b_col = info_b.column_types.find(pk_b);
                            if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                            }
                        }
                    }
                }
            }

            // Phase 2: Suffix matching with different tables
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;

                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;

                std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                for (const auto& pk_b : pks_b) {
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                        // Standard suffix matching
                        std::string prefix_a, suffix_a;
                        if (splitColumnName(col_a, prefix_a, suffix_a)) {
                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == to_lower(pk_b)) {
                                suffix_matches = true;
                            } else {
                                std::string prefix_b, suffix_b;
                                if (splitColumnName(pk_b, prefix_b, suffix_b)) {
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
                            }

                            // Heuristic: Column suffix matches target table
                            if (!suffix_matches && matchTableName(suffix_a, tbl_b)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                            }
                        }

                        // Middle _id_ or id[Entity] convention
                        if (matchMiddleIdConvention(col_a, tbl_b)) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_a;
                            rel.to_table = tbl_b;
                            rel.to_column = pk_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                        }
                    }
                }
            }

            // Phase 3: Self-referencing suffix matching (tbl_a == tbl_b)
            do {
                auto it_b = tables_info.find(tbl_a);
                if (it_b == tables_info.end()) break;
                const auto& info_b = it_b->second;

                std::vector<std::string> pks_b = getEffectivePKs(tbl_a, info_b);
                if (pks_b.size() == 1) {
                    const std::string& pk_b = pks_b[0];
                    if (to_lower(col_a) == to_lower(pk_b)) {
                        break;
                    }
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                        std::string prefix_a, suffix_a;
                        if (splitColumnName(col_a, prefix_a, suffix_a)) {
                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == to_lower(pk_b)) {
                                suffix_matches = true;
                            } else {
                                std::string prefix_b, suffix_b;
                                if (splitColumnName(pk_b, prefix_b, suffix_b)) {
                                    if (to_lower(suffix_a) == to_lower(suffix_b)) {
                                        suffix_matches = true;
                                    }
                                }
                            }

                            if (suffix_matches && isSelfReferentialPrefix(prefix_a)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_a;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                            }
                        }
                    }
                }
            } while (false);

            // Heuristic: Self-referencing role matching (same table, role prefix/name matching PK)
            {
                std::vector<std::string> self_ref_words = {
                    "parent", "child", "prev", "previous", "next", "successor", "predecessor", "manager", "mgr", "reports", "report"
                };
                std::string col_lower = to_lower(col_a);
                bool is_self_ref_name = false;
                for (const auto& word : self_ref_words) {
                    if (col_lower.rfind(word, 0) == 0) {
                        is_self_ref_name = true;
                        break;
                    }
                }
                if (is_self_ref_name) {
                    auto it_b = tables_info.find(tbl_a);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        std::vector<std::string> pks_b = getEffectivePKs(tbl_a, info_b);
                        for (const auto& pk_b : pks_b) {
                            auto it_b_col = info_b.column_types.find(pk_b);
                            if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_a;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                            }
                        }
                    }
                }
            }

            // Phase 4: Domain-Specific Keys Matching for Amazon Vendor Central and similar datasets
            matchDomainSpecificKeys(tbl_a, col_a, type_a, table_names, tables_info, relationships);

            // Phase 5: Subtype ID Matching (e.g. mining_parcel.id -> parcel.id)
            if (to_lower(col_a) == "id") {
                for (const auto& tbl_b : table_names) {
                    if (tbl_a == tbl_b) continue;
                    if (isSubtypeTable(tbl_a, tbl_b)) {
                        auto it_b = tables_info.find(tbl_b);
                        if (it_b != tables_info.end()) {
                            const auto& info_b = it_b->second;
                            std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                            for (const auto& pk_b : pks_b) {
                                if (to_lower(pk_b) == "id") {
                                    auto it_b_col = info_b.column_types.find(pk_b);
                                    if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                        Relationship rel;
                                        rel.from_table = tbl_a;
                                        rel.from_column = col_a;
                                        rel.to_table = tbl_b;
                                        rel.to_column = pk_b;
                                        rel.is_explicit = false;
                                        relationships.insert(rel);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Heuristic: Non-ID PK suffix matching (e.g., paciente.pcpf -> pessoa.cpf)
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                    for (const auto& pk_b : pks_b) {
                        if (to_lower(pk_b) != "id") {
                            std::string col_lower = to_lower(col_a);
                            std::string pk_lower = to_lower(pk_b);
                            if (col_lower.length() > pk_lower.length() &&
                                col_lower.rfind(pk_lower) == col_lower.length() - pk_lower.length()) {
                                auto it_b_col = info_b.column_types.find(pk_b);
                                if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = pk_b;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace

bool typeMatches(const std::string& t1, const std::string& t2) {
    std::string s1 = to_lower(t1);
    std::string s2 = to_lower(t2);
    if (s1 == s2) return true;
    
    auto isNumeric = [](const std::string& t) {
        return t.find("int") != std::string::npos ||
               t.find("serial") != std::string::npos ||
               t.find("numeric") != std::string::npos ||
               t.find("number") != std::string::npos ||
               t.find("decimal") != std::string::npos ||
               t.find("double") != std::string::npos ||
               t.find("float") != std::string::npos ||
               t.find("real") != std::string::npos ||
               t.find("identity") != std::string::npos;
    };
    
    auto isString = [](const std::string& t) {
        return t.find("char") != std::string::npos ||
               t.find("text") != std::string::npos ||
               t.find("string") != std::string::npos ||
               t.find("uuid") != std::string::npos;
    };
    
    if (isNumeric(s1) && isNumeric(s2)) return true;
    if (isString(s1) && isString(s2)) return true;
    
    return false;
}

bool isSystemDatabase(const std::string& db) {
    std::string name = to_lower(db);
    return name == "information_schema" || name == "performance_schema" ||
           name == "mysql" || name == "sys";
}

std::string analyzeSchemaRelationships(const std::string& db_name, vsql::preview_sql_query::Session& session) {
    std::vector<std::string> table_names = getTableNames(db_name, session);
    if (table_names.empty()) {
        std::ostringstream oss;
        oss << "Database: " << db_name << "\n\n";
        oss << "Table Connections:\n";
        oss << "------------------\n";
        oss << "(No relationships found)\n";
        return oss.str();
    }

    std::unordered_map<std::string, TableInfo> tables_info = getTablesInfo(db_name, session);
    std::set<std::pair<std::string, std::string>> explicit_mapped_cols;
    std::set<Relationship> relationships = getExplicitRelationships(db_name, session, explicit_mapped_cols);

    findImpliedRelationships(table_names, tables_info, explicit_mapped_cols, relationships);

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
