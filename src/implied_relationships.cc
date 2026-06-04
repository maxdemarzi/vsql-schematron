#include "implied_relationships.h"
#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include "string_helpers.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {

bool isActivitiTable(const std::string& tbl_name) {
    std::string t = to_lower(tbl_name);
    size_t dot = t.find_last_of('.');
    if (dot != std::string::npos) {
        t = t.substr(dot + 1);
    }
    return (t.rfind("act_ru_", 0) == 0 ||
            t.rfind("act_re_", 0) == 0 ||
            t.rfind("act_ge_", 0) == 0 ||
            t.rfind("act_hi_", 0) == 0 ||
            t.rfind("act_id_", 0) == 0 ||
            t.rfind("act_co_", 0) == 0 ||
            t.rfind("act_fo_", 0) == 0 ||
            t.rfind("act_evt_", 0) == 0 ||
            t.rfind("act_dmn_", 0) == 0);
}

bool isDescriptiveAttribute(const std::string& s) {
    std::string l = to_lower(s);
    static const std::unordered_set<std::string> DESCRIPTIVE_WORDS = {
        "name", "image", "img", "desc", "description", "info", "title", "text",
        "type", "status", "price", "cost", "value", "val", "qty", "quantity",
        "count", "num", "number", "url", "path", "file", "email", "phone",
        "mobile", "address", "date", "time", "datetime", "timestamp",
        "rate", "amount", "amt", "size", "scale", "weight", "oid", "version", "ver"
    };
    return DESCRIPTIVE_WORDS.count(l) > 0;
}

/**
 * Pass 1: Identifies non-subtype implied foreign key relationships.
 *
 * Logic:
 *   Loops through all columns of all tables. If a column name:
 *   1. Is not already explicitly mapped,
 *   2. Matches a naming convention pointing to another table name (e.g. table acronym,
 *      prefix, middle ID convention, or exact suffix match to the target table's primary keys),
 *      and is compatible in data type,
 *   Then it suggests an implied relationship.
 *
 * Examples:
 *   - orders.customer_id -> customers.id  (where customer is a table name prefix / match)
 *   - employees.manager_id -> employees.id (where manager is a self-referential role prefix)
 */
