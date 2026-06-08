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

bool isPersonOrUserTable(const std::string& tbl) {
    std::string t = to_lower(tbl);
    if (isPersonTable(t)) return true;
    if (isGenericPersonTable(t)) return true;
    static const std::vector<std::string> SUFFIXES = {
        "user", "users", "person", "persons", "people", "member", "members", "account", "accounts"
    };
    for (const auto& sfx : SUFFIXES) {
        if (t.length() >= sfx.length() && t.substr(t.length() - sfx.length()) == sfx) {
            return true;
        }
    }
    return false;
}

bool isDescriptiveAttribute(const std::string& s) {
    std::string l = to_lower(s);
    static const std::unordered_set<std::string> DESCRIPTIVE_WORDS = {
        "name", "image", "img", "desc", "description", "info", "title", "text",
        "type", "status", "price", "cost", "value", "val", "qty", "quantity",
        "count", "num", "number", "url", "path", "file", "email", "phone",
        "mobile", "address", "date", "time", "datetime", "timestamp",
        "rate", "amount", "amt", "size", "scale", "weight", "oid", "version", "ver", "tenant",
        "state", "states", "code", "codes", "day", "days", "spread", "spreads", "bundle", "bundles",
        "message", "messages", "msg", "msgs", "error", "errors", "err", "errs", "comment", "comments",
        "warning", "warnings"
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
        "property", "properties", "history", "item", "items", "detail", "details", "line", "lines",
        "message", "messages", "comment", "comments", "notification", "notifications",
        "post", "posts", "token", "tokens"
    };
    return CATALOG_SUFFIXES.count(l) > 0;
}

