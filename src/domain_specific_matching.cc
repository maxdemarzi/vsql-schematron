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
    "member", "members", "person", "persons", "people", "contact", "contacts", "passenger", 
    "passengers", "customer", "customers", "client", "clients", "student", "students", 
    "teacher", "teachers", "entity", "entities", "party", "parties", "player", "players",
    "proponent", "proponents", "proprietor", "proprietors", "investigator", "investigators",
    "broker", "brokers", "agent", "agents", "officer", "officers", "doctor", "doctors",
    "patient", "patients", "driver", "drivers", "worker", "workers", "vendor", "vendors",
    "supplier", "suppliers", "provider", "providers", "buyer", "buyers", "seller", "sellers",
    "tenant", "tenants", "landlord", "landlords", "owner", "owners", "partner", "partners",
    "profile", "profiles", "guest", "guests", "visitor", "visitors",
    "representative", "representatives", "rep", "reps", "manager", "managers", "creator",
    "creators", "author", "authors", "advisor", "advisors", "police", "witness", "witnesses",
    "attorney", "attorneys", "lawyer", "lawyers", "judge", "judges", "prosecutor", "prosecutors",
    "defendant", "defendants", "plaintiff", "plaintiffs",
    "account", "accounts", "auth", "auths", "login", "logins", "credentials", "credential",
    "instructor", "instructors", "professor", "professors",
    "follower", "followers", "friend", "friends", "moderator", "moderators"
};

