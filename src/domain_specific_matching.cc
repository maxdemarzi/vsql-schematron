#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include "name_matching.h"
#include "string_helpers.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

const std::unordered_set<std::string> PERSON_TABLE_SYNONYMS = {
    "user", "users", "app_user", "app_users", "employee", "employees", "staff", 
    "member", "members", "person", "people", "contact", "contacts", "passenger", 
    "passengers", "customer", "customers", "client", "clients", "student", "students", 
    "teacher", "teachers", "entity", "entities", "party", "parties", "player", "players",
    "proponent", "proponents", "proprietor", "proprietors", "investigator", "investigators"
};

const std::unordered_set<std::string> PERSON_ROLE_SYNONYMS = {
    "user", "employee", "staff", "member", "person", "officer", "agent", "manager", 
    "supervisor", "operator", "contact", "author", "creator", "updater", "editor", 
    "owner", "handler", "assignee", "commenter", "accessor", "passenger", "customer", 
    "client", "visitor", "guest", "host", "student", "teacher", "driver", "worker", 
    "admin", "assistant", "delegate", "representative", "rep", "appellant", "defendant", 
    "plaintiff", "vendor", "supplier", "provider", "partner", "merchant", "buyer", 
    "seller", "tenant", "landlord", "holder", "borrower", "lender", "debtor", "creditor", 
    "shipper", "carrier", "objector", "proponent", "proprietor", "advisor", "from", "to",
    "sender", "receiver", "recipient", "profile", "investigator", "invitee", "inviter",
    "judge", "prosecutor", "offender", "complainant", "police", "witness", "attorney",
    "counsel", "lawyer", "defense_att", "oid"
};

const std::unordered_set<std::string> LOOKUP_TABLE_SYNONYMS = {
    "lookup", "lookups", "code_lookup", "reference", "references", "dictionary", 
    "dictionaries", "codelist", "enum_value", "enum_values", "lookup_value", "lookup_values"
};

const std::unordered_set<std::string> GENERIC_PK_FK_PREFIXES = {
    "id", "key", "pk", "fk", "ref", "cod", "code", "cd", "no", "num", "nro", "nra", "nr", "number"
};

} // namespace

/**
 * Matches specialized domain keys for datasets like Amazon Vendor Central.
 * Identifies columns like 'asin', 'upc', 'ean', 'isbn', 'campaign_name', or 'deal_id'
 * and links them to their catalog/summary tables.
 *
 * Examples:
 *   sponsored_products_ads.campaign_name -> ads_sponsored_products_campaigns_vc.campaign_name
 *   order_details.asin -> product_catalog.asin
 */