void findPass1ImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::set<std::pair<std::string, std::string>>& explicit_mapped_cols,
    const std::unordered_map<std::string, std::vector<std::string>>& effective_pks,
    std::set<Relationship>& relationships) {

    auto same_expanded_word = [](const std::string& x, const std::string& y) -> bool {
        return to_lower(expandAllAbbreviations(x)) == to_lower(expandAllAbbreviations(y));
    };

    // Iterate through every table (Table A) in the schema
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        // Retrieve precomputed effective primary keys for Table A
        const auto& pks_a = effective_pks.at(tbl_a);

        // Scan every column in Table A
        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            // Heuristic Rule: Skip auditing, temporal, and statistics columns (e.g. "created_date", "modified_by", "item_count")
            if (isTemporalType(type_a) || isSystemColumn(col_a) || isStatisticColumn(col_a)) {
                continue;
            }

            // Heuristic Rule: Skip if the column is already mapped to an explicit constraint
            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            // Strip the table's initials if they prefix the column name (e.g., "oi_quantity" -> "quantity")
            std::string col_a_clean = stripAcronymPrefix(col_a, tbl_a);
            
            // Check if col_a is the primary key of Table A
            bool col_a_is_pk = false;
            if (pks_a.size() == 1) {
                for (const auto& pk_a : pks_a) {
                    if (to_lower(col_a_clean) == to_lower(pk_a)) {
                        col_a_is_pk = true;
                        break;
                    }
                }
            }
            
            // Optimization: Precompute table matching lookups to avoid O(N^3) work in the inner table loop
            bool col_has_exact_tbl_match = false;
            for (const auto& other_tbl : table_names) {
                if (matchTableName(col_a_clean, other_tbl, false)) {
                    col_has_exact_tbl_match = true;
                    break;
                }
            }

            std::string prefix_a, suffix_a;
            bool has_split = splitColumnName(col_a_clean, prefix_a, suffix_a);
            
            // Check if column prefix has an exact match in the schema table names
            bool prefix_has_exact = false;
            bool prefix_is_ambiguous = false;
            if (has_split) {
                for (const auto& other_tbl : table_names) {
                    if (matchTableName(prefix_a, other_tbl, false)) {
                        prefix_has_exact = true;
                        break;
                    }
                }
                // Mark prefix as ambiguous if it loose-matches multiple tables (to avoid incorrect guesses)
                if (!prefix_has_exact) {
                    int prefix_match_count = 0;
                    for (const auto& other_tbl : table_names) {
                        if (other_tbl == tbl_a) continue;
                        if (matchTableName(prefix_a, other_tbl, true)) {
                            prefix_match_count++;
                        }
                    }
                    if (prefix_match_count > 1) {
                        prefix_is_ambiguous = true;
                    }
                }
            }

            // Check if column suffix has an exact match in the schema table names
            bool suffix_has_exact = false;
            bool suffix_is_ambiguous = false;
            if (has_split) {
                for (const auto& other_tbl : table_names) {
                    if (matchTableName(suffix_a, other_tbl, false)) {
                        suffix_has_exact = true;
                        break;
                    }
                }
                // Mark suffix as ambiguous if it loose-matches multiple tables (to avoid incorrect guesses)
                if (!suffix_has_exact) {
                    int suffix_match_count = 0;
                    for (const auto& other_tbl : table_names) {
                        if (other_tbl == tbl_a) continue;
                        if (matchTableName(suffix_a, other_tbl, true)) {
                            suffix_match_count++;
                        }
                    }
                    if (suffix_match_count > 1) {
                        suffix_is_ambiguous = true;
                    }
                }
            }

            // Parse out middle ID conventions (e.g. "customer_id_seq" -> "customer")
            bool entity_has_exact = false;
            std::string c_lower = to_lower(col_a_clean);
            std::string entity = "";
            size_t pos = c_lower.find("_id_");
            if (pos != std::string::npos && pos > 0) {
                entity = c_lower.substr(0, pos);
            } else if (c_lower.rfind("id_", 0) == 0) {
                entity = c_lower.substr(3);
            } else if (c_lower.rfind("id", 0) == 0 && col_a_clean.length() > 2 && std::isupper(col_a_clean[2])) {
                entity = c_lower.substr(2);
            }
            if (!entity.empty()) {
                for (const auto& other_tbl : table_names) {
                    if (matchTableName(entity, other_tbl, false)) {
                        entity_has_exact = true;
                        break;
                    }
                }
            }

            // Custom Rule: Domain-Specific Keys Matching for Amazon Vendor Central and similar datasets
            matchDomainSpecificKeys(tbl_a, col_a, type_a, table_names, tables_info, relationships);

            // Compare Table A's column against all possible target tables (Table B)
            for (const auto& tbl_b : table_names) {
                if (isSequenceOrSystemTable(tbl_b)) continue;
                auto it_b = effective_pks.find(tbl_b);
                if (it_b == effective_pks.end()) continue;
                const auto& pks_b = it_b->second;
                if (pks_b.size() != 1) continue;

                auto it_b_info = tables_info.find(tbl_b);
                if (it_b_info == tables_info.end()) continue;
                const auto& info_b = it_b_info->second;

                bool is_self = (tbl_a == tbl_b);

                // --- Scenario 1: Self-Referencing Heuristics (e.g., employee referencing employee) ---
                if (is_self) {
                    // Heuristic: Self-referencing role match
                    // Example: employees.manager_id -> employees.id
                    std::vector<std::string> self_ref_words = {
                        "parent", "child", "prev", "previous", "next", "successor", "predecessor", "manager", "mgr", "reports", "report", "oid"
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
                            if (to_lower(col_a_clean) == to_lower(pk_b)) continue;
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

                    // Heuristic: Self-referencing suffix match (checking words like parent/child)
                    // Example: node.parent_node_id -> node.node_id
                    for (const auto& pk_b : pks_b) {
                        if (to_lower(col_a_clean) == to_lower(pk_b)) continue;

                        auto it_b_col = info_b.column_types.find(pk_b);
                        if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                            if (has_split) {
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
                    // --- Scenario 2: Cross-Table Heuristics (tbl_a != tbl_b) ---
                    
                    // Primary Key column of Table A can only link cross-table via subtype match (Pass 2)
                    if (col_a_is_pk) {
                        continue;
                    }

                    for (const auto& pk_b : pks_b) {
                        std::string col_lower = to_lower(col_a);
                        if (col_lower.rfind("pay_", 0) == 0 || col_lower.rfind("receive_", 0) == 0) {
                            std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                            if (clean_a == "swap" || clean_a == "swaps") {
                                std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                                if (clean_b == "businessdayconvention" || clean_b == "daycount" || clean_b == "currency") {
                                    continue;
                                }
                            }
                        }

                        auto it_b_col = info_b.column_types.find(pk_b);
                        if (it_b_col == info_b.column_types.end() || !typesAreSemanticallyCompatible(col_a, type_a, it_b_col->second)) {
                            continue;
                        }

                        // Heuristic: Exact match (excluding generic "id")
                        // Example: orders.customer_code -> customers.customer_code
                        if (to_lower(col_a_clean) == to_lower(pk_b) && !isGenericIdentifier(col_a_clean) && !isGenericAttribute(col_a_clean)) {
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
                        // Example: orders.customer -> customers.id
                        bool is_match = false;
                        if (matchTableName(col_a_clean, tbl_b, false)) {
                            is_match = true;
                        } else if (!col_has_exact_tbl_match && matchTableName(col_a_clean, tbl_b, true)) {
                            bool blocked = false;
                            if (isDescriptiveAttribute(col_a_clean)) {
                                blocked = true;
                            }
                            if (has_split) {
                                if (isDescriptiveAttribute(prefix_a) && !same_expanded_word(prefix_a, pk_b) && !matchTableName(prefix_a, tbl_b, true) && !matchTableName(prefix_a, tbl_a, true)) {
                                    blocked = true;
                                }
                                if (isDescriptiveAttribute(suffix_a) && !same_expanded_word(suffix_a, pk_b) && !matchTableName(suffix_a, tbl_b, true) && !matchTableName(suffix_a, tbl_a, true)) {
                                    blocked = true;
                                }
                            }
                            if (!blocked) {
                                is_match = true;
                            }
                        }
                        if (is_match) {
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
                        // Example: items.bp_id -> best_prices.id (where acronym of best_prices is "bp")
                        std::string ref_tbl_acronym = getTableAcronym(tbl_b);
                        if (!ref_tbl_acronym.empty() && ref_tbl_acronym.length() >= 2) {
                            if (has_split) {
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

                        // Heuristic: User ID fallback match
                        // Example: orders.id -> users.id (where "users" is identified as a person table)
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

                        // Heuristic: Direct Role Match to Person Table (without requiring "_id" suffix)
                        // Example: tasks.owner -> users.id
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
                        // Example: Table B has column "customer_code" and PK "id". Column A is "customer_code_id".
                        //          Since B contains a column matching prefix "customer_code", we match to B.id.
                        if (isGenericIdentifier(pk_b)) {
                            if (has_split) {
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

                        // Heuristic: Generic PK/FK prefix match (matching codes/numbers prefixes)
                        // Example: orders.nro_pront -> patients.nra_pront (both "pront" prefix/match)
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
                        // Example: orders.customer_id -> customers.id
                        bool suffix_match = false;
                        if (has_split) {
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
                                if (!prefix_is_ambiguous && (matchTableName(prefix_a, tbl_b, !prefix_has_exact) || isPersonMatch(prefix_a, tbl_b) || isLookupMatch(prefix_a, tbl_b, table_names))) {
                                    if (!(isDescriptiveAttribute(prefix_a) && !same_expanded_word(prefix_a, pk_b) && !matchTableName(prefix_a, tbl_b, true) && !matchTableName(prefix_a, tbl_a, true))) {
                                        suffix_match = true;
                                    }
                                }
                            }

                            // Column suffix matches target table (e.g. orders.id_customer -> customers.id)
                            if (!suffix_match) {
                                if (!suffix_is_ambiguous && matchTableName(suffix_a, tbl_b, !suffix_has_exact)) {
                                    if (!(isDescriptiveAttribute(prefix_a) && !same_expanded_word(prefix_a, pk_b) && !matchTableName(prefix_a, tbl_b, true) && !matchTableName(prefix_a, tbl_a, true))) {
                                        suffix_match = true;
                                    }
                                }
                            }

                            // Suffix match on last word
                            if (!suffix_match) {
                                if (matchLastWord(prefix_a, tbl_b)) {
                                    if (!(isDescriptiveAttribute(suffix_a) && !same_expanded_word(suffix_a, pk_b) && !matchTableName(suffix_a, tbl_b, true) && !matchTableName(suffix_a, tbl_a, true))) {
                                        suffix_match = true;
                                    }
                                }
                            }
                        }

                        // Heuristic: Middle ID convention
                        // Example: orders.customer_id_seq -> customers.id
                        if (!suffix_match) {
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
                        // Example: orders.main_customer_code -> customers.customer_code
                        if (to_lower(pk_b) != "id" && !isGenericIdentifier(pk_b) && !isGenericAttribute(pk_b)) {
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
}

/**
 * Pass 1.5: Identifies same-named key column fallback matches between tables.
 *
 * Logic:
 *   If two tables share a non-primary key column of the same name (such as "company_code" or "department_uid")
 *   and the column is not already classified as a foreign key on either end, and one table represents
 *   the target entity of that name, we suggest a relationship between them.
 *
 * Examples:
 *   - orders.store_code -> stores.store_code (where store_code is shared and stores represents the target entity)
 */
void findPass1_5ImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::unordered_map<std::string, std::vector<std::string>>& effective_pks,
    const std::unordered_map<std::string, std::unordered_map<std::string, bool>>& col_is_fk_cache,
    std::set<Relationship>& relationships) {

    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;
        
        const auto& pks_a = effective_pks.at(tbl_a);
        
        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_name_a = col_pair.first;
            const std::string& type_a = col_pair.second;
            std::string col_a = to_lower(col_name_a);
            
            // Check if column is already Table A's PK
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
                static const std::unordered_set<std::string> DIRECT_KEYS = {
                    "id", "uuid", "guid", "uid", "key", "code"
                };
                if (DIRECT_KEYS.count(n) > 0) return true;
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
                
                // Skip if a relationship is already registered for this column from A to B
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
                const auto& pks_b = effective_pks.at(tbl_b);
                
                for (const auto& col_pair_b : info_b.column_types) {
                    const std::string& col_name_b = col_pair_b.first;
                    const std::string& type_b = col_pair_b.second;
                    std::string col_b = to_lower(col_name_b);
                    
                    // Check if column is Table B's PK
                    bool col_b_is_pk = false;
                    for (const auto& pk_b : info_b.pk_columns) {
                        if (to_lower(col_name_b) == to_lower(pk_b)) {
                            col_b_is_pk = true;
                            break;
                        }
                    }
                    if (col_b_is_pk) continue;
                    
                    // Lookup precomputed FK check to avoid O(N^3 * C^2) matching loops
                    bool col_b_is_fk = col_is_fk_cache.at(tbl_b).at(col_name_b);
                    if (col_b_is_fk) continue;
                    
                    // Match shared same-name key columns (e.g. orders.store_code -> stores.store_code)
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
}

/**
 * Pass 2: Identifies subtype (or ISA) relationships between tables.
 *
 * Logic:
 *   A subtype relationship exists when a primary key of table A references the primary key of table B.
 *   This occurs if:
 *   1. No other relationship exists between A and B,
 *   2. The table names imply hierarchy (e.g., A is a suffix/extension of B),
 *   3. The primary key types match.
 *
 * Examples:
 *   - customers_corporate.customer_id -> customers.customer_id
 */
void findPass2ImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::set<std::pair<std::string, std::string>>& explicit_mapped_cols,
    const std::unordered_map<std::string, std::vector<std::string>>& effective_pks,
    std::set<Relationship>& relationships) {

    // Precompute parent tables from Pass 1 relationships for fast sibling check
    std::unordered_map<std::string, std::unordered_set<std::string>> parent_tables;
    for (const auto& rel : relationships) {
        parent_tables[rel.from_table].insert(rel.to_table);
    }

    // Find all subtype relationships (only if no relationship between tbl_a and tbl_b via other columns already exists)
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        const auto& pks_a = effective_pks.at(tbl_a);

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

                auto it_b_info = tables_info.find(tbl_b);
                if (it_b_info == tables_info.end()) continue;
                const auto& info_b = it_b_info->second;

                const auto& pks_b = effective_pks.at(tbl_b);
                if (pks_b.size() != 1) continue;

                for (const auto& pk_b : pks_b) {
                    auto it_b_col = info_b.column_types.find(pk_b);
                    if (it_b_col == info_b.column_types.end() || !typeMatches(type_a, it_b_col->second)) {
                        continue;
                    }

                    // Heuristic: Subtype match
                    // Example: tbl_a = "customers_corporate", tbl_b = "customers"
                    //          A primary key customer_id in tbl_a references customer_id in tbl_b.
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
                                    bool are_siblings = false;
                                    auto it_pa = parent_tables.find(tbl_a);
                                    auto it_pb = parent_tables.find(tbl_b);
                                    if (it_pa != parent_tables.end() && it_pb != parent_tables.end()) {
                                        for (const auto& p_a : it_pa->second) {
                                            if (p_a != tbl_b && it_pb->second.count(p_a) > 0) {
                                                are_siblings = true;
                                                break;
                                            }
                                        }
                                    }
                                    if (!are_siblings) {
                                        is_subtype = true;
                                    }
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
}

} // namespace

/**
 * The entry point for identifying implied relationships.
 * Hoists prefix detection and precomputes foreign key state to optimize performance,
 * and runs Pass 1, Pass 1.5, and Pass 2 in sequence.
 */
void findImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::set<std::pair<std::string, std::string>>& explicit_mapped_cols,
    std::set<Relationship>& relationships) {

    // 1. Detect dynamic table prefixes
    setDynamicPrefix(detectSharedTablePrefix(table_names));

    // 2. Precompute effective PKs for all tables to avoid O(N^3) calls to getEffectivePKs
    std::unordered_map<std::string, std::vector<std::string>> effective_pks;
    for (const auto& tbl : table_names) {
        auto it = tables_info.find(tbl);
        if (it != tables_info.end()) {
            effective_pks[tbl] = getEffectivePKs(tbl, it->second, table_names, tables_info);
        }
    }

    // 3. Precompute col_b_is_fk mapping for Pass 1.5 (checks if column prefix loose-matches any other table)
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> col_is_fk_cache;
    for (const auto& tbl : table_names) {
        auto it = tables_info.find(tbl);
        if (it != tables_info.end()) {
            for (const auto& col_pair : it->second.column_types) {
                const std::string& col_name = col_pair.first;
                std::string prefix, suffix;
                bool is_fk = false;
                if (splitColumnName(col_name, prefix, suffix)) {
                    for (const auto& other_tbl : table_names) {
                        if (other_tbl != tbl && matchTableName(prefix, other_tbl, false)) {
                            is_fk = true;
                            break;
                        }
                    }
                }
                col_is_fk_cache[tbl][col_name] = is_fk;
            }
        }
    }

    // 4. Run the three relationship finding passes sequentially
    findPass1ImpliedRelationships(table_names, tables_info, explicit_mapped_cols, effective_pks, relationships);
    findPass1_5ImpliedRelationships(table_names, tables_info, effective_pks, col_is_fk_cache, relationships);
    findPass2ImpliedRelationships(table_names, tables_info, explicit_mapped_cols, effective_pks, relationships);

    // Filter out implied relationships on Activiti/Camunda tables
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        if (isActivitiTable(it->from_table) || isActivitiTable(it->to_table)) {
            it = relationships.erase(it);
        } else {
            ++it;
        }
    }

    // 5. Clean up the dynamic table prefix
    clearDynamicPrefix();

}
