#include "implied_relationships.h"
#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include "string_helpers.h"
#include "name_matching.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>
#include <iostream>

namespace {

bool isDescriptiveAttribute(const std::string& s) {
    std::string l = to_lower(s);
    static const std::unordered_set<std::string> DESCRIPTIVE_WORDS = {
        "name", "image", "img", "desc", "description", "info", "title", "text",
        "type", "status", "price", "cost", "value", "val", "qty", "quantity",
        "count", "num", "number", "url", "path", "file", "email", "phone",
        "mobile", "address", "date", "time", "datetime", "timestamp",
        "rate", "amount", "amt", "size", "scale", "weight", "oid", "version", "ver", "tenant",
        "state", "states"
    };
    return DESCRIPTIVE_WORDS.count(l) > 0;
}

// Pre-computed table metadata used to optimize the O(N^2) inner-loop matches to O(1) or O(K) candidates.
struct TablePrep {
    std::string name;
    std::string clean_name;
    std::string acronym;                  // E.g., "bp" for "best_prices"
    bool is_lookup = false;              // E.g., reference or lookup tables
    bool is_person = false;              // E.g., user, member, staff
    bool is_sequence_or_system = false;  // E.g., hibernate_sequence
    bool is_junction_or_history = false; // E.g., user_role_map
    bool is_composite_pk = false;
    std::vector<std::string> pks;
    std::vector<std::string> pks_lower;
    std::vector<std::string> pks_lower_clean;
    std::vector<std::string> pk_types;
    std::unordered_set<std::string> columns_lower;
};

// Caches table name lookup properties and detects if a prefix/suffix query is ambiguous (matches multiple tables).
struct AmbiguityProps {
    bool has_exact = false;
    bool is_ambiguous = false;
    std::string exact_match_tbl = "";
    std::vector<std::string> filtered_matches;
};

bool isCatalogSuffix(const std::string& s) {
    std::string l = to_lower(s);
    static const std::unordered_set<std::string> CATALOG_SUFFIXES = {
        "type", "types", "status", "statuses", "category", "categories",
        "class", "classes", "group", "groups", "genre", "genres", "role", "roles",
        "state", "states", "level", "levels", "priority", "priorities",
        "mode", "modes", "action", "actions", "tag", "tags",
        "version", "versions", "value", "values", "data", "file", "files",
        "map", "maps", "link", "links", "relation", "relations", "relationship", "relationships",
        "property", "properties", "history", "item", "items", "detail", "details", "line", "lines"
    };
    return CATALOG_SUFFIXES.count(l) > 0;
}

bool isGenericPersonTable(const std::string& tbl) {
    std::string clean = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
    static const std::unordered_set<std::string> GENERIC_PERSON_TABLES = {
        "user", "users", "person", "persons", "people", "member", "members", "account", "accounts", "party", "parties",
        "comtnuser", "comtnusers", "comtnperson", "comtnpersons", "comtnpeople", "comtnmember", "comtnmembers", "comtnaccount", "comtnaccounts", "comtnparty", "comtnparties"
    };
    return GENERIC_PERSON_TABLES.count(clean) > 0;
}

std::string getLastWordOfCleanName(const std::string& clean_name) {
    size_t last_under = clean_name.find_last_of('_');
    if (last_under != std::string::npos) {
        return clean_name.substr(last_under + 1);
    }
    return clean_name;
}

AmbiguityProps getAmbiguityProps(const std::string& query, const std::string& tbl_a,
                                 const std::vector<std::string>& table_names,
                                 const std::unordered_map<std::string, std::vector<std::string>>& effective_pks,
                                 const std::unordered_map<std::string, TableInfo>& tables_info,
                                 std::unordered_map<std::string, AmbiguityProps>& cache) {
    auto it = cache.find(query);
    if (it != cache.end()) {
        return it->second;
    }
    
    AmbiguityProps props;
    for (const auto& other_tbl : table_names) {
        if (isJunctionOrHistoryTable(other_tbl)) continue;
        if (matchTableName(query, other_tbl, false)) {
            props.has_exact = true;
            props.exact_match_tbl = other_tbl;
            break;
        }
    }
    if (!props.has_exact) {
        std::vector<std::string> matching_tables;
        for (const auto& other_tbl : table_names) {
            if (other_tbl == tbl_a) continue;
            if (isSequenceOrSystemTable(other_tbl)) continue;
            if (isJunctionOrHistoryTable(other_tbl)) continue;
            auto it_pks = effective_pks.find(other_tbl);
            if (it_pks != effective_pks.end() && it_pks->second.empty()) {
                continue;
            }
            if (matchTableName(query, other_tbl, true)) {
                matching_tables.push_back(other_tbl);
            }
        }
        
        bool any_has_physical_pk = false;
        for (const auto& tbl : matching_tables) {
            auto it_info = tables_info.find(tbl);
            if (it_info != tables_info.end() && !it_info->second.pk_columns.empty()) {
                any_has_physical_pk = true;
                break;
            }
        }
        std::vector<std::string> filtered_by_pk;
        for (const auto& tbl : matching_tables) {
            auto it_info = tables_info.find(tbl);
            bool has_physical_pk = (it_info != tables_info.end() && !it_info->second.pk_columns.empty());
            if (!any_has_physical_pk || has_physical_pk) {
                filtered_by_pk.push_back(tbl);
            }
        }
        matching_tables = filtered_by_pk;

        std::vector<std::string> filtered;
        for (const auto& t1 : matching_tables) {
            bool is_sub = false;
            for (const auto& t2 : matching_tables) {
                if (t1 != t2 && isSubtypeTable(t1, t2)) {
                    is_sub = true;
                    break;
                }
            }
            if (!is_sub) {
                filtered.push_back(t1);
            }
        }
        props.filtered_matches = filtered;
        if (filtered.size() > 1) {
            props.is_ambiguous = true;
        }
    }
    
    cache[query] = props;
    return props;
}

} // namespace