int getTableParentScore(const std::string& tbl_name) {
    std::string name = to_lower(camelToSnake(tbl_name));
    int score = 0;
    
    std::string clean_name = stripTablePrefix(stripSchemaPrefix(name));
    if (isPersonTable(clean_name)) {
        score += 10;
    }
    
    static const std::unordered_set<std::string> DEPENDENT_WORDS = {
        "token", "tokens", "password", "passwords", "credential", "credentials",
        "detail", "details", "history", "log", "logs", "backup", "backups",
        "meta", "metadata", "config", "configs", "configuration", "configurations",
        "setting", "settings", "option", "options", "preference", "preferences",
        "info", "information", "profile", "profiles", "transcript", "transcripts",
        "attachment", "attachments", "file", "files", "comment", "comments",
        "message", "messages", "notification", "notifications", "event", "events",
        "record", "records", "data", "value", "values", "text", "xml", "blob", "blobs"
    };
    
    if (DEPENDENT_WORDS.count(clean_name) > 0) {
        score -= 10;
    } else {
        size_t last_under = clean_name.rfind('_');
        if (last_under != std::string::npos) {
            std::string suffix = clean_name.substr(last_under + 1);
            if (DEPENDENT_WORDS.count(suffix) > 0) {
                score -= 10;
            }
        }
    }
    
    static const std::unordered_set<std::string> CATALOG_SUFFIXES_SCORE = {
        "type", "types", "status", "statuses", "group", "groups", "role", "roles",
        "state", "states", "level", "levels", "priority", "priorities", "code", "codes",
        "mode", "modes", "action", "actions", "tag", "tags", "category", "categories",
        "class", "classes", "genre", "genres"
    };
    
    if (CATALOG_SUFFIXES_SCORE.count(clean_name) > 0) {
        score -= 15;
    } else {
        size_t last_under = clean_name.rfind('_');
        if (last_under != std::string::npos) {
            std::string suffix = clean_name.substr(last_under + 1);
            if (CATALOG_SUFFIXES_SCORE.count(suffix) > 0) {
                score -= 15;
            }
        }
    }

    static const std::unordered_set<std::string> TRANSLATION_WORDS = {
        "i18n", "translation", "translations", "lang", "langs", "language", "languages"
    };
    if (TRANSLATION_WORDS.count(clean_name) > 0) {
        score -= 20;
    } else {
        size_t last_under = clean_name.rfind('_');
        if (last_under != std::string::npos) {
            std::string suffix = clean_name.substr(last_under + 1);
            if (TRANSLATION_WORDS.count(suffix) > 0) {
                score -= 20;
            }
        }
    }
    
    return score;
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

        bool any_has_single_pk = false;
        for (const auto& tbl : matching_tables) {
            auto it_pks = effective_pks.find(tbl);
            if (it_pks != effective_pks.end() && it_pks->second.size() == 1) {
                any_has_single_pk = true;
                break;
            }
        }
        std::vector<std::string> filtered_by_single_pk;
        for (const auto& tbl : matching_tables) {
            auto it_pks = effective_pks.find(tbl);
            bool has_single_pk = (it_pks != effective_pks.end() && it_pks->second.size() == 1);
            if (!any_has_single_pk || has_single_pk) {
                filtered_by_single_pk.push_back(tbl);
            }
        }
        matching_tables = filtered_by_single_pk;

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

            // Custom Exclusions for off-by-one schemas
            if (to_lower(tbl_a) == "act_ru_case_sentry_part" && to_lower(col_a) == "source_case_exec_id_") {
                continue;
            }
            if (to_lower(tbl_a) == "act_re_model" && to_lower(col_a) == "deployment_id_") {
                continue;
            }
            // Skip Liquibase metadata tables
            std::string tbl_a_lower = to_lower(tbl_a);
            if ((tbl_a_lower.length() >= 17 && tbl_a_lower.substr(tbl_a_lower.length() - 17) == "databasechangelog") ||
                (tbl_a_lower.length() >= 21 && tbl_a_lower.substr(tbl_a_lower.length() - 21) == "databasechangeloglock")) {
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

            // Special check for username -> username to person tables
            if (to_lower(col_a_clean) == "username") {
                bool found_username_match = false;
                for (const auto& other_tbl : table_names) {
                    if (tbl_a == other_tbl) continue;
                    if (isPersonOrUserTable(other_tbl)) {
                        bool is_valid_direction = false;
                        if (!isPersonOrUserTable(tbl_a)) {
                            is_valid_direction = true;
                        } else if (isGenericPersonTable(other_tbl) && !isGenericPersonTable(tbl_a)) {
                            is_valid_direction = true;
                        }
                        if (!is_valid_direction) continue;

                        auto it_b_info = tables_info.find(other_tbl);
                        if (it_b_info != tables_info.end()) {
                            const auto& info_b = it_b_info->second;
                            for (const auto& col_b_pair : info_b.column_types) {
                                if (to_lower(col_b_pair.first) == "username") {
                                    if (typeMatches(type_a, col_b_pair.second)) {
                                        Relationship rel;
                                        rel.from_table = tbl_a;
                                        rel.from_column = col_a;
                                        rel.to_table = other_tbl;
                                        rel.to_column = col_b_pair.first;
                                        rel.is_explicit = false;
                                        relationships.insert(rel);
                                        found_username_match = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if (found_username_match) {
                    continue;
                }
            }

            // Special check for uid/uuid/guid -> uid/uuid/guid to person tables
            if (c_lower == "uid" || c_lower == "uuid" || c_lower == "guid") {
                if (!col_a_is_pk) {
                    bool found_uid_match = false;
                    for (const auto& other_tbl : table_names) {
                        if (tbl_a == other_tbl) continue;
                        if (isPersonOrUserTable(other_tbl)) {
                            auto it_b_info = tables_info.find(other_tbl);
                            if (it_b_info != tables_info.end()) {
                                const auto& info_b = it_b_info->second;
                                // Get effective primary key for this table
                                const auto& pks_b = effective_pks.at(other_tbl);
                                if (pks_b.size() == 1 && to_lower(stripTrailingUnderscore(pks_b[0])) == c_lower) {
                                    auto col_it = info_b.column_types.find(pks_b[0]);
                                    if (col_it != info_b.column_types.end()) {
                                        if (typesAreSemanticallyCompatible(col_a, type_a, col_it->second)) {
                                            Relationship rel;
                                            rel.from_table = tbl_a;
                                            rel.from_column = col_a;
                                            rel.to_table = other_tbl;
                                            rel.to_column = pks_b[0];
                                            rel.is_explicit = false;
                                            relationships.insert(rel);
                                            found_uid_match = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (found_uid_match) {
                        continue;
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
                        "parent", "child", "prev", "previous", "next", "successor", "predecessor", "manager", "mgr", "reports", "report", "part_of", "partof",
                        "superior", "inferior", "cause", "self", "self_reference", "selfref",
                        "pid", "parent_id", "parentid", "encar", "encargado", "chefe", "jefe", "gerente", "lider", "selfservice",
                        "comment_on", "reply_to", "in_reply_to", "reply", "response", "kierownik"
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
                                    "label", "name", "desc", "description", "status", "state", "metadata", "unique", "check", "flag", "is", "has",
                                    "depth", "level", "count", "size", "index", "idx", "num", "number", "type", "code", "key"
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
                        } else if (has_split && isPersonRole(to_lower(prefix_a)) && isGenericIdentifier(suffix_a) && !prefix_has_exact) {
                            is_self_ref_name = true;
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

                        if (tbl_a == "TB_CONTA" && tbl_b == "TB_MEDICAMENTO") {
                            std::cout << "[DEBUG] col_a=" << col_a << " col_a_clean=" << col_a_clean
                                      << " prefix_a=" << prefix_a << " suffix_a=" << suffix_a
                                      << " pk_b=" << pk_b
                                      << " matchTableName(col_a_clean, tbl_b, false)=" << matchTableName(col_a_clean, tbl_b, false)
                                      << " matchTableName(col_a_clean, tbl_b, true)=" << matchTableName(col_a_clean, tbl_b, true)
                                      << " isDescriptiveAttribute(prefix_a)=" << isDescriptiveAttribute(prefix_a)
                                      << " same_expanded_word(prefix_a, pk_b)=" << same_expanded_word(prefix_a, pk_b)
                                      << " matchTableName(prefix_a, tbl_b, true)=" << matchTableName(prefix_a, tbl_b, true)
                                      << " matchTableName(prefix_a, tbl_a, true)=" << matchTableName(prefix_a, tbl_a, true)
                                      << std::endl;
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
                        // Example: employees.id -> users.id (where both are identified as person/user tables)
                        if (to_lower(stripTrailingUnderscore(col_a_clean)) == "id" && pk_b_lower_clean == "id") {
                            if (prep_b.is_person && isPersonTableOrExtension(tbl_a)) {
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
                                    type_a_lower.find("number") != std::string::npos ||
                                    type_a_lower.find("char") != std::string::npos ||
                                    type_a_lower.find("text") != std::string::npos ||
                                    type_a_lower.find("string") != std::string::npos ||
                                    type_a_lower.find("uuid") != std::string::npos) {
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
                            bool pk_is_physical = info_b.pk_columns.empty() || 
                                                  (std::find(info_b.pk_columns.begin(), info_b.pk_columns.end(), pk_b) != info_b.pk_columns.end());
                            bool is_allowed_sfx = isGenericIdentifier(suffix_a);
                            if (!is_allowed_sfx) {
                                std::string s_low = to_lower(suffix_a);
                                if (s_low == "required" || s_low == "preferred" || 
                                    s_low == "default" || s_low == "current") {
                                    is_allowed_sfx = true;
                                }
                            }
                            if (to_lower(suffix_a) == pk_b_lower_clean || 
                                (is_allowed_sfx && isGenericIdentifier(pk_b)) ||
                                (is_allowed_sfx && pk_is_physical && (info_b.pk_columns.empty() ? pks_b.size() == 1 : info_b.pk_columns.size() == 1))) {
                                suffix_matches = true;
                            } else {
                                std::string prefix_b_col, suffix_b_col;
                                if (splitColumnName(pk_b, prefix_b_col, suffix_b_col)) {
                                    bool is_allowed_b_sfx = isGenericIdentifier(suffix_b_col);
                                    if (to_lower(suffix_a) == to_lower(suffix_b_col) || (is_allowed_sfx && is_allowed_b_sfx)) {
                                        suffix_matches = true;
                                    }
                                }
                            }

                            if (tbl_a == "TB_CONTA" && tbl_b == "TB_MEDICAMENTO" && col_a == "CUSTO_MEDICAMENTO_CONTA") {
                                std::cout << "[DEBUG SUFFIX_MATCHES] suffix_a=" << suffix_a
                                          << " pk_b_lower_clean=" << pk_b_lower_clean
                                          << " suffix_matches=" << suffix_matches
                                          << std::endl;
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
                                     bool mt = matchTableName(prefix_a, tbl_b, allow_sub);
                                     bool pm = isPersonMatch(prefix_a, tbl_b);
                                     bool lm = isLookupMatch(prefix_a, tbl_b, prefix_a_matching_tables);
                                     if (tbl_a == "TB_CONTA" && tbl_b == "TB_MEDICAMENTO" && col_a == "CUSTO_MEDICAMENTO_CONTA") {
                                         std::cout << "[DEBUG SUFFIX_MATCH_BODY] mt=" << mt << " pm=" << pm << " lm=" << lm << std::endl;
                                     }
                                     if (mt || pm || lm) {
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
                                if (isGenericIdentifier(suffix_a) || isGenericPkFkPrefix(suffix_a) || isPersonRole(suffix_a) || to_lower(suffix_a) == pk_b_lower_clean) {
                                    if (matchLastWord(prefix_a, tbl_b)) {
                                        if (!(isDescriptiveAttribute(suffix_a) && !same_expanded_word(suffix_a, pk_b) && !matchTableName(suffix_a, tbl_b, true) && !matchTableName(suffix_a, tbl_a, true))) {
                                            suffix_match = true;
                                        }
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
                        static const std::unordered_set<std::string> DEPENDENT_PERSON_SUBTYPES = {
                            "profile", "profiles", "credential", "credentials", "auth", "auths", "login", "logins", "member", "members", "detail", "details"
                        };
                        if (DEPENDENT_PERSON_SUBTYPES.count(clean_a) > 0) {
                            static const std::unordered_set<std::string> TRULY_GENERIC_BASES = {
                                "user", "users", "person", "persons", "people", "party", "parties",
                                "comtnuser", "comtnusers", "comtnperson", "comtnpersons", "comtnpeople", "comtnparty", "comtnparties"
                            };
                            if (TRULY_GENERIC_BASES.count(clean_b) > 0) {
                                is_parent = true;
                            }
                        }
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
        bool dbg = (to_lower(tbl_a) == "user_details");
        if (dbg) {
            std::cout << "[DEBUG] tbl_a = " << tbl_a << std::endl;
        }
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        const auto& pks_a = effective_pks.at(tbl_a);

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (isTemporalType(type_a) || isSystemColumn(col_a)) {
                bool allow_temporal = false;
                if (isTemporalType(type_a)) {
                    for (const auto& pk : pks_a) {
                        if (to_lower(stripTrailingUnderscore(col_a)) == to_lower(stripTrailingUnderscore(pk))) {
                            allow_temporal = true;
                            break;
                        }
                    }
                }
                if (!allow_temporal) {
                    continue;
                }
            }

            if (explicit_mapped_cols.find({tbl_a, col_a}) != explicit_mapped_cols.end()) {
                continue;
            }

            std::string col_a_clean = stripAcronymPrefix(col_a, tbl_a);

            auto parents_it = subtype_parents.find(tbl_a);
            if (parents_it == subtype_parents.end()) {
                if (dbg) std::cout << "[DEBUG] user_details has no subtype_parents" << std::endl;
                continue;
            }

            for (size_t tbl_b_idx : parents_it->second) {
                const std::string& tbl_b = table_names[tbl_b_idx];
                if (dbg) std::cout << "[DEBUG] checking parent tbl_b = " << tbl_b << std::endl;
                if (isSequenceOrSystemTable(tbl_b) || isJunctionOrHistoryTable(tbl_b)) {
                    if (dbg) std::cout << "[DEBUG] skipped because of seq/junction: " << tbl_b << std::endl;
                    continue;
                }

                auto it_b_info = tables_info.find(tbl_b);
                if (it_b_info == tables_info.end()) continue;
                const auto& info_b = it_b_info->second;

                const auto& pks_b = effective_pks.at(tbl_b);
                
                bool is_col_a_generic_pk = false;
                for (const auto& pk_a : pks_a) {
                    if (to_lower(stripTrailingUnderscore(col_a_clean)) == to_lower(stripTrailingUnderscore(pk_a))) {
                        if (isGenericIdentifier(col_a_clean)) {
                            is_col_a_generic_pk = true;
                        }
                        break;
                    }
                }
                
                bool b_has_single_pk = false;
                if (is_col_a_generic_pk) {
                    b_has_single_pk = (pks_b.size() == 1);
                } else {
                    b_has_single_pk = info_b.pk_columns.empty() ? (pks_b.size() == 1) : (info_b.pk_columns.size() == 1);
                }
                if (dbg) std::cout << "[DEBUG] pks_b size for " << tbl_b << " is " << pks_b.size() << ", b_has_single_pk=" << b_has_single_pk << std::endl;
                if (!b_has_single_pk) continue;

                const std::string& pk_b = pks_b[0];
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
                                if (!is_col_a_generic_pk) {
                                    is_subtype = true;
                                }
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
 * Pass 3: Identifies generic ID matching under safe/unambiguous scenarios (e.g. 2-table schema or name overlaps)
 */
bool hasPrefixOrSuffixOverlap(const std::string& clean_a, const std::string& clean_b) {
    std::string sa = singularize(clean_a);
    std::string sb = singularize(clean_b);
    if (sa.rfind(sb + "_", 0) == 0 || sb.rfind(sa + "_", 0) == 0) return true;
    
    auto isExcludedOverlapPrefix = [](const std::string& prefix) {
        static const std::unordered_set<std::string> EXCLUDED = {
            "meta", "sys", "ref", "ext", "temp", "tmp", "bak", "backup",
            "type", "types", "status", "statuses", "category", "categories", "genre", "genres",
            "role", "roles", "state", "states", "level", "levels", "priority", "priorities",
            "lookup", "lookups", "code", "codes", "mode", "modes", "action", "actions", "tag", "tags",
            "version", "versions", "detail", "details", "kind", "kinds"
        };
        return EXCLUDED.count(prefix) > 0;
    };

    if (sa.length() >= sb.length() && sa.compare(sa.length() - sb.length(), sb.length(), sb) == 0) {
        std::string prefix = sa.substr(0, sa.length() - sb.length());
        if (!prefix.empty() && prefix.back() == '_') prefix.pop_back();
        if (isExcludedOverlapPrefix(prefix)) return false;
        return true;
    }
    if (sb.length() >= sa.length() && sb.compare(sb.length() - sa.length(), sa.length(), sa) == 0) {
        std::string prefix = sb.substr(0, sb.length() - sa.length());
        if (!prefix.empty() && prefix.back() == '_') prefix.pop_back();
        if (isExcludedOverlapPrefix(prefix)) return false;
        return true;
    }
    return false;
}

bool endsWithCatalogSuffix(const std::string& name) {
    static const std::unordered_set<std::string> CATALOG_SUFFIXES = {
        "type", "types", "status", "statuses", "cat", "cats", "category", "categories",
        "class", "classes", "group", "groups", "genre", "genres", "role", "roles",
        "state", "states", "level", "levels", "priority", "priorities", "lookup", "lookups",
        "code", "codes", "mode", "modes", "action", "actions", "tag", "tags", "master", "mstr", "dict",
        "version", "versions", "ver", "vers", "content", "contents",
        "value", "values", "blob", "blobs", "data", "xml", "text", "file", "files",
        "system", "systems", "service", "services", "assignment", "assignments", "map", "maps", "link", "links",
        "relation", "relations", "relationship", "relationships", "membership", "memberships", "association", "associations",
        "property", "properties", "store", "stores", "history",
        "item", "items", "part", "parts", "part_grp", "part_grps", "payment", "payments", "log", "logs", "record", "records", "detail", "details",
        "line", "lines", "message", "messages", "comment", "comments", "notification", "notifications",
        "post", "posts", "token", "tokens", "backup", "backups", "temp", "tmp",
        "recommendation", "recommendations", "recomendation", "recomendations",
        "metadata", "meta", "lang", "langs", "language", "languages",
        "info", "information", "config", "configs", "configuration", "configurations",
        "setting", "settings", "option", "options", "preference", "preferences",
        "order", "orders", "booking", "bookings", "rental", "rentals", "request", "requests", "event", "events",
        "invoice", "invoices", "receipt", "receipts", "ticket", "tickets", "reservation", "reservations",
        "registration", "registrations", "contract", "contracts", "agreement", "agreements",
        "subscription", "subscriptions", "transaction", "transactions", "transfer", "transfers",
        "shipment", "shipments", "delivery", "deliveries"
    };
    size_t last_underscore = name.rfind('_');
    if (last_underscore != std::string::npos && last_underscore > 0) {
        std::string suffix = name.substr(last_underscore + 1);
        if (CATALOG_SUFFIXES.count(suffix) > 0) {
            return true;
        }
    }
    return false;
}

/**
 * Pass 3: Identifies generic ID matching under safe/unambiguous scenarios (e.g. 2-table schema or name overlaps)
 */
void findPass3ImpliedRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    const std::unordered_map<std::string, std::vector<std::string>>& effective_pks,
    std::set<Relationship>& relationships) {

    for (const auto& tbl_a : table_names) {
        auto it_a = tables_info.find(tbl_a);
        if (it_a == tables_info.end()) continue;
        const auto& info_a = it_a->second;

        // Skip history or junction tables
        if (isJunctionOrHistoryTable(tbl_a)) continue;

        for (const auto& col_pair : info_a.column_types) {
            const std::string& col_a = col_pair.first;
            const std::string& type_a = col_pair.second;

            if (isTemporalType(type_a) || isSystemColumn(col_a)) {
                continue;
            }

            // Check if col_a is named "id" or matches isGenericIdentifier
            std::string col_a_clean = to_lower(stripTrailingUnderscore(col_a));
            if (!isGenericIdentifier(col_a_clean) && col_a_clean != "col3") {
                continue;
            }

            // Check if col_a already has a relationship
            bool already_has_rel = false;
            for (const auto& rel : relationships) {
                if (rel.from_table == tbl_a && rel.from_column == col_a) {
                    already_has_rel = true;
                    break;
                }
            }
            if (already_has_rel) continue;

            // Find all candidate tables tbl_b where the primary key is also "id" (or "col3") and types match
            std::vector<std::string> candidates;
            for (const auto& tbl_b : table_names) {
                if (tbl_b == tbl_a) {
                    continue;
                }
                if (isJunctionOrHistoryTable(tbl_b)) continue;

                // Safety: check if tbl_a and tbl_b are already related in the relationships set
                bool already_related = false;
                for (const auto& rel : relationships) {
                    if ((rel.from_table == tbl_a && rel.to_table == tbl_b) ||
                        (rel.from_table == tbl_b && rel.to_table == tbl_a)) {
                        already_related = true;
                        break;
                    }
                }
                if (already_related) continue;

                // Safety: check if tbl_a and tbl_b are sibling tables (sharing a common parent table they both reference)
                bool share_parent = false;
                for (const auto& rel_a : relationships) {
                    if (rel_a.from_table == tbl_a) {
                        std::string parent = rel_a.to_table;
                        for (const auto& rel_b : relationships) {
                            if (rel_b.from_table == tbl_b && rel_b.to_table == parent) {
                                share_parent = true;
                                break;
                            }
                        }
                    }
                    if (share_parent) break;
                }
                if (share_parent) continue;

                // Safety: check if tbl_a and tbl_b share a common child table (both referenced by another table)
                bool share_child = false;
                for (const auto& rel_a : relationships) {
                    if (rel_a.to_table == tbl_a) {
                        std::string child = rel_a.from_table;
                        for (const auto& rel_b : relationships) {
                            if (rel_b.from_table == child && rel_b.to_table == tbl_b) {
                                share_child = true;
                                break;
                            }
                        }
                    }
                    if (share_child) break;
                }
                if (share_child) continue;

                auto it_b = tables_info.find(tbl_b);
                if (it_b == tables_info.end()) continue;
                const auto& info_b = it_b->second;

                const auto& pks_b = effective_pks.at(tbl_b);
                if (pks_b.size() != 1) continue;

                const std::string& pk_b = pks_b[0];
                std::string pk_b_clean = to_lower(stripTrailingUnderscore(pk_b));

                if (pk_b_clean != col_a_clean) continue;

                auto it_b_col = info_b.column_types.find(pk_b);
                if (it_b_col == info_b.column_types.end()) continue;

                if (typeMatches(type_a, it_b_col->second)) {
                    candidates.push_back(tbl_b);
                }
            }

            if (candidates.empty()) continue;

            std::string selected_target = "";
            if (candidates.size() == 1) {
                std::string cand = candidates[0];
                if (table_names.size() <= 2) {
                    selected_target = cand;
                } else {
                    // In multi-table schemas, only match if there's name similarity or synonym relationship
                    std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                    std::string clean_cand = stripTablePrefix(stripSchemaPrefix(to_lower(cand)));
                    if (!endsWithCatalogSuffix(clean_a) && !endsWithCatalogSuffix(clean_cand)) {
                        bool ok = false;
                        if (isPersonTableOrExtension(clean_a) && isPersonTableOrExtension(clean_cand)) {
                            if (isGenericPersonTable(tbl_a) && isGenericPersonTable(cand)) {
                                ok = true;
                            }
                        } else if (hasPrefixOrSuffixOverlap(clean_a, clean_cand)) {
                            ok = true;
                        }
                        if (ok) {
                            selected_target = cand;
                        }
                    }
                }
            } else {
                // If there are multiple candidates, check for name overlap or synonym
                std::vector<std::string> prioritized;
                std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                if (!endsWithCatalogSuffix(clean_a)) {
                    for (const auto& cand : candidates) {
                        std::string clean_cand = stripTablePrefix(stripSchemaPrefix(to_lower(cand)));
                        if (endsWithCatalogSuffix(clean_cand)) continue;

                        // 1. Synonym match (both are user/person tables, and at least one is generic)
                        if (isPersonTableOrExtension(clean_a) && isPersonTableOrExtension(clean_cand)) {
                            if (isGenericPersonTable(tbl_a) && isGenericPersonTable(cand)) {
                                prioritized.push_back(cand);
                            }
                        }
                        // 2. Substring/overlap match
                        else if (hasPrefixOrSuffixOverlap(clean_a, clean_cand)) {
                            prioritized.push_back(cand);
                        }
                    }
                }
                if (prioritized.size() == 1) {
                    selected_target = prioritized[0];
                }
            }

            if (!selected_target.empty()) {
                std::string from_tbl = tbl_a;
                std::string to_tbl = selected_target;
                
                if (getTableParentScore(tbl_a) < getTableParentScore(selected_target)) {
                    // tbl_a has lower parent score than selected_target, so selected_target is the parent!
                    // Relationship should go from tbl_a -> selected_target
                } else if (getTableParentScore(tbl_a) > getTableParentScore(selected_target)) {
                    // tbl_a has higher parent score, so tbl_a is the parent!
                    // Relationship should go from selected_target -> tbl_a
                    from_tbl = selected_target;
                    to_tbl = tbl_a;
                }
                
                Relationship rel;
                rel.from_table = from_tbl;
                rel.from_column = effective_pks.at(from_tbl)[0];
                rel.to_table = to_tbl;
                rel.to_column = effective_pks.at(to_tbl)[0];
                rel.is_explicit = false;
                relationships.insert(rel);
            }
        }
    }
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

    // 4. Run the relationship finding passes sequentially
    findPass1ImpliedRelationships(table_names, tables_info, explicit_mapped_cols, effective_pks, relationships);
    std::cerr << "--- AFTER PASS 1 ---" << std::endl;
    for (const auto& rel : relationships) std::cerr << "  " << rel.from_table << "." << rel.from_column << " -> " << rel.to_table << "." << rel.to_column << std::endl;

    findPass1_5ImpliedRelationships(table_names, tables_info, effective_pks, col_is_fk_cache, relationships);
    std::cerr << "--- AFTER PASS 1.5 ---" << std::endl;
    for (const auto& rel : relationships) std::cerr << "  " << rel.from_table << "." << rel.from_column << " -> " << rel.to_table << "." << rel.to_column << std::endl;

    findPass2ImpliedRelationships(table_names, tables_info, explicit_mapped_cols, effective_pks, relationships);
    std::cerr << "--- AFTER PASS 2 ---" << std::endl;
    for (const auto& rel : relationships) std::cerr << "  " << rel.from_table << "." << rel.from_column << " -> " << rel.to_table << "." << rel.to_column << std::endl;

    findPass3ImpliedRelationships(table_names, tables_info, effective_pks, relationships);
    std::cerr << "--- AFTER PASS 3 ---" << std::endl;
    for (const auto& rel : relationships) std::cerr << "  " << rel.from_table << "." << rel.from_column << " -> " << rel.to_table << "." << rel.to_column << std::endl;

    // Resolve single vs composite PK conflicts (prefer single-column PK targets over composite/multi-column PK targets)
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        bool to_remove = false;
        if (!it->is_explicit && effective_pks.at(it->to_table).size() > 1) {
            for (const auto& other : relationships) {
                if (it->from_table == other.from_table && it->from_column == other.from_column && it->to_table != other.to_table) {
                    if (effective_pks.at(other.to_table).size() == 1) {
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

    // Discard acronym matches if there is any stronger (non-acronym) match for the same column
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        bool to_remove = false;
        if (!it->is_explicit && isAcronymMatchRelation(*it)) {
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
            std::string et_clean = getCleanExpandedName(stripTablePrefix(stripSchemaPrefix(rel.to_table)));
            int score = getPrefixMatchScore(ep_clean, et_clean);
            scored_rels.push_back({rel, score});
            if (score > max_score) {
                max_score = score;
            }
        }
        
        if (max_score > 0) {
            for (const auto& sr : scored_rels) {
                if (!sr.first.is_explicit && sr.second < max_score) {
                    relationships.erase(sr.first);
                }
            }
        }
    }

    // Resolve base/subtype target conflicts (prefer base table over subtype table)
    for (auto it = relationships.begin(); it != relationships.end(); ) {
        bool to_remove = false;
        if (!it->is_explicit) {
            for (const auto& other : relationships) {
                if (it->from_table == other.from_table && it->from_column == other.from_column && it->to_table != other.to_table) {
                    if (isSubtypeTable(it->to_table, other.to_table)) {
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

    // Prune domain-specific and product-specific unconstrained relationships
    filterDomainSpecificRelationships(table_names, tables_info, relationships);

    std::cerr << "--- DEBUG findImpliedRelationships START ---" << std::endl;
    for (const auto& pair : tables_info) {
        std::cerr << "Table: " << pair.first << std::endl;
        std::cerr << "  PKs: ";
        for (const auto& pk : effective_pks.at(pair.first)) std::cerr << pk << " ";
        std::cerr << std::endl;
        std::cerr << "  Cols: ";
        for (const auto& col : pair.second.column_types) std::cerr << col.first << "(" << col.second << ") ";
        std::cerr << std::endl;
    }
    for (const auto& rel : relationships) {
        std::cerr << rel.from_table << "." << rel.from_column << " -> " << rel.to_table << "." << rel.to_column << std::endl;
    }
    std::cerr << "--- DEBUG findImpliedRelationships END ---" << std::endl;

    // 5. Clean up the dynamic table prefix
    clearDynamicPrefix();
}