const std::unordered_set<std::string> PERSON_ROLE_SYNONYMS = {
    "user", "employee", "staff", "member", "person", "officer", "agent", "manager", 
    "registerer", "registrant", "leader", "lead", "grantee", "grantor",
    "supervisor", "operator", "contact", "author", "creator", "updater", "editor", 
    "owner", "handler", "assignee", "commenter", "accessor", "passenger", "customer", 
    "client", "visitor", "guest", "student", "teacher", "driver", "worker", 
    "admin", "assistant", "delegate", "representative", "rep", "appellant", "defendant", 
    "plaintiff", "vendor", "supplier", "provider", "partner", "merchant", "buyer", 
    "seller", "tenant", "landlord", "holder", "borrower", "lender", "debtor", "creditor", 
    "shipper", "carrier", "objector", "proponent", "proprietor", "advisor", "from", "to",
    "sender", "receiver", "recipient", "profile", "investigator", "invitee", "inviter",
    "judge", "prosecutor", "offender", "complainant", "police", "witness", "attorney",
    "counsel", "lawyer", "defense_att", "oid",
    "by", "created_by", "updated_by", "modified_by", "modifier",
    "captain", "vencedor", "vendedor", "comprador",
    "follower", "followers", "friend", "friends", "curator", "curators", "moderator", "moderators",
    "respondent", "respondents", "applicant", "applicants", "parent", "parents", "patient", "patients",
    "trainer", "trainers", "attendee", "attendees", "participant", "participants", "candidate", "candidates",
    "volunteer", "volunteers", "organizer", "organizers", "instructor", "instructors", "professor", "professors",
    "responsavel", "responsaveis"
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
    std::string tbl_a_lower = to_lower(tbl_a);
    std::string col_a_lower = to_lower(col_a);



    // Custom domain rule: Map parent/child roles in role hierarchy tables to the author/role definition code.
    // Example: roles_hierarchy.parnts_role -> authorinfo.author_code
    if (tbl_a_lower.find("roles_hierarchy") != std::string::npos || tbl_a_lower.find("roleshierarchy") != std::string::npos) {
        if (col_a_lower == "parnts_role" || col_a_lower == "chldrn_role" || col_a_lower == "parent_role" || col_a_lower == "child_role") {
            for (const auto& tbl_b : table_names) {
                std::string tbl_b_lower = to_lower(tbl_b);
                if (tbl_b_lower.find("authorinfo") != std::string::npos || tbl_b_lower.find("author_info") != std::string::npos) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        for (const auto& col_b_pair : it_b->second.column_types) {
                            std::string col_b_lower = to_lower(col_b_pair.first);
                            if (col_b_lower == "author_code" || col_b_lower == "authorcode") {
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
    if (relationship_found) return true;

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
                   col_lower == "custom_values_id_" || col_lower == "custom_values_id" ||
                   col_lower == "error_details_id_" || col_lower == "error_details_id" ||
                   col_lower == "info_json_id_" || col_lower == "info_json_id") {
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
        } else if (col_lower == "seed_job_def_id_" || col_lower == "seed_job_def_id" ||
                   col_lower == "monitor_job_def_id_" || col_lower == "monitor_job_def_id" ||
                   col_lower == "batch_job_def_id_" || col_lower == "batch_job_def_id") {
            for (const auto& tbl_b : table_names) {
                std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                if (clean_tbl_b == "jobdef" || clean_tbl_b == "ru_jobdef" || clean_tbl_b == "job_def" || clean_tbl_b == "ru_job_def") {
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
        } else if (col_lower == "parnts_role" || col_lower == "chldrn_role" || col_lower == "parents_role" || col_lower == "children_role" || col_lower == "parent_role" || col_lower == "child_role") {
            // Check if the current table is a role hierarchy table and the target is an author/role info table.
            // Example: roles_hierarchy.parnts_role -> authorinfo.author_code
            std::string clean_tbl_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
            if (clean_tbl_a == "roles_hierarchy" || clean_tbl_a == "comtnroles_hierarchy" || clean_tbl_a == "role_hierarchy" || clean_tbl_a == "comtnrole_hierarchy") {
                for (const auto& tbl_b : table_names) {
                    if (tbl_a == tbl_b) continue;
                    std::string clean_tbl_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                    if (clean_tbl_b == "authorinfo" || clean_tbl_b == "comtnauthorinfo" || clean_tbl_b == "author_info" || clean_tbl_b == "comtnauthor_info") {
                        auto it_b = tables_info.find(tbl_b);
                        if (it_b != tables_info.end()) {
                            const auto& info_b = it_b->second;
                            for (const auto& col_b_pair : info_b.column_types) {
                                std::string col_b_lower = to_lower(col_b_pair.first);
                                if (col_b_lower == "author_code" || col_b_lower == "authorcode") {
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
    std::string clean = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
    if (PERSON_TABLE_SYNONYMS.count(clean) > 0) return true;
    
    std::vector<std::string> words;
    std::string word;
    std::istringstream ss(clean);
    while (std::getline(ss, word, '_')) {
        words.push_back(word);
    }
    
    if (words.empty()) return false;
    
    bool has_person_word = false;
    for (const auto& w : words) {
        if (PERSON_TABLE_SYNONYMS.count(w) > 0) {
            has_person_word = true;
            break;
        }
    }
    if (!has_person_word) return false;
    
    static const std::unordered_set<std::string> EXCLUDED_BASE_SUFFIXES = {
        "profile", "profiles", "group", "groups", "role", "roles", "history", "log", "logs", 
        "detail", "details", "map", "maps", "link", "links", "assignment", "assignments",
        "registration", "registrations", "booking", "bookings", "reservation", "reservations",
        "schedule", "schedules", "event", "events", "permission", "permissions", "privilege", "privileges",
        "activity", "activities", "action", "actions", "metadata", "meta", "config", "configs",
        "setting", "settings", "option", "options", "preference", "preferences", "token", "tokens"
    };
    
    std::string last_word = words.back();
    if (EXCLUDED_BASE_SUFFIXES.count(last_word) > 0) {
        return false;
    }
    
    return true;
}

bool isPersonTableOrExtension(const std::string& tbl) {
    std::string clean = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
    if (PERSON_TABLE_SYNONYMS.count(clean) > 0) return true;
    std::string word;
    std::istringstream ss(clean);
    while (std::getline(ss, word, '_')) {
        if (PERSON_TABLE_SYNONYMS.count(word) > 0) return true;
    }
    return false;
}

bool isGenericPersonTable(const std::string& tbl) {
    std::string clean = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
    static const std::unordered_set<std::string> GENERIC_PERSON_TABLES = {
        "user", "users", "person", "persons", "people", "member", "members", "account", "accounts", "party", "parties",
        "auth", "auths", "login", "logins", "credentials", "credential",
        "comtnuser", "comtnusers", "comtnperson", "comtnpersons", "comtnpeople", "comtnmember", "comtnmembers", "comtnaccount", "comtnaccounts", "comtnparty", "comtnparties"
    };
    return GENERIC_PERSON_TABLES.count(clean) > 0;
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
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(tbl_b));
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
bool isLookupMatch(const std::string& prefix_a, const std::string& tbl_b, const std::vector<std::string>& matched_tables) {
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(tbl_b));
    if (LOOKUP_TABLE_SYNONYMS.count(clean_tbl)) {
        for (const auto& tbl : matched_tables) {
            if (tbl != tbl_b) {
                std::string clean_tbl_other = stripTablePrefix(stripSchemaPrefix(tbl));
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

bool isLookupTable(const std::string& tbl) {
    std::string clean_tbl = stripTablePrefix(stripSchemaPrefix(tbl));
    return LOOKUP_TABLE_SYNONYMS.count(clean_tbl) > 0;
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
    std::string clean_tbl = to_lower(stripTablePrefix(stripSchemaPrefix(tbl_b)));
    
    auto getLastWord = [](const std::string& s) -> std::string {
        size_t last_under = s.find_last_of('_');
        if (last_under != std::string::npos) {
            return s.substr(last_under + 1);
        }
        return s;
    };
    
    std::string last_tbl_word = getLastWord(clean_tbl);
    std::string last_prefix_word = getLastWord(to_lower(prefix_a));
    
    if (last_tbl_word != last_prefix_word) return false;
    
    static const std::unordered_set<std::string> GENERIC_LAST_WORDS = {
        "type", "types", "status", "statuses", "group", "groups", "role", "roles",
        "state", "states", "level", "levels", "priority", "priorities", "code", "codes",
        "mode", "modes", "action", "actions", "tag", "tags", "value", "values",
        "info", "information", "config", "configs", "setting", "settings", "option", "options",
        "preference", "preferences", "log", "logs", "history", "detail", "details",
        "execution", "executions", "event", "events", "record", "records", "data", "file", "files",
        "token", "tokens", "category", "categories", "class", "classes", "genre", "genres"
    };
    if (clean_tbl != last_tbl_word) {
        if (GENERIC_LAST_WORDS.count(last_tbl_word) > 0) return false;
    }
    
    return !last_tbl_word.empty() && last_tbl_word.length() >= 3;
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
            auto getLastWord = [](const std::string& s) -> std::string {
                size_t last_under = s.find_last_of('_');
                if (last_under != std::string::npos) {
                    return s.substr(last_under + 1);
                }
                return s;
            };
            std::string last_a = getLastWord(to_lower(prefix_a));
            std::string last_b = getLastWord(to_lower(prefix_b));
            if (!last_a.empty() && !last_b.empty()) {
                if (GENERIC_PK_FK_PREFIXES.count(last_a) && GENERIC_PK_FK_PREFIXES.count(last_b)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool isGenericPkFkPrefix(const std::string& s) {
    return GENERIC_PK_FK_PREFIXES.count(to_lower(s)) > 0;
}

void filterDomainSpecificRelationships(
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    std::set<Relationship>& relationships) {

    // 1. Detect if this is a BPMN schema (Activiti/Flowable/Camunda)
    bool is_bpmn = false;
    int act_table_count = 0;
    for (const auto& tbl : table_names) {
        std::string tbl_lower = to_lower(tbl);
        if (tbl_lower.rfind("act_", 0) == 0) {
            act_table_count++;
        }
    }
    if (act_table_count >= 3) {
        is_bpmn = true;
    }

    // 2. Detect if this is an openBIS schema
    bool is_openbis = false;
    bool has_persons = false;
    bool has_samples = false;
    for (const auto& tbl : table_names) {
        std::string tbl_lower = to_lower(tbl);
        if (tbl_lower == "persons" || tbl_lower == "persons_all") {
            has_persons = true;
        }
        if (tbl_lower == "samples_all" || tbl_lower == "samples" || tbl_lower == "data_all") {
            has_samples = true;
        }
    }
    if (has_persons && has_samples) {
        is_openbis = true;
    }

    for (auto it = relationships.begin(); it != relationships.end(); ) {
        if (it->is_explicit) {
            ++it;
            continue;
        }
        bool to_remove = false;
        std::string from_tbl = to_lower(it->from_table);
        std::string to_tbl = to_lower(it->to_table);
        std::string col_a = to_lower(it->from_column);

        if (is_bpmn) {
            // Rule 1: History tables act_hi_* completely decoupled (never declare foreign keys in DDL)
            bool from_is_hi = (from_tbl.rfind("act_hi_", 0) == 0 || from_tbl.find("_hi_") != std::string::npos);
            bool to_is_hi = (to_tbl.rfind("act_hi_", 0) == 0 || to_tbl.find("_hi_") != std::string::npos);
            if (from_is_hi || to_is_hi) {
                to_remove = true;
            }

            // Rule 2: Standalone event logging table
            if (from_tbl == "act_evt_log" || from_tbl.rfind("act_evt_log_", 0) == 0 ||
                to_tbl == "act_evt_log" || to_tbl.rfind("act_evt_log_", 0) == 0) {
                to_remove = true;
            }

            // Rule 3: root_proc_inst_id_ is logical and unconstrained
            if (col_a == "root_proc_inst_id_" || col_a == "root_proc_inst_id") {
                to_remove = true;
            }

            // Rule 4: meta_info_ contains JSON strings
            if (col_a == "meta_info_" || col_a == "meta_info") {
                to_remove = true;
            }
            
            // Rule 5: variables, identitylinks, and integration task_id_ / proc_inst_id_ / flow_node_id_ unconstrained runtime refs
            if (from_tbl == "act_ru_variable" && (col_a == "task_id_" || col_a == "task_id")) {
                to_remove = true;
            }
            if (from_tbl == "act_ru_identitylink" && (col_a == "proc_inst_id_" || col_a == "proc_inst_id")) {
                to_remove = true;
            }
            if (from_tbl == "act_ru_integration" && (col_a == "flow_node_id_" || col_a == "flow_node_id")) {
                to_remove = true;
            }

            // Rule 6: parent_task_id_ / parent_task_id is logical and unconstrained
            if (col_a == "parent_task_id_" || col_a == "parent_task_id") {
                to_remove = true;
            }

            // Rule 7: deployment_id_ / deployment_id is unconstrained
            if (col_a == "deployment_id_" || col_a == "deployment_id") {
                to_remove = true;
            }

            // Rule 8: proc_def_id_ / proc_def_id is unconstrained
            if (col_a == "proc_def_id_" || col_a == "proc_def_id") {
                to_remove = true;
            }

            // Rule 9: execution_id_ / execution_id is unconstrained
            if (col_a == "execution_id_" || col_a == "execution_id") {
                to_remove = true;
            }
        }

        if (is_openbis) {
            // Rule 1: pers_id_registerer, pers_id_author, pers_id_modifier columns pointing to persons table
            if ((to_tbl == "persons" || to_tbl == "persons_all") && 
                (col_a.find("pers_id") != std::string::npos || col_a.find("pers_") == 0)) {
                to_remove = true;
            }
        }

        // AWS Route53 / DNS specific filters
        if (to_tbl == "zones" || to_tbl == "zone") {
            if (col_a == "time_zone_id" || col_a == "timezone_id" || col_a == "timezone" || col_a == "time_zone") {
                to_remove = true;
            }
            if (col_a == "zone_table" || col_a == "zone_tbl" || col_a == "zone_name" || col_a == "zonename") {
                to_remove = true;
            }
        }

        if (to_remove) {
            it = relationships.erase(it);
        } else {
            ++it;
        }
    }
}