bool matchDomainSpecificKeys(
    const std::string& tbl_a,
    const std::string& col_a,
    const std::string& type_a,
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    std::set<Relationship>& relationships) {

    bool relationship_found = false;
    std::string col_lower = to_lower(col_a);
    static const std::unordered_set<std::string> DOMAIN_KEYS = {
        "asin", "upc", "ean", "isbn", "isbn-13"
    };
    if (DOMAIN_KEYS.count(col_lower) > 0) {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            if (to_lower(tbl_b) == "product_catalog" || to_lower(tbl_b) == "product" || to_lower(tbl_b) == "products") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    // Check if target table has the same column and type compatible
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == col_lower) {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "campaign_name") {
        // Find the appropriate campaign table
        std::string target_campaign_tbl = "";
        std::string tbl_a_lower = to_lower(tbl_a);
        if (tbl_a_lower.find("sponsored_products") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_products_campaigns_vc";
        } else if (tbl_a_lower.find("sponsored_brands_video") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_brands_video_campaign_vc";
        } else if (tbl_a_lower.find("sponsored_brands") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_brands_campaign_vc";
        } else if (tbl_a_lower.find("sponsored_display") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_display_campaign_vc";
        }
        
        if (!target_campaign_tbl.empty()) {
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                if (to_lower(tbl_b) == target_campaign_tbl) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(col_b_pair.first) == "campaign_name") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "deal_id") {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            if (to_lower(tbl_b) == "advertising_deals_summary") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == "deal_id") {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "pay_offset_fixing_id" || col_lower == "receive_offset_fixing_id" || col_lower == "offset_fixing_id") {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
            if (clean_tbl == "frequency" || clean_tbl == "frequencies") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == "id") {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "mate_prop_id") {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
            if (clean_tbl == "materials" || clean_tbl == "material") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == "id") {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    } else {
        // BPMN / Camunda Specific Rules
        std::string clean_tbl_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
        bool is_hi_a = (clean_tbl_a.rfind("hi_", 0) == 0 || clean_tbl_a.find("_hi_") != std::string::npos ||
                        to_lower(tbl_a).rfind("act_hi_", 0) == 0 || to_lower(tbl_a).find("_hi_") != std::string::npos);

        if (col_lower == "proc_inst_id_" || col_lower == "proc_inst_id" ||
            col_lower == "process_instance_id_" || col_lower == "process_instance_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                bool match = false;
                if (is_hi_a) {
                    if (clean_tbl_b == "procinst" || clean_tbl_b == "hi_procinst" || clean_tbl_b == "processinstance") {
                        match = true;
                    }
                } else {
                    if (clean_tbl_b == "execution" || clean_tbl_b == "ru_execution") {
                        match = true;
                    }
                }
                if (match) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        } else if (col_lower == "proc_def_id_" || col_lower == "proc_def_id" ||
                   col_lower == "process_definition_id_" || col_lower == "process_definition_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (clean_tbl_b == "procdef" || clean_tbl_b == "re_procdef" || clean_tbl_b == "processdefinition") {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        } else if (col_lower == "exception_stack_id_" || col_lower == "exception_stack_id" ||
                   col_lower == "editor_source_value_id_" || col_lower == "editor_source_value_id" ||
                   col_lower == "editor_source_extra_value_id_" || col_lower == "editor_source_extra_value_id" ||
                   col_lower == "custom_values_id_" || col_lower == "custom_values_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (clean_tbl_b == "bytearray" || clean_tbl_b == "ge_bytearray") {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        } else if (col_lower == "super_exec_" || col_lower == "super_exec") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (clean_tbl_b == "execution" || clean_tbl_b == "ru_execution") {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        } else if (col_lower == "case_inst_id_" || col_lower == "case_inst_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                bool match = false;
                if (is_hi_a) {
                    if (clean_tbl_b == "caseinst" || clean_tbl_b == "hi_caseinst") {
                        match = true;
                    }
                } else {
                    if (clean_tbl_b == "case_execution" || clean_tbl_b == "ru_case_execution" || clean_tbl_b == "caseexecution") {
                        match = true;
                    }
                }
                if (match) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        } else if (col_lower == "case_def_id_" || col_lower == "case_def_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (clean_tbl_b == "casedef" || clean_tbl_b == "case_def" || clean_tbl_b == "re_case_def" || clean_tbl_b == "casedefinition" || clean_tbl_b == "case_definition") {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(stripTrailingUnderscore(col_b_pair.first)) == "id") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return relationship_found;
}

/**
 * Checks if a table matches person/user/employee entity synonyms.
 *
 * Examples:
 *   isPersonTable("user") -> true
 *   isPersonTable("employee") -> true
 *   isPersonTable("orders") -> false
 */
bool isPersonTable(const std::string& tbl) {
    return PERSON_TABLE_SYNONYMS.count(tbl);
}

/**
 * Checks if a role word/suffix represents a person role.
 *
 * Examples:
 *   isPersonRole("manager") -> true
 *   isPersonRole("updater") -> true
 *   isPersonRole("status") -> false
 */
bool isPersonRole(const std::string& role) {
    return PERSON_ROLE_SYNONYMS.count(role);
}

/**
 * Evaluates if a column prefix matches a person/user role and the target table is a person table.
 *
 * Example:
 *   Given prefix_a = "created_by", tbl_b = "users" (where users is a person table, and "by" / "creator" matches role):
 *   Returns true (suggesting created_by -> users.id).
 */
bool isPersonMatch(const std::string& prefix_a, const std::string& tbl_b) {
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
    if (PERSON_TABLE_SYNONYMS.count(clean_tbl)) {
        std::vector<std::string> prefix_words;
        std::string word;
        std::istringstream tokenStream(to_lower(prefix_a));
        while (std::getline(tokenStream, word, '_')) {
            if (!word.empty()) prefix_words.push_back(word);
        }
        std::string last_word = prefix_words.empty() ? "" : prefix_words.back();
        if (PERSON_ROLE_SYNONYMS.count(last_word)) {
            return true;
        }
    }
    return false;
}

/**
 * Returns true if target table B is a lookup/reference table and prefix A matches another table name,
 * avoiding matching when the prefix is just a prefix of B itself.
 */
bool isLookupMatch(const std::string& prefix_a, const std::string& tbl_b, const std::vector<std::string>& table_names) {
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
    if (LOOKUP_TABLE_SYNONYMS.count(clean_tbl)) {
        for (const auto& tbl : table_names) {
            if (tbl != tbl_b && matchTableName(prefix_a, tbl)) {
                std::string clean_tbl_other = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
                std::string p_lower = to_lower(prefix_a);
                if (p_lower.find(clean_tbl_other) == 0 && clean_tbl_other != p_lower) {
                    continue;
                }
                return false;
            }
        }
        return true;
    }
    return false;
}

/**
 * Matches columns by their tokenized last word.
 *
 * Example:
 *   Given prefix_a = "shipping_address", tbl_b = "addresses":
 *   Tokenizes prefix_a into {"shipping", "address"} and tbl_b into {"addresses"} (singularized address).
 *   Since last word "address" matches "address", it returns true.
 */
bool matchLastWord(const std::string& prefix_a, const std::string& tbl_b) {
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
    
    std::vector<std::string> tbl_words;
    std::string word;
    std::istringstream tokenStreamT(clean_tbl);
    while (std::getline(tokenStreamT, word, '_')) {
        if (!word.empty()) tbl_words.push_back(word);
    }
    
    std::vector<std::string> prefix_words;
    std::istringstream tokenStreamP(to_lower(prefix_a));
    while (std::getline(tokenStreamP, word, '_')) {
        if (!word.empty()) prefix_words.push_back(word);
    }
    
    if (!tbl_words.empty() && !prefix_words.empty()) {
        if (tbl_words.back() == prefix_words.back() && tbl_words.back().length() >= 3) {
            return true;
        }
    }
    return false;
}

/**
 * Suggests matching when column names use common generic prefix combinations (such as ref, id, key, pk, fk).
 *
 * Example:
 *   Given col_a = "customer_fk", col_b = "customer_pk" (where suffix "fk" and "pk" match, and prefixes are generic):
 *   Returns true.
 */
bool isGenericPkFkMatch(const std::string& col_a, const std::string& col_b, const std::vector<std::string>& pks_b) {
    bool found_pk = false;
    for (const auto& pk : pks_b) {
        if (col_b == pk) { found_pk = true; break; }
    }
    if (!found_pk) return false;
    
    std::string prefix_a, suffix_a;
    std::string prefix_b, suffix_b;
    if (splitColumnName(col_a, prefix_a, suffix_a) && splitColumnName(col_b, prefix_b, suffix_b)) {
        if (to_lower(suffix_a) == to_lower(suffix_b)) {
            std::vector<std::string> pfx_words_a;
            std::string word;
            std::istringstream tokenStreamA(to_lower(prefix_a));
            while (std::getline(tokenStreamA, word, '_')) {
                if (!word.empty()) pfx_words_a.push_back(word);
            }
            
            std::vector<std::string> pfx_words_b;
            std::istringstream tokenStreamB(to_lower(prefix_b));
            while (std::getline(tokenStreamB, word, '_')) {
                if (!word.empty()) pfx_words_b.push_back(word);
            }
            
            if (!pfx_words_a.empty() && !pfx_words_b.empty()) {
                if (GENERIC_PK_FK_PREFIXES.count(pfx_words_a.back()) && GENERIC_PK_FK_PREFIXES.count(pfx_words_b.back())) {
                    return true;
                }
            }
        }
    }
    return false;
}