/**
 * Pass 1: Identifies non-subtype implied foreign key relationships.
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

    // Pre-compile index structures to map suffix names, acronyms, table roles, and columns directly to indices.
    // This allows us to select matching table candidates in O(1) or O(K) lookup complexity rather than O(T) full scans.
    std::vector<TablePrep> prepped_tables(table_names.size());
    std::unordered_map<std::string, size_t> table_name_to_idx;
    table_name_to_idx.reserve(table_names.size());
    
    std::unordered_map<std::string, std::vector<size_t>> column_name_to_table_idxs;
    std::unordered_map<std::string, std::vector<size_t>> pk_lower_clean_to_table_idxs;
    std::unordered_map<std::string, std::vector<size_t>> pk_suffix_to_table_idxs;
    std::unordered_map<std::string, std::vector<size_t>> acronym_to_table_idxs;
    std::unordered_map<std::string, std::vector<size_t>> last_word_to_table_idxs;
    std::vector<size_t> non_id_pk_table_idxs;
    std::vector<size_t> person_table_idxs;
    std::vector<size_t> lookup_table_idxs;

    for (size_t i = 0; i < table_names.size(); ++i) {
        const auto& tbl = table_names[i];
        table_name_to_idx[tbl] = i;
        auto& prep = prepped_tables[i];
        prep.name = tbl;
        prep.clean_name = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
        prep.acronym = getTableAcronym(tbl);
        prep.is_lookup = isLookupTable(tbl);
        prep.is_person = isPersonTable(prep.clean_name);
        prep.is_sequence_or_system = isSequenceOrSystemTable(tbl);
        prep.is_junction_or_history = isJunctionOrHistoryTable(tbl);

        auto it = tables_info.find(tbl);
        if (it != tables_info.end()) {
            prep.is_composite_pk = (it->second.pk_columns.size() > 1);
            for (const auto& col_pair : it->second.column_types) {
                std::string col_lower = to_lower(col_pair.first);
                prep.columns_lower.insert(col_lower);
                column_name_to_table_idxs[col_lower].push_back(i);
            }
        }
        
        prep.pks = effective_pks.at(tbl);
        for (const auto& pk : prep.pks) {
            std::string pk_lower = to_lower(pk);
            std::string pk_lower_clean = to_lower(stripTrailingUnderscore(pk));
            prep.pks_lower.push_back(pk_lower);
            prep.pks_lower_clean.push_back(pk_lower_clean);
            
            std::string pk_type = "INT";
            if (it != tables_info.end()) {
                auto col_it = it->second.column_types.find(pk);
                if (col_it != it->second.column_types.end()) {
                    pk_type = col_it->second;
                }
            }
            prep.pk_types.push_back(pk_type);
            
            pk_lower_clean_to_table_idxs[pk_lower_clean].push_back(i);
            
            std::string pk_prefix, pk_suffix;
            if (splitColumnName(pk, pk_prefix, pk_suffix)) {
                pk_suffix_to_table_idxs[to_lower(pk_suffix)].push_back(i);
            } else {
                pk_suffix_to_table_idxs[pk_lower_clean].push_back(i);
            }
        }
        
        if (!prep.acronym.empty()) {
            acronym_to_table_idxs[to_lower(prep.acronym)].push_back(i);
        }
        
        std::string last_word = getLastWordOfCleanName(prep.clean_name);
        if (!last_word.empty()) {
            last_word_to_table_idxs[last_word].push_back(i);
        }
        
        if (prep.is_person) {
            person_table_idxs.push_back(i);
        }
        if (prep.is_lookup) {
            lookup_table_idxs.push_back(i);
        }
        
        bool has_non_id_pk = false;
        for (const auto& pk_clean : prep.pks_lower_clean) {
            if (pk_clean != "id" && !isGenericIdentifier(pk_clean) && !isGenericAttribute(pk_clean)) {
                has_non_id_pk = true;
                break;
            }
        }
        if (has_non_id_pk) {
            non_id_pk_table_idxs.push_back(i);
        }
    }

    std::unordered_map<std::string, AmbiguityProps> prefix_cache;
    std::unordered_map<std::string, AmbiguityProps> suffix_cache;

    // Iterate through every table (Table A) in the schema
    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;
        const auto& pks_a = effective_pks.at(tbl_a);

        // Scan every column in Table A
        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            // Skip relationship_types.dbin_id
            if (to_lower(tbl_a) == "relationship_types" && to_lower(col_a) == "dbin_id") {
                continue;
            }

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
            if (info_a.pk_columns.size() == 1) {
                for (const auto& pk_a : info_a.pk_columns) {
                    if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_a))) {
                        col_a_is_pk = true;
                        break;
                    }
                }
            } else if (pks_a.size() == 1) {
                for (const auto& pk_a : pks_a) {
                    if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_a))) {
                        col_a_is_pk = true;
                        break;
                    }
                }
            }

            std::string prefix_a, suffix_a;
            bool has_split = splitColumnName(col_a_clean, prefix_a, suffix_a);
            
            // Optimization: Precompute table matching lookups to avoid O(N^3) work in the inner table loop
            bool col_has_exact_tbl_match = false;
            std::string match_target = has_split ? prefix_a : col_a_clean;
            for (const auto& other_tbl : table_names) {
                if (matchTableName(match_target, other_tbl, false)) {
                    col_has_exact_tbl_match = true;
                    break;
                }
            }
            
            // Check if column prefix has an exact match in the schema table names
            bool prefix_has_exact = false;
            bool prefix_is_ambiguous = false;
            std::string prefix_exact_match_tbl = "";
            if (has_split) {
                auto props = getAmbiguityProps(prefix_a, tbl_a, table_names, effective_pks, tables_info, prefix_cache);
                prefix_has_exact = props.has_exact;
                prefix_is_ambiguous = props.is_ambiguous;
                prefix_exact_match_tbl = props.exact_match_tbl;
            }

            // Check if column suffix has an exact match in the schema table names
            bool suffix_has_exact = false;
            bool suffix_is_ambiguous = false;
            std::string suffix_exact_match_tbl = "";
            if (has_split) {
                auto props = getAmbiguityProps(suffix_a, tbl_a, table_names, effective_pks, tables_info, suffix_cache);
                suffix_has_exact = props.has_exact;
                suffix_is_ambiguous = props.is_ambiguous;
                suffix_exact_match_tbl = props.exact_match_tbl;
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
            if (matchDomainSpecificKeys(tbl_a, col_a, type_a, table_names, tables_info, relationships)) {
                continue;
            }

            // Optimization: Filter target candidate tables (Table B) using precomputed indexes to avoid O(T) inner loop scans.
            // Candidates are gathered if their names match column prefixes, suffixes, acronyms, or column names.
            std::vector<size_t> candidates;
            candidates.reserve(64);
            
            // ALWAYS add the table itself as a candidate to support self-referencing heuristics
            candidates.push_back(table_name_to_idx.at(tbl_a));
            
            // Heuristic: Add Prisma junction table candidates for columns A and B
            std::string col_a_lower_p = to_lower(col_a_clean);
            if (col_a_lower_p == "a" || col_a_lower_p == "b") {
                std::string t1, t2;
                std::string name_a = stripSchemaPrefix(tbl_a);
                if (!name_a.empty() && name_a[0] == '_') {
                    name_a = name_a.substr(1);
                }
                std::string name_a_lower = to_lower(name_a);
                size_t to_pos = name_a_lower.find("_to_");
                if (to_pos != std::string::npos && to_pos > 0) {
                    t1 = name_a.substr(0, to_pos);
                    t2 = name_a.substr(to_pos + 4);
                } else {
                    to_pos = name_a.find("To");
                    if (to_pos != std::string::npos && to_pos > 0 && to_pos + 2 < name_a.length() && std::isupper(name_a[to_pos + 2])) {
                        t1 = name_a.substr(0, to_pos);
                        t2 = name_a.substr(to_pos + 2);
                    }
                }
                if (!t1.empty() && !t2.empty()) {
                    for (size_t idx = 0; idx < prepped_tables.size(); ++idx) {
                        if (matchTableName(t1, prepped_tables[idx].name, false) || matchTableName(t2, prepped_tables[idx].name, false)) {
                            candidates.push_back(idx);
                        }
                    }
                }
            }
            
            auto add_candidates = [&](const std::vector<size_t>& idxs) {
                candidates.insert(candidates.end(), idxs.begin(), idxs.end());
            };
            
            for (size_t i = 0; i < prepped_tables.size(); ++i) {
                if (matchTableName(col_a_clean, prepped_tables[i].name, true)) {
                    candidates.push_back(i);
                }
            }
            
            {
                auto it = pk_lower_clean_to_table_idxs.find(to_lower(stripTrailingUnderscore(col_a_clean)));
                if (it != pk_lower_clean_to_table_idxs.end()) {
                    add_candidates(it->second);
                }
            }
            
            std::vector<size_t> prefix_a_matches;
            std::vector<size_t> suffix_a_matches;
            if (has_split) {
                for (size_t i = 0; i < prepped_tables.size(); ++i) {
                    if (matchTableName(prefix_a, prepped_tables[i].name, true)) {
                        prefix_a_matches.push_back(i);
                    }
                    if (matchTableName(suffix_a, prepped_tables[i].name, true)) {
                        suffix_a_matches.push_back(i);
                    }
                }
                add_candidates(prefix_a_matches);
                add_candidates(suffix_a_matches);
                
                {
                    auto it = acronym_to_table_idxs.find(to_lower(prefix_a));
                    if (it != acronym_to_table_idxs.end()) {
                        add_candidates(it->second);
                    }
                }
                
                {
                    auto it = pk_suffix_to_table_idxs.find(to_lower(suffix_a));
                    if (it != pk_suffix_to_table_idxs.end()) {
                        add_candidates(it->second);
                    }
                }
                
                if (isGenericIdentifier(suffix_a)) {
                    auto it = column_name_to_table_idxs.find(to_lower(prefix_a));
                    if (it != column_name_to_table_idxs.end()) {
                        add_candidates(it->second);
                    }
                }
                
                if (isPersonRole(prefix_a)) {
                    add_candidates(person_table_idxs);
                }
                
                add_candidates(lookup_table_idxs);
                
                {
                    size_t last_under = prefix_a.find_last_of('_');
                    std::string last_prefix_word = (last_under != std::string::npos) ? prefix_a.substr(last_under + 1) : prefix_a;
                    auto it = last_word_to_table_idxs.find(to_lower(last_prefix_word));
                    if (it != last_word_to_table_idxs.end()) {
                        add_candidates(it->second);
                    }
                }
            } else {
                auto it = pk_suffix_to_table_idxs.find(to_lower(col_a_clean));
                if (it != pk_suffix_to_table_idxs.end()) {
                    add_candidates(it->second);
                }
            }
            
            if (to_lower(stripTrailingUnderscore(col_a_clean)) == "id" || isPersonRole(to_lower(col_a_clean))) {
                add_candidates(person_table_idxs);
            }
            
            if (!entity.empty()) {
                for (size_t i = 0; i < prepped_tables.size(); ++i) {
                    if (matchTableName(entity, prepped_tables[i].name, true)) {
                        candidates.push_back(i);
                    }
                }
            }
            
            add_candidates(non_id_pk_table_idxs);
            
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

            // Compare Table A's column against candidate target tables (Table B)
            for (size_t tbl_b_idx : candidates) {
                const auto& prep_b = prepped_tables[tbl_b_idx];
                const std::string& tbl_b = prep_b.name;

                if (prep_b.is_sequence_or_system || prep_b.is_junction_or_history) continue;
                const auto& pks_b = prep_b.pks;
                if (pks_b.empty()) continue;
                bool is_composite = prep_b.is_composite_pk;

                auto it_b_info = tables_info.find(tbl_b);
                if (it_b_info == tables_info.end()) continue;
                const auto& info_b = it_b_info->second;

                bool is_self = (tbl_a == tbl_b);

                // --- Scenario 1: Self-Referencing Heuristics (e.g., employee referencing employee) ---
                if (is_self) {
                    if (is_composite) continue;
                    bool is_junction = prep_b.is_junction_or_history;
                    if (is_junction) {
                        continue;
                    }
                    // Heuristic: Self-referencing role match
                    // Example: employees.manager_id -> employees.id
                    std::vector<std::string> self_ref_words = {
                        "parent", "child", "prev", "previous", "next", "successor", "predecessor", "manager", "mgr", "reports", "report", "oid", "part_of", "partof",
                        "superior", "inferior", "sub", "cause", "self", "self_reference", "selfref",
                        "pid", "parent_id", "parentid", "encar", "encargado", "chefe", "jefe", "gerente", "lider"
                    };
                    std::string col_a_lower = to_lower(col_a_clean);
                    bool is_self_ref_name = false;
                    for (const auto& word : self_ref_words) {
                        if (col_a_lower == word) {
                            is_self_ref_name = true;
                            break;
                        }
                        size_t pos = col_a_lower.find(word);
                        while (pos != std::string::npos) {
                            bool prev_ok = (pos == 0 || col_a_lower[pos - 1] == '_');
                            bool next_ok = (pos + word.length() == col_a_lower.length() || col_a_lower[pos + word.length()] == '_');
                            if (prev_ok && next_ok) {
                                bool is_flag_or_label = false;
                                static const std::unordered_set<std::string> EXCLUDED_SUFFIXES = {
                                    "label", "name", "desc", "description", "status", "state", "metadata", "unique", "check", "flag", "is", "has"
                                };
                                size_t last_under = col_a_lower.find_last_of('_');
                                if (last_under != std::string::npos) {
                                    std::string suffix = col_a_lower.substr(last_under + 1);
                                    if (EXCLUDED_SUFFIXES.count(suffix) > 0) {
                                        is_flag_or_label = true;
                                    }
                                }
                                if (col_a_lower.rfind("is_", 0) == 0 || col_a_lower.rfind("has_", 0) == 0 || col_a_lower.rfind("show_", 0) == 0) {
                                    is_flag_or_label = true;
                                }
                                if (!is_flag_or_label) {
                                    is_self_ref_name = true;
                                    break;
                                }
                            }
                            pos = col_a_lower.find(word, pos + 1);
                        }
                        if (is_self_ref_name) break;
                    }
                    if (!is_self_ref_name && prep_b.is_person) {
                        if (has_split && isPersonRole(to_lower(suffix_a))) {
                            std::string p_low = to_lower(prefix_a);
                            if (p_low != "is" && p_low != "has" && p_low != "can" && p_low != "was" &&
                                p_low.rfind("is_", 0) != 0 && p_low.rfind("has_", 0) != 0 && p_low.rfind("can_", 0) != 0) {
                                is_self_ref_name = true;
                            }
                        } else if (!has_split && isPersonRole(to_lower(col_a_clean))) {
                            std::string c_low = to_lower(col_a_clean);
                            if (c_low != "admin") {
                                is_self_ref_name = true;
                            }
                        }
                    }
                    if (!is_self_ref_name && matchMiddleIdConvention(col_a_clean, tbl_b, false)) {
                        is_self_ref_name = true;
                    }
                    if (!is_self_ref_name) {
                        std::string clean_tbl_a = prep_b.clean_name;
                        std::string clean_pref_a = to_lower(prefix_a);
                        if (clean_pref_a.find(clean_tbl_a) == 0) {
                            std::string rem = clean_pref_a.substr(clean_tbl_a.length());
                            if (!rem.empty() && rem[0] == '_') rem = rem.substr(1);
                            if ((rem.empty() && (isGenericIdentifier(suffix_a) || std::find(self_ref_words.begin(), self_ref_words.end(), suffix_a) != self_ref_words.end() || isPersonRole(suffix_a))) ||
                                rem.length() == 1 || 
                                std::find(self_ref_words.begin(), self_ref_words.end(), rem) != self_ref_words.end() ||
                                isPersonRole(rem)) {
                                is_self_ref_name = true;
                            }
                        }
                    }
                    if (is_self_ref_name) {
                        for (const auto& pk_b : pks_b) {
                            if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_b))) continue;
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
                        if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_b))) continue;
 
                        auto it_b_col = info_b.column_types.find(pk_b);
                        if (it_b_col != info_b.column_types.end() && typeMatches(type_a, it_b_col->second)) {
                            if (has_split) {
                                bool suffix_matches = false;
                                if (to_lower(suffix_a) == to_lower(stripTrailingUnderscore(pk_b)) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b))) {
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
                    
                    // Heuristic: Prisma/junction table columns 'A' and 'B'
                    // Example: _RoomToUser.A -> Room.id, _RoomToUser.B -> User.id
                    std::string col_a_lower = to_lower(col_a);
                    if (col_a_lower == "a" || col_a_lower == "b") {
                        std::string t1, t2;
                        std::string name_a = stripSchemaPrefix(tbl_a);
                        if (!name_a.empty() && name_a[0] == '_') {
                            name_a = name_a.substr(1);
                        }
                        // Try snake_case first
                        std::string name_a_lower = to_lower(name_a);
                        size_t to_pos = name_a_lower.find("_to_");
                        if (to_pos != std::string::npos && to_pos > 0) {
                            t1 = name_a.substr(0, to_pos);
                            t2 = name_a.substr(to_pos + 4);
                        } else {
                            // Try camelCase "To"
                            to_pos = name_a.find("To");
                            if (to_pos != std::string::npos && to_pos > 0 && to_pos + 2 < name_a.length() && std::isupper(name_a[to_pos + 2])) {
                                t1 = name_a.substr(0, to_pos);
                                t2 = name_a.substr(to_pos + 2);
                            }
                        }
                        
                        if (!t1.empty() && !t2.empty()) {
                            std::string target_tbl = (col_a_lower == "a") ? t1 : t2;
                            if (matchTableName(target_tbl, tbl_b, false)) {
                                // Find pk of tbl_b
                                for (size_t pk_idx = 0; pk_idx < pks_b.size(); ++pk_idx) {
                                    const std::string& pk_b = pks_b[pk_idx];
                                    const std::string& type_b = prep_b.pk_types[pk_idx];
                                    if (isGenericIdentifier(pk_b) && typesAreSemanticallyCompatible(col_a, type_a, type_b)) {
                                        Relationship rel;
                                        rel.from_table = tbl_a;
                                        rel.from_column = col_a;
                                        rel.to_table = tbl_b;
                                        rel.to_column = pk_b;
                                        rel.is_explicit = false;
                                        relationships.insert(rel);
                                        break;
                                    }
                                }
                                continue;
                            }
                        }
                    }

                    // Primary Key column of Table A can only link cross-table via subtype match (Pass 2)
                    if (col_a_is_pk) {
                        continue;
                    }

                    for (size_t pk_idx = 0; pk_idx < pks_b.size(); ++pk_idx) {
                        const std::string& pk_b = pks_b[pk_idx];
                        const std::string& pk_b_lower = prep_b.pks_lower[pk_idx];
                        const std::string& pk_b_lower_clean = prep_b.pks_lower_clean[pk_idx];
                        const std::string& type_b = prep_b.pk_types[pk_idx];

                        if (is_composite) {
                            // If table B has a composite primary key, we allow matching by exact column name match,
                            // but exclude generic columns (like "id", "key", "type") to avoid false-positive relationships.
                            // Example: tbl_a.TRIGGER_NAME -> tbl_b.TRIGGER_NAME (where TRIGGER_NAME is part of composite PK in tbl_b)
                            if (to_lower(col_a) == pk_b_lower && typesAreSemanticallyCompatible(col_a, type_a, type_b)) {
                                static const std::unordered_set<std::string> GENERIC_COMPOSITE_COLS = {
                                    "id", "uuid", "guid", "uid", "key", "code", "name", "value", "desc", "description",
                                    "type", "status", "num", "number", "no", "date", "time", "timestamp",
                                    "sched_name", "schedname", "sched_id", "schedid", "scheduler_name", "schedulername",
                                    "tenant_id", "tenantid", "tenant_code", "tenantcode",
                                    "game_id", "gameid", "id_game", "idgame",
                                    "local_language_id", "locallanguageid", "reg_tenant_id", "regtenantid", "integer_idx", "integeridx", "index_id", "indexid",
                                    "vu_identifikator", "vuidentifikator", "tip_ust", "tipust"
                                };
                                if (GENERIC_COMPOSITE_COLS.count(pk_b_lower_clean) == 0) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = pk_b;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                }
                            }
                            continue;
                        }

                        if (!typesAreSemanticallyCompatible(col_a, type_a, type_b)) {
                            continue;
                        }

                        // Heuristic: Exact match (excluding generic "id")
                        // Example: orders.customer_code -> customers.customer_code
                        if (to_lower(stripTrailingUnderscore(col_a_clean)) == pk_b_lower_clean && !isGenericIdentifier(col_a_clean) && !isGenericAttribute(col_a_clean)) {
                            bool is_physical_pk = (std::find(info_b.pk_columns.begin(), info_b.pk_columns.end(), pk_b) != info_b.pk_columns.end());
                            if (!is_physical_pk && (pk_b_lower_clean == "email" || pk_b_lower_clean == "username" || pk_b_lower_clean == "login")) {
                                continue;
                            }
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
                        std::string ref_tbl_acronym = prep_b.acronym;
                        if (!ref_tbl_acronym.empty() && ref_tbl_acronym.length() >= 2) {
                            if (has_split) {
                                if (to_lower(prefix_a) == to_lower(ref_tbl_acronym)) {
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

                        // Heuristic: URL Referrer matching rule
                        if ((tbl_b == "url" || tbl_b == "urls") && isGenericIdentifier(pk_b)) {
                            if (has_split && isGenericIdentifier(suffix_a)) {
                                std::string p_low = to_lower(prefix_a);
                                if (p_low == "refer" || p_low == "referrer" || p_low == "redirect" || p_low == "forward" || p_low == "link") {
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
                        if (to_lower(stripTrailingUnderscore(col_a_clean)) == "id" && pk_b_lower_clean == "id") {
                            if (prep_b.is_person) {
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
                        bool is_person_role_col = false;
                        if (isPersonRole(to_lower(col_a_clean))) {
                            is_person_role_col = true;
                        } else if (has_split && isPersonRole(to_lower(suffix_a))) {
                            is_person_role_col = true;
                        }
                        
                        if (is_person_role_col && prep_b.is_person && typesAreSemanticallyCompatible(col_a, type_a, type_b)) {
                            bool is_allowed = false;
                            if (isGenericIdentifier(pk_b)) {
                                std::string type_a_lower = to_lower(type_a);
                                if (type_a_lower.find("int") != std::string::npos ||
                                    type_a_lower.find("serial") != std::string::npos ||
                                    type_a_lower.find("numeric") != std::string::npos ||
                                    type_a_lower.find("number") != std::string::npos) {
                                    is_allowed = true;
                                }
                            } else if (pk_b_lower_clean == "username" || pk_b_lower_clean == "email" || 
                                       pk_b_lower_clean == "login" || pk_b_lower_clean == "userid" ||
                                       pk_b_lower_clean == "name" ||
                                       (pk_b_lower_clean.length() > 4 && pk_b_lower_clean.rfind("name") == pk_b_lower_clean.length() - 4)) {
                                bool generic_role = false;
                                std::string r_low = to_lower(col_a_clean);
                                static const std::unordered_set<std::string> SYSTEM_ROLES = {
                                    "user", "owner", "creator", "modifier", "created_by", "updated_by", "modified_by", "by",
                                    "login", "username", "email", "member", "staff", "employee", "agent", "officer", "manager",
                                    "registerer", "registrant", "lead", "leader", "supervisor", "operator", "handler",
                                    "assignee", "commenter", "updater", "editor", "co_author", "coauthor"
                                };
                                if (SYSTEM_ROLES.count(r_low) > 0) {
                                    generic_role = true;
                                } else if (has_split) {
                                    std::string sfx_low = to_lower(suffix_a);
                                    if (SYSTEM_ROLES.count(sfx_low) > 0) {
                                        generic_role = true;
                                    }
                                }
                                if (generic_role) {
                                    is_allowed = true;
                                } else {
                                    if (matchTableName(col_a_clean, tbl_b, true)) {
                                        is_allowed = true;
                                    } else if (has_split && matchTableName(suffix_a, tbl_b, true)) {
                                        is_allowed = true;
                                    }
                                }
                            }

                            if (is_allowed) {
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
                                    bool has_alt_key = (prep_b.columns_lower.count(to_lower(prefix_a)) > 0);
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
                        if (to_lower(col_a_clean).find("_id_") != std::string::npos) {
                            if (matchMiddleIdConvention(col_a_clean, tbl_b, false)) {
                                suffix_match = true;
                            }
                        } else if (has_split) {
                            bool suffix_matches = false;
                            if (to_lower(suffix_a) == pk_b_lower_clean || 
                                (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b)) ||
                                (isGenericIdentifier(suffix_a) && pks_b.size() == 1)) {
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
                                 bool allow_sub = !prefix_has_exact;
                                 if (prefix_has_exact && !prefix_exact_match_tbl.empty() && isSubtypeTable(prefix_exact_match_tbl, tbl_b)) {
                                     allow_sub = true;
                                 }
                                 if (!prefix_is_ambiguous) {
                                     std::vector<std::string> prefix_a_matching_tables;
                                     for (size_t idx : prefix_a_matches) {
                                         prefix_a_matching_tables.push_back(prepped_tables[idx].name);
                                     }
                                     if (matchTableName(prefix_a, tbl_b, allow_sub) || isPersonMatch(prefix_a, tbl_b) || isLookupMatch(prefix_a, tbl_b, prefix_a_matching_tables)) {
                                         if (!(isDescriptiveAttribute(prefix_a) && !same_expanded_word(prefix_a, pk_b) && !matchTableName(prefix_a, tbl_b, true) && !matchTableName(prefix_a, tbl_a, true))) {
                                             suffix_match = true;
                                         }
                                     }
                                 }
                             }

                             // Column suffix matches target table (e.g. orders.id_customer -> customers.id)
                             if (!suffix_match) {
                                 bool allow_sub = !suffix_has_exact && !prefix_has_exact;
                                 if (suffix_has_exact && !suffix_exact_match_tbl.empty() && isSubtypeTable(suffix_exact_match_tbl, tbl_b)) {
                                     allow_sub = true;
                                 }
                                 if (!suffix_is_ambiguous && matchTableName(suffix_a, tbl_b, allow_sub)) {
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
                        if (pk_b_lower != "id" && !isGenericIdentifier(pk_b) && !isGenericAttribute(pk_b)) {
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
                if (to_lower(stripTrailingUnderscore(col_name_a)) == to_lower(stripTrailingUnderscore(pk_a))) {
                    col_a_is_pk = true;
                    break;
                }
            }
            if (col_a_is_pk) continue;
            
            if (isTemporalType(type_a) || isSystemColumn(col_name_a) || isStatisticColumn(col_name_a)) continue;
            
            auto is_key_column = [](const std::string& name) -> bool {
                std::string n = to_lower(stripTrailingUnderscore(name));
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
                if (isSequenceOrSystemTable(tbl_b) || isJunctionOrHistoryTable(tbl_b)) continue;
                
                // Skip if a relationship is already registered for this column from A to ANY table
                bool already_has_rel = false;
                for (const auto& r : relationships) {
                    if (r.from_table == tbl_a && to_lower(r.from_column) == col_a) {
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
                        if (to_lower(stripTrailingUnderscore(col_name_b)) == to_lower(stripTrailingUnderscore(pk_b))) {
                            col_b_is_pk = true;
                            break;
                        }
                    }
                    if (col_b_is_pk) continue;
                    
                    // Lookup precomputed FK check to avoid O(N^3 * C^2) matching loops
                    bool col_b_is_fk = col_is_fk_cache.at(tbl_b).at(col_name_b);
                    if (col_b_is_fk) continue;
                    
                    // Match shared same-name key columns (e.g. orders.store_code -> stores.store_code)
                    if (to_lower(stripTrailingUnderscore(col_name_a)) == to_lower(stripTrailingUnderscore(col_name_b)) && typesAreSemanticallyCompatible(col_name_a, type_a, type_b)) {
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
                            std::string clean_b = stripTablePrefix(stripTableSuffix(stripSchemaPrefix(to_lower(tbl_b))));
                            size_t last_under = clean_b.rfind('_');
                            std::string suffix = (last_under == std::string::npos) ? clean_b : clean_b.substr(last_under + 1);
                            static const std::unordered_set<std::string> JUNCTION_SUFFIXES = {
                                "map", "maps", "link", "links", "relation", "relations",
                                "relationship", "relationships", "membership", "memberships",
                                "association", "associations", "history"
                            };
                            if (JUNCTION_SUFFIXES.count(suffix) == 0 && JUNCTION_SUFFIXES.count(clean_b) == 0) {
                                should_match = true;
                            }
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

    // Precompute subtype/person parent mappings to avoid O(T^2) inner loop search
    std::unordered_map<std::string, std::vector<size_t>> subtype_parents;
    for (size_t i = 0; i < table_names.size(); ++i) {
        const auto& tbl_a = table_names[i];
        const auto& pks_a = effective_pks.at(tbl_a);
        for (size_t j = 0; j < table_names.size(); ++j) {
            if (i == j) continue;
            const auto& tbl_b = table_names[j];
            
            bool is_parent = false;
            if (isSubtypeTable(tbl_a, tbl_b)) {
                is_parent = true;
            } else {
                std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (isPersonTable(clean_a) && isPersonTable(clean_b)) {
                    if (!isGenericPersonTable(tbl_a) && isGenericPersonTable(tbl_b)) {
                        is_parent = true;
                    }
                }
            }
            
            if (!is_parent) {
                // If the primary key of tbl_a matches the name of tbl_b, tbl_b is a parent
                for (const auto& pk_a : pks_a) {
                    std::string pk_a_clean = stripAcronymPrefix(pk_a, tbl_a);
                    if (isGenericIdentifier(pk_a_clean)) continue;
                    if (matchTableName(pk_a_clean, tbl_b, false)) {
                        is_parent = true;
                        break;
                    }
                    std::string prefix_a, suffix_a;
                    if (splitColumnName(pk_a_clean, prefix_a, suffix_a)) {
                        if (isCatalogSuffix(suffix_a)) continue;
                        if (matchTableName(prefix_a, tbl_b, false)) {
                            is_parent = true;
                            break;
                        }
                    }
                }
            }
            
            if (is_parent) {
                subtype_parents[tbl_a].push_back(j);
            }
        }
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

            auto parents_it = subtype_parents.find(tbl_a);
            if (parents_it == subtype_parents.end()) continue;

            for (size_t tbl_b_idx : parents_it->second) {
                const std::string& tbl_b = table_names[tbl_b_idx];
                if (isSequenceOrSystemTable(tbl_b) || isJunctionOrHistoryTable(tbl_b)) continue;

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
                        if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_a))) {
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
                            bool col_name_matches = false;
                            if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_b))) {
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
                                    } else if (to_lower(suffix_a) == to_lower(stripTrailingUnderscore(pk_b)) || (isGenericIdentifier(suffix_a) && isGenericIdentifier(pk_b))) {
                                        if (matchTableName(prefix_a, tbl_b, false)) {
                                            col_name_matches = true;
                                        }
                                    }
                                }
                            }
                            if (col_name_matches) {
                                if (isSubtypeTable(tbl_a, tbl_b)) {
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
                                } else {
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

bool isAcronymMatchRelation(const Relationship& rel) {
    std::string prefix_a, suffix_a;
    std::string col_clean = stripAcronymPrefix(rel.from_column, rel.from_table);
    if (splitColumnName(col_clean, prefix_a, suffix_a)) {
        std::string ref_tbl_acronym = getTableAcronym(rel.to_table);
        if (!ref_tbl_acronym.empty() && to_lower(prefix_a) == ref_tbl_acronym) {
            return true;
        }
    }
    return false;
}

std::string getCleanEntityPrefix(const std::string& col, const std::string& tbl) {
    std::string col_clean = stripTrailingUnderscore(stripAcronymPrefix(col, tbl));
    std::string prefix, suffix;
    if (splitColumnName(col_clean, prefix, suffix)) {
        std::string entity = prefix;
        static const std::vector<std::string> SUFFIXES = {
            "_id", "_uuid", "_guid", "_uid", "_key", "_code", "id", "uuid", "guid", "uid", "key", "code"
        };
        std::string low_entity = to_lower(entity);
        for (const auto& sfx : SUFFIXES) {
            if (low_entity.length() > sfx.length() && low_entity.rfind(sfx) == low_entity.length() - sfx.length()) {
                entity = entity.substr(0, entity.length() - sfx.length());
                break;
            }
        }
        return entity;
    }
    return col_clean;
}

std::string getCleanExpandedName(const std::string& name) {
    std::string exp = expandAllAbbreviations(to_lower(name));
    exp.erase(std::remove(exp.begin(), exp.end(), '_'), exp.end());
    return singularize(exp);
}

int getPrefixMatchScore(const std::string& ep_clean, const std::string& et_clean) {
    if (ep_clean == et_clean) {
        return 1000 + ep_clean.length(); // Huge bonus for exact match
    }
    if (ep_clean.find(et_clean) == 0) {
        return et_clean.length(); // prefix match
    }
    if (et_clean.find(ep_clean) == 0) {
        return ep_clean.length(); // suffix/prefix match from the other side
    }
    return 0;
}

/**
 * The entry point for identifying implied relationships.
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

    // Resolve base/subtype target conflicts (prefer base table over subtype table)
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        bool to_remove = false;
        for (const auto& other : relationships) {
            if (it->from_table == other.from_table && it->from_column == other.from_column && it->to_table != other.to_table) {
                if (isSubtypeTable(it->to_table, other.to_table)) {
                    to_remove = true;
                    break;
                }
            }
        }
        if (to_remove) {
            it = relationships.erase(it);
        } else {
            ++it;
        }
    }

    // Discard acronym matches if there is any stronger (non-acronym) match for the same column
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        bool to_remove = false;
        if (isAcronymMatchRelation(*it)) {
            for (const auto& other : relationships) {
                if (it->from_table == other.from_table && it->from_column == other.from_column && it->to_table != other.to_table) {
                    if (!isAcronymMatchRelation(other)) {
                        to_remove = true;
                        break;
                    }
                }
            }
        }
        if (to_remove) {
            it = relationships.erase(it);
        } else {
            ++it;
        }
    }

    // Resolve overlapping prefix conflicts (prefer more specific matches)
    std::unordered_map<std::string, std::vector<Relationship>> groups;
    for (const auto& rel : relationships) {
        std::string key = rel.from_table + "." + to_lower(rel.from_column);
        groups[key].push_back(rel);
    }

    for (const auto& pair : groups) {
        if (pair.second.size() < 2) continue;
        
        int max_score = -1;
        std::vector<std::pair<Relationship, int>> scored_rels;
        for (const auto& rel : pair.second) {
            std::string entity = getCleanEntityPrefix(rel.from_column, rel.from_table);
            std::string ep_clean = getCleanExpandedName(entity);
            std::string et_clean = getCleanExpandedName(stripSchemaPrefix(rel.to_table));
            int score = getPrefixMatchScore(ep_clean, et_clean);
            scored_rels.push_back({rel, score});
            if (score > max_score) {
                max_score = score;
            }
        }
        
        if (max_score > 0) {
            for (const auto& sr : scored_rels) {
                if (sr.second < max_score) {
                    relationships.erase(sr.first);
                }
            }
        }
    }

    // 5. Clean up the dynamic table prefix
    clearDynamicPrefix();
}
