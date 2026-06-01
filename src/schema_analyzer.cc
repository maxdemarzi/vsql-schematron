#include "schema_analyzer.h"
#include "domain_specific_matching.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>

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
    auto check = [](const std::string& s) -> std::string {
        if (s == "dept") return "department";
        if (s == "cust") return "customer";
        if (s == "emp") return "employee";
        if (s == "mgr") return "manager";
        if (s == "org") return "organization";
        if (s == "src") return "source";
        if (s == "dest") return "destination";
        if (s == "addr") return "address";
        if (s == "desc") return "description";
        if (s == "prod") return "product";
        if (s == "cat") return "category";
        if (s == "msg") return "message";
        if (s == "pos") return "position";
        if (s == "usr") return "user";
        if (s == "grp") return "group";
        if (s == "auth") return "authority";
        if (s == "info") return "information";
        if (s == "spec") return "specification";
        if (s == "dev") return "development";
        if (s == "pkg") return "package";
        if (s == "txn") return "transaction";
        if (s == "tx") return "transaction";
        if (s == "doc") return "document";
        if (s == "ref") return "reference";
        if (s == "rel") return "relationship";
        if (s == "std") return "student";
        if (s == "sch") return "school";
        if (s == "loc") return "location";
        if (s == "lang") return "language";
        if (s == "qty") return "quantity";
        if (s == "amt") return "amount";
        if (s == "val") return "value";
        if (s == "num") return "number";
        if (s == "no") return "number";
        if (s == "pct") return "percent";
        if (s == "dt") return "date";
        if (s == "cd") return "code";
        if (s == "cl") return "client";
        if (s == "cli") return "client";
        if (s == "srv") return "server";
        if (s == "app") return "application";
        if (s == "cfg") return "configuration";
        if (s == "ctx") return "context";
        if (s == "db") return "database";
        if (s == "dir") return "directory";
        if (s == "env") return "environment";
        if (s == "err") return "error";
        if (s == "fun") return "function";
        if (s == "gen") return "generator";
        if (s == "hdr") return "header";
        if (s == "impl") return "implementation";
        if (s == "lib") return "library";
        if (s == "log") return "logical";
        if (s == "max") return "maximum";
        if (s == "min") return "minimum";
        if (s == "opt") return "option";
        if (s == "param") return "parameter";
        if (s == "prop") return "property";
        if (s == "req") return "request";
        if (s == "res") return "resource";
        if (s == "resp") return "response";
        if (s == "stat") return "status";
        if (s == "sys") return "system";
        if (s == "tmp") return "temporary";
        if (s == "temp") return "temporary";
        if (s == "util") return "utility";
        if (s == "ver") return "version";
        if (s == "vol") return "volume";
        if (s == "yr") return "year";
        if (s == "mth") return "month";
        if (s == "wk") return "week";
        if (s == "hr") return "hour";
        if (s == "sec") return "second";
        if (s == "ms") return "millisecond";
        if (s == "int") return "intersection";
        if (s == "nm") return "name";
        if (s == "st") return "status";
        if (s == "nb") return "number";
        if (s == "flg") return "flag";
        if (s == "fk") return "key";
        if (s == "pk") return "key";
        if (s == "crsreq") return "course_request";
        if (s == "corr") return "correlation";
        if (s == "corrset") return "correlation_set";
        return "";
    };
    std::string exp = check(w);
    if (!exp.empty()) return exp;
    if (w.length() > 1 && w.back() == 's') {
        std::string exp_sing = check(w.substr(0, w.length() - 1));
        if (!exp_sing.empty()) return exp_sing + "s";
    }
    if (w.length() > 2 && w.substr(w.length() - 2) == "es") {
        std::string exp_sing = check(w.substr(0, w.length() - 2));
        if (!exp_sing.empty()) return exp_sing + "es";
    }
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
        "main", "sub", "old", "new", "parent", "child", "prev", "next",
        "local", "external", "global", "remote", "internal", "mapped", "referred",
        "target", "source", "src", "dest", "original", "orig", "current", "curr",
        "default", "def", "temp", "temporary", "master", "detail", "base", "derived"
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& role : roles) {
            if (n.rfind(role + "_", 0) == 0) {
                n = n.substr(role.length() + 1);
                changed = true;
                break;
            }
        }
    }
    return n;
}

