#include "schema_analyzer.h"
#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include <sstream>
#include <vector>
#include <unordered_map>
#include <set>

namespace {

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

    setDynamicPrefix(detectSharedTablePrefix(table_names));

    // Pass 1: Find all non-subtype relationships
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        std::vector<std::string> pks_a = getEffectivePKs(tbl_a, info_a);

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (isTemporalType(type_a) || isSystemColumn(col_a) || isStatisticColumn(col_a)) {
                continue;
            }

            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            std::string col_a_clean = stripAcronymPrefix(col_a, tbl_a);
            
            // Check if col_a is one of the effective PKs of tbl_a
            bool col_a_is_pk = false;
            if (pks_a.size() == 1) {
                for (const auto& pk_a : pks_a) {
                    if (to_lower(col_a_clean) == to_lower(pk_a)) {
                        col_a_is_pk = true;
                        break;
                    }
                }
            }
            
            // Phase 4: Domain-Specific Keys Matching for Amazon Vendor Central and similar datasets
            matchDomainSpecificKeys(tbl_a, col_a, type_a, table_names, tables_info, relationships);

            for (const auto& tbl_b : table_names) {
                if (isSequenceOrSystemTable(tbl_b)) continue;
                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;

                std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                if (pks_b.empty()) continue;

                bool is_self = (tbl_a == tbl_b);

                if (is_self) {
                    // Self-referencing role match
                    std::vector<std::string> self_ref_words = {
                        "parent", "child", "prev", "previous", "next", "successor", "predecessor", "manager", "mgr", "reports", "report"
                    };
                    std::string col_a_lower = to_lower(col_a_clean);
                    bool is_self_ref_name = false;
                    for (const auto& word : self_ref_words) {
                        if (col_a_lower.rfind(word, 0) == 0) {
                            is_self_ref_name = true;
                            break;
                        }
                    }
                    if (is_self_ref_name) {
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

                    // Self-referencing suffix match
                    for (const auto& pk_b : pks_b) {
                        if (to_lower(col_a_clean) == to_lower(pk_b)) continue;

                        auto it_b_col = info_b.column_types.find(pk_b);
                        if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                            std::string prefix_a, suffix_a;
                            if (splitColumnName(col_a_clean, prefix_a, suffix_a)) {
                                bool suffix_matches = false;
                                if (to_lower(suffix_a) == to_lower(pk_b) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b))) {
                                    suffix_matches = true;
                                } else {
                                    std::string prefix_b_col, suffix_b_col;
                                    if (splitColumnName(pk_b, prefix_b_col, suffix_b_col)) {
                                        if (to_lower(suffix_a) == to_lower(suffix_b_col) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(suffix_b_col))) {
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
                } else {
                    // tbl_a != tbl_b
                    
                    // If col_a is a primary key of tbl_a, it can only match via subtype match (Pass 2)
                    if (col_a_is_pk) {
                        continue;
                    }

                    for (const auto& pk_b : pks_b) {
                        auto it_b_col = info_b.column_types.find(pk_b);
                        if (it_b_col == info_b.column_types.end() || !typesAreSemanticallyCompatible(col_a, type_a, it_b_col->second)) {
                            continue;
                        }

                        // Determine if col_a has an exact match in the schema to avoid looser substring match
                        bool col_has_exact_tbl_match = false;
                        for (const auto& other_tbl : table_names) {
                            if (matchTableName(col_a_clean, other_tbl, false)) {
                                col_has_exact_tbl_match = true;
                                break;
                            }
                        }

                        // Heuristic: Exact match (excluding "id")
                        if (to_lower(col_a_clean) == to_lower(pk_b) && !isGenericIdentifier(col_a_clean)) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_a;
                            rel.to_table = tbl_b;
                            rel.to_column = pk_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                            continue;
                        }

                        // Heuristic: Column name matches target table name
                        if (matchTableName(col_a_clean, tbl_b, !col_has_exact_tbl_match)) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_a;
                            rel.to_table = tbl_b;
                            rel.to_column = pk_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                            continue;
                        }

                        // Heuristic: Column prefix matches target table acronym
                        std::string ref_tbl_acronym = getTableAcronym(tbl_b);
                        if (!ref_tbl_acronym.empty() && ref_tbl_acronym.length() >= 2) {
                            std::string prefix_a, suffix_a;
                            if (splitColumnName(col_a_clean, prefix_a, suffix_a)) {
                                if (to_lower(prefix_a) == ref_tbl_acronym) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = pk_b;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    continue;
                                }
                            }
                        }

                        // Heuristic: User ID fallback match (orders.ID -> user.ID)
                        if (to_lower(col_a_clean) == "id" && to_lower(pk_b) == "id") {
                            std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                            if (isPersonTable(clean_b)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                continue;
                            }
                        }

                        // Heuristic: Direct Role Match to Person Table (without requiring _id suffix)
                        bool type_a_is_numeric = (type_a.find("int") != std::string::npos ||
                                                  type_a.find("serial") != std::string::npos ||
                                                  type_a.find("numeric") != std::string::npos ||
                                                  type_a.find("number") != std::string::npos);
                        if (isPersonRole(to_lower(col_a_clean)) && isGenericIdentifier(pk_b) && type_a_is_numeric) {
                            std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                            if (isPersonTable(clean_b)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = pk_b;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                continue;
                            }
                        }

                        // Heuristic: Alternate key matching rule
                        if (isGenericIdentifier(pk_b)) {
                            std::string prefix_a, suffix_a;
                            if (splitColumnName(col_a_clean, prefix_a, suffix_a)) {
                                if (isGenericIdentifier(suffix_a)) {
                                    bool has_alt_key = false;
                                    for (const auto& col_pair_b : info_b.column_types) {
                                        if (to_lower(col_pair_b.first) == to_lower(prefix_a)) {
                                            has_alt_key = true;
                                            break;
                                        }
                                    }
                                    if (has_alt_key) {
                                        Relationship rel;
                                        rel.from_table = tbl_a;
                                        rel.from_column = col_a;
                                        rel.to_table = tbl_b;
                                        rel.to_column = pk_b;
                                        rel.is_explicit = false;
                                        relationships.insert(rel);
                                        continue;
                                    }
                                }
                            }
                        }

                        // Heuristic: Generic PK/FK prefix match (e.g. nro_pront -> nra_pront)
                        if (isGenericPkFkMatch(col_a_clean, pk_b, pks_b)) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_a;
                            rel.to_table = tbl_b;
                            rel.to_column = pk_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                            continue;
                        }

                        // Heuristic: Suffix matching
                        bool suffix_match = false;
                        std::string prefix_a, suffix_a;
                        if (splitColumnName(col_a_clean, prefix_a, suffix_a)) {
                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == to_lower(pk_b) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b))) {
                                suffix_matches = true;
                            } else {
                                std::string prefix_b_col, suffix_b_col;
                                if (splitColumnName(pk_b, prefix_b_col, suffix_b_col)) {
                                    if (to_lower(suffix_a) == to_lower(suffix_b_col) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(suffix_b_col))) {
                                        suffix_matches = true;
                                    }
                                }
                            }

                            if (suffix_matches) {
                                bool prefix_has_exact = false;
                                for (const auto& other_tbl : table_names) {
                                    if (matchTableName(prefix_a, other_tbl, false)) {
                                        prefix_has_exact = true;
                                        break;
                                    }
                                }

                                bool is_ambiguous = false;
                                if (!prefix_has_exact) {
                                    int prefix_match_count = 0;
                                    for (const auto& other_tbl : table_names) {
                                        if (other_tbl == tbl_a) continue;
                                        if (matchTableName(prefix_a, other_tbl, !prefix_has_exact)) {
                                            prefix_match_count++;
                                        }
                                    }
                                    if (prefix_match_count > 1) {
                                        is_ambiguous = true;
                                    }
                                }

                                if (!is_ambiguous && (matchTableName(prefix_a, tbl_b, !prefix_has_exact) || isPersonMatch(prefix_a, tbl_b) || isLookupMatch(prefix_a, tbl_b, table_names))) {
                                    suffix_match = true;
                                }
                            }

                            // Column suffix matches target table
                            if (!suffix_match) {
                                bool suffix_has_exact = false;
                                for (const auto& other_tbl : table_names) {
                                    if (matchTableName(suffix_a, other_tbl, false)) {
                                        suffix_has_exact = true;
                                        break;
                                    }
                                }

                                bool is_ambiguous = false;
                                if (!suffix_has_exact) {
                                    int suffix_match_count = 0;
                                    for (const auto& other_tbl : table_names) {
                                        if (other_tbl == tbl_a) continue;
                                        if (matchTableName(suffix_a, other_tbl, !suffix_has_exact)) {
                                            suffix_match_count++;
                                        }
                                    }
                                    if (suffix_match_count > 1) {
                                        is_ambiguous = true;
                                    }
                                }

                                if (!is_ambiguous && matchTableName(suffix_a, tbl_b, !suffix_has_exact)) {
                                    suffix_match = true;
                                }
                            }

                            // Last word match
                            if (!suffix_match) {
                                if (matchLastWord(prefix_a, tbl_b)) {
                                    suffix_match = true;
                                }
                            }
                        }

                        // Middle ID convention
                        if (!suffix_match) {
                            bool entity_has_exact = false;
                            std::string c = to_lower(col_a_clean);
                            std::string entity = "";
                            size_t pos = c.find("_id_");
                            if (pos != std::string::npos && pos > 0) {
                                entity = c.substr(0, pos);
                            } else if (c.rfind("id_", 0) == 0) {
                                entity = c.substr(3);
                            } else if (c.rfind("id", 0) == 0 && col_a_clean.length() > 2 && std::isupper(col_a_clean[2])) {
                                entity = c.substr(2);
                            }
                            if (!entity.empty()) {
                                for (const auto& other_tbl : table_names) {
                                    if (matchTableName(entity, other_tbl, false)) {
                                        entity_has_exact = true;
                                        break;
                                    }
                                }
                            }
                            if (matchMiddleIdConvention(col_a_clean, tbl_b, !entity_has_exact)) {
                                suffix_match = true;
                            }
                        }

                        if (suffix_match) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_a;
                            rel.to_table = tbl_b;
                            rel.to_column = pk_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                            continue;
                        }

                        // Heuristic: Non-ID PK suffix match
                        if (to_lower(pk_b) != "id") {
                            if (col_a_clean.length() > pk_b.length()) {
                                std::string col_lower = to_lower(col_a_clean);
                                std::string pk_lower = to_lower(pk_b);
                                if (col_lower.rfind(pk_lower) == col_lower.length() - pk_lower.length()) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = pk_b;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass 1.5: Same Key Column Fallback Match
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;
        
        std::vector<std::string> pks_a = getEffectivePKs(tbl_a, info_a);
        
        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_name_a = col_pair.first;
            const std::string& type_a = col_pair.second;
            std::string col_a = to_lower(col_name_a);
            
            bool col_a_is_pk = false;
            for (const auto& pk_a : pks_a) {
                if (to_lower(col_name_a) == to_lower(pk_a)) {
                    col_a_is_pk = true;
                    break;
                }
            }
            if (col_a_is_pk) continue;
            
            if (isTemporalType(type_a) || isSystemColumn(col_name_a) || isStatisticColumn(col_name_a)) continue;
            
            auto is_key_column = [](const std::string& name) -> bool {
                std::string n = to_lower(name);
                if (n == "id" || n == "uuid" || n == "guid" || n == "uid" || n == "key" || n == "code") return true;
                if (n.length() > 3 && (n.rfind("_id") == n.length() - 3 || n.rfind("_key") == n.length() - 4 || n.rfind("_uid") == n.length() - 4)) return true;
                if (n.length() > 5 && (n.rfind("_code") == n.length() - 5 || n.rfind("_uuid") == n.length() - 5 || n.rfind("_guid") == n.length() - 5)) return true;
                return false;
            };
            if (!is_key_column(col_name_a)) continue;
            
            std::string prefix_a, suffix_a;
            bool has_prefix = splitColumnName(stripAcronymPrefix(col_name_a, tbl_a), prefix_a, suffix_a);
            
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                if (isSequenceOrSystemTable(tbl_b)) continue;
                
                bool already_has_rel = false;
                for (const auto& r : relationships) {
                    if (r.from_table == tbl_a && to_lower(r.from_column) == col_a && r.to_table == tbl_b) {
                        already_has_rel = true;
                        break;
                    }
                }
                if (already_has_rel) continue;
                
                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;
                std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                
                for (const auto& col_pair_b : info_b.column_types) {
                    const std::string& col_name_b = col_pair_b.first;
                    const std::string& type_b = col_pair_b.second;
                    std::string col_b = to_lower(col_name_b);
                    
                    bool col_b_is_pk = false;
                    for (const auto& pk_b : info_b.pk_columns) {
                        if (to_lower(col_name_b) == to_lower(pk_b)) {
                            col_b_is_pk = true;
                            break;
                        }
                    }
                    if (col_b_is_pk) continue;
                    
                    bool col_b_is_fk = false;
                    std::string prefix_b, suffix_b;
                    if (splitColumnName(col_name_b, prefix_b, suffix_b)) {
                        for (const auto& other_tbl : table_names) {
                            if (other_tbl != tbl_b && matchTableName(prefix_b, other_tbl, false)) {
                                col_b_is_fk = true;
                                break;
                            }
                        }
                    }
                    if (col_b_is_fk) continue;
                    
                    if (col_a == col_b && typesAreSemanticallyCompatible(col_name_a, type_a, type_b)) {
                        bool should_match = false;
                        if (pks_b.empty() && info_a.pk_columns.empty()) {
                            bool tbl_b_has_outgoing = false;
                            for (const auto& r : relationships) {
                                if (r.from_table == tbl_b) {
                                    tbl_b_has_outgoing = true;
                                    break;
                                }
                            }
                            if (!tbl_b_has_outgoing) {
                                std::string prefix_b_check, suffix_b_check;
                                if (splitColumnName(col_name_b, prefix_b_check, suffix_b_check)) {
                                    std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                                    std::string p_b = to_lower(prefix_b_check);
                                    std::string s_b = to_lower(suffix_b_check);
                                    if (clean_b.rfind(p_b, 0) == 0 || clean_b == s_b || (clean_b.length() > s_b.length() && clean_b.rfind(s_b, 0) == 0)) {
                                        should_match = true;
                                    }
                                }
                            }
                        } else if (pks_b.empty() && has_prefix && matchTableName(prefix_a, tbl_b)) {
                            should_match = true;
                        }
                        
                        if (should_match) {
                            Relationship rel;
                            rel.from_table = tbl_a;
                            rel.from_column = col_name_a;
                            rel.to_table = tbl_b;
                            rel.to_column = col_name_b;
                            rel.is_explicit = false;
                            relationships.insert(rel);
                        }
                    }
                }
            }
        }
    }

    // Pass 2: Find all subtype relationships (only if no relationship between tbl_a and tbl_b via other columns already exists)
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        std::vector<std::string> pks_a = getEffectivePKs(tbl_a, info_a);

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (isTemporalType(type_a) || isSystemColumn(col_a)) {
                continue;
            }

            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            std::string col_a_clean = stripAcronymPrefix(col_a, tbl_a);

            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                if (isSequenceOrSystemTable(tbl_b)) continue;

                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;

                std::vector<std::string> pks_b = getEffectivePKs(tbl_b, info_b);
                if (pks_b.empty()) continue;

                for (const auto& pk_b : pks_b) {
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col == info_b.column_types.end() || !typeMatches(type_a, it_b_col->second)) {
                        continue;
                    }

                    // Heuristic: Subtype match
                    bool is_subtype = false;
                    bool col_a_is_pk = false;
                    for (const auto& pk_a : pks_a) {
                        if (to_lower(col_a_clean) == to_lower(pk_a)) {
                            col_a_is_pk = true;
                            break;
                        }
                    }
                    if (col_a_is_pk) {
                        // Check if tbl_a already has a relationship to tbl_b via another column
                        bool already_has_rel = false;
                        for (const auto& rel : relationships) {
                            if (rel.from_table == tbl_a && rel.to_table == tbl_b && rel.from_column != col_a) {
                                already_has_rel = true;
                                break;
                            }
                        }
                        if (!already_has_rel) {
                            if (isSubtypeTable(tbl_a, tbl_b)) {
                                bool col_name_matches = false;
                                if (to_lower(col_a_clean) == to_lower(pk_b)) {
                                    col_name_matches = true;
                                } else if (isGenericIdentifier(col_a_clean)) {
                                    col_name_matches = true;
                                } else if (matchTableName(col_a_clean, tbl_b, false)) {
                                    col_name_matches = true;
                                } else {
                                    std::string prefix_a, suffix_a;
                                    if (splitColumnName(col_a_clean, prefix_a, suffix_a)) {
                                        if (matchTableName(prefix_a, tbl_b, false)) {
                                            col_name_matches = true;
                                        } else if (to_lower(suffix_a) == to_lower(pk_b) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b))) {
                                            if (matchTableName(prefix_a, tbl_b, false)) {
                                                col_name_matches = true;
                                            }
                                        }
                                    }
                                }
                                if (col_name_matches) {
                                    is_subtype = true;
                                }
                            } else {
                                std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                                std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                                if (isPersonTable(clean_a) && isPersonTable(clean_b)) {
                                    is_subtype = true;
                                }
                            }
                        }
                    }
                    if (is_subtype) {
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
    clearDynamicPrefix();
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

bool typesAreSemanticallyCompatible(const std::string& col_a, const std::string& type_a, const std::string& type_b) {
    if (!typeMatches(type_a, type_b)) return false;
    
    std::string ta = to_lower(type_a);
    std::string tb = to_lower(type_b);
    std::string col = to_lower(col_a);
    
    bool a_is_bool = (ta.find("tinyint") != std::string::npos || ta.find("bool") != std::string::npos);
    bool b_is_bool = (tb.find("tinyint") != std::string::npos || tb.find("bool") != std::string::npos);
    
    if (a_is_bool != b_is_bool) {
        bool col_is_key = (col == "id" || col == "uid" || col == "uuid" || col == "guid" || col == "key");
        if (!col_is_key) {
            if (col.length() > 3 && (col.rfind("_id") == col.length() - 3 || col.rfind("_cd") == col.length() - 3)) {
                col_is_key = true;
            } else if (col.length() > 4 && (col.rfind("_key") == col.length() - 4 || col.rfind("_uid") == col.length() - 4)) {
                col_is_key = true;
            } else if (col.length() > 5) {
                std::vector<std::string> suffixes = {"_uuid", "_guid", "_code"};
                for (const auto& sfx : suffixes) {
                    if (col.rfind(sfx) == col.length() - sfx.length()) {
                        col_is_key = true;
                        break;
                    }
                }
            }
        }
        if (!col_is_key) {
            return false;
        }
    }
    return true;
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