bool matchCleanTableNames(const std::string& p, const std::string& t, bool allow_substring) {
    std::string pl = to_lower(p);
    std::string tl = to_lower(t);
    if (pl == tl) return true;
    if (tl == pl + "s" || tl == pl + "es") return true;
    if (pl.length() > 1 && pl.back() == 'y') {
        std::string ies = pl.substr(0, pl.length() - 1) + "ies";
        if (tl == ies) return true;
    }
    if (pl == tl + "s" || pl == tl + "es") return true;
    if (tl.length() > 1 && tl.back() == 'y') {
        std::string ies = tl.substr(0, tl.length() - 1) + "ies";
        if (pl == ies) return true;
    }
    if (tl.rfind(pl + "_", 0) == 0 || pl.rfind(tl + "_", 0) == 0) return true;
    
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
    if (et.rfind(ep + "_", 0) == 0 || ep.rfind(et + "_", 0) == 0) return true;
    
    std::string p_clean = ep;
    std::string t_clean = et;
    p_clean.erase(std::remove(p_clean.begin(), p_clean.end(), '_'), p_clean.end());
    t_clean.erase(std::remove(t_clean.begin(), t_clean.end(), '_'), t_clean.end());
    if (p_clean == t_clean) return true;
    if (t_clean == p_clean + "s" || t_clean == p_clean + "es") return true;
    if (p_clean == t_clean + "s" || p_clean == t_clean + "es") return true;
    
    if (allow_substring && p_clean.length() >= 3) {
        if (t_clean.length() >= p_clean.length()) {
            if (t_clean.find(p_clean) == 0) return true;
            size_t pos = t_clean.rfind(p_clean);
            if (pos != std::string::npos && pos == t_clean.length() - p_clean.length()) return true;
            
            size_t pos_s = t_clean.rfind(p_clean + "s");
            if (pos_s != std::string::npos && pos_s == t_clean.length() - p_clean.length() - 1) return true;
            
            size_t pos_es = t_clean.rfind(p_clean + "es");
            if (pos_es != std::string::npos && pos_es == t_clean.length() - p_clean.length() - 2) return true;
        }
        if (p_clean.length() >= t_clean.length()) {
            if (p_clean.find(t_clean) == 0) return true;
            size_t pos = p_clean.rfind(t_clean);
            if (pos != std::string::npos && pos == p_clean.length() - t_clean.length()) return true;
            
            size_t pos_s = p_clean.rfind(t_clean + "s");
            if (pos_s != std::string::npos && pos_s == p_clean.length() - t_clean.length() - 1) return true;
            
            size_t pos_es = p_clean.rfind(t_clean + "es");
            if (pos_es != std::string::npos && pos_es == p_clean.length() - t_clean.length() - 2) return true;
        }
    }
    return false;
}

bool matchTableName(const std::string& col_prefix, const std::string& tbl_name, bool allow_substring = true) {
    std::string prefix = to_lower(col_prefix);
    std::string tbl = stripSchemaPrefix(to_lower(tbl_name));
    
    std::string clean_tbl = stripTablePrefix(tbl);
    std::string clean_prefix = stripTablePrefix(prefix);
    
    std::string prefix_norole = stripRolePrefix(prefix);
    std::string clean_prefix_norole = stripRolePrefix(clean_prefix);
    
    if (matchCleanTableNames(prefix, tbl, allow_substring)) return true;
    if (matchCleanTableNames(clean_prefix, clean_tbl, allow_substring)) return true;
    if (matchCleanTableNames(prefix, clean_tbl, allow_substring)) return true;
    if (matchCleanTableNames(clean_prefix, tbl, allow_substring)) return true;
    
    if (matchCleanTableNames(prefix_norole, tbl, allow_substring)) return true;
    if (matchCleanTableNames(clean_prefix_norole, clean_tbl, allow_substring)) return true;
    
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
    // Fallback for lowercase without underscore ending in identifier suffixes
    std::string c_lower = to_lower(col);
    std::vector<std::string> suffixes = {"uuid", "guid", "uid", "id"};
    for (const auto& sfx : suffixes) {
        if (c_lower.length() > sfx.length() && c_lower.rfind(sfx) == c_lower.length() - sfx.length()) {
            prefix = col.substr(0, col.length() - sfx.length());
            suffix = col.substr(col.length() - sfx.length());
            return true;
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

bool matchMiddleIdConvention(const std::string& col, const std::string& tbl_b, bool allow_substring = true) {
    std::string c = to_lower(col);
    size_t pos = c.find("_id_");
    if (pos != std::string::npos && pos > 0) {
        std::string entity = c.substr(0, pos);
        if (matchTableName(entity, tbl_b, allow_substring)) return true;
    }
    if (c.rfind("id_", 0) == 0) {
        std::string entity = c.substr(3);
        if (matchTableName(entity, tbl_b, allow_substring)) return true;
    }
    if (c.rfind("id", 0) == 0 && col.length() > 2 && std::isupper(col[2])) {
        std::string entity = c.substr(2);
        if (matchTableName(entity, tbl_b, allow_substring)) return true;
    }
    return false;
}

bool isSubtypeTable(const std::string& tbl_a, const std::string& tbl_b) {
    std::string a = stripSchemaPrefix(to_lower(tbl_a));
    std::string b = stripSchemaPrefix(to_lower(tbl_b));
    std::string clean_a = stripTablePrefix(a);
    std::string clean_b = stripTablePrefix(b);
    if (clean_a == clean_b) return true;
    if (clean_a.length() > clean_b.length()) {
        size_t pos = clean_a.rfind(clean_b);
        if (pos != std::string::npos && pos == clean_a.length() - clean_b.length()) return true;
        
        // Prefix-based hierarchy (e.g. step_video -> steps)
        std::string sb = clean_b;
        if (sb.length() > 1 && sb.back() == 's') sb = sb.substr(0, sb.length() - 1);
        if (clean_a.rfind(sb + "_", 0) == 0) {
            return true;
        }
    }
    return false;
}

std::string getTableAcronym(const std::string& tbl) {
    std::string name = stripTablePrefix(stripSchemaPrefix(to_lower(tbl)));
    std::vector<std::string> words;
    std::string word;
    std::istringstream tokenStream(name);
    while (std::getline(tokenStream, word, '_')) {
        if (!word.empty()) words.push_back(word);
    }
    if (words.size() >= 2) {
        std::string acronym;
        for (const auto& w : words) {
            acronym += w[0];
        }
        return acronym;
    }
    return "";
}

std::string stripAcronymPrefix(const std::string& col, const std::string& tbl) {
    std::string acronym = getTableAcronym(tbl);
    if (acronym.length() >= 2) {
        std::string col_lower = to_lower(col);
        if (col_lower.rfind(acronym + "_", 0) == 0) {
            return col.substr(acronym.length() + 1);
        }
    }
    return col;
}

bool isGenericIdentifier(const std::string& s) {
    std::string l = to_lower(s);
    return l == "id" || l == "uuid" || l == "guid" || l == "uid";
}

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
    "sender", "receiver", "recipient", "profile", "investigator", "invitee", "inviter"
};

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

const std::unordered_set<std::string> LOOKUP_TABLE_SYNONYMS = {
    "lookup", "lookups", "code_lookup", "reference", "references", "dictionary", 
    "dictionaries", "codelist", "enum_value", "enum_values", "lookup_value", "lookup_values"
};

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

const std::unordered_set<std::string> GENERIC_PK_FK_PREFIXES = {
    "id", "key", "pk", "fk", "ref", "cod", "code", "cd", "no", "num", "nro", "nra", "nr", "number"
};

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

    // Pass 1: Find all non-subtype relationships
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
            
            // Check if col_a is one of the effective PKs of tbl_a
            bool col_a_is_pk = false;
            for (const auto& pk_a : pks_a) {
                if (to_lower(col_a_clean) == to_lower(pk_a)) {
                    col_a_is_pk = true;
                    break;
                }
            }
            
            // Phase 4: Domain-Specific Keys Matching for Amazon Vendor Central and similar datasets
            matchDomainSpecificKeys(tbl_a, col_a, type_a, table_names, tables_info, relationships);

            for (const auto& tbl_b : table_names) {
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
                        if (it_b_col == info_b.column_types.end() || !typeMatches(type_a, it_b_col->second)) {
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
                            if (PERSON_TABLE_SYNONYMS.count(clean_b)) {
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
                                if (matchTableName(prefix_a, tbl_b, !prefix_has_exact) || isPersonMatch(prefix_a, tbl_b) || isLookupMatch(prefix_a, tbl_b, table_names)) {
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
                                if (matchTableName(suffix_a, tbl_b, !suffix_has_exact)) {
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
                                is_subtype = true;
                            } else {
                                std::string clean_a = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_a)));
                                std::string clean_b = stripTablePrefix(stripSchemaPrefix(to_lower(tbl_b)));
                                if (PERSON_TABLE_SYNONYMS.count(clean_a) && PERSON_TABLE_SYNONYMS.count(clean_b)) {
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
