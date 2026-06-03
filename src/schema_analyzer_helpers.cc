#include "schema_analyzer_helpers.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace {

thread_local std::string g_dynamic_prefix = "";

} // namespace

/**
 * Sets the dynamic table prefix used for filtering prefix-like parts of table names.
 */
void setDynamicPrefix(const std::string& prefix) {
    g_dynamic_prefix = prefix;
}

/**
 * Clears the dynamic table prefix.
 */
void clearDynamicPrefix() {
    g_dynamic_prefix = "";
}

/**
 * Converts a string to lowercase.
 */
std::string to_lower(std::string s) {
    for (char &c : s) c = std::tolower(c);
    return s;
}

/**
 * Strips any schema/database prefix from a table name.
 *
 * Example:
 *   stripSchemaPrefix("sales.orders") -> "orders"
 */
std::string stripSchemaPrefix(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        return name.substr(dot + 1);
    }
    return name;
}

/**
 * Strips common technical prefixes from a table name, including tbl_, ref_,
 * and the configured dynamic prefix.
 *
 * Examples:
 *   stripTablePrefix("tbl_orders") -> "orders"
 *   stripTablePrefix("ref_customer") -> "customer"
 */
std::string stripTablePrefix(const std::string& name) {
    std::string n = to_lower(name);
    if (!g_dynamic_prefix.empty()) {
        std::string dp = to_lower(g_dynamic_prefix) + "_";
        if (n.rfind(dp, 0) == 0) {
            n = n.substr(dp.length());
        }
    }
    if (n.rfind("tbl_", 0) == 0) n = n.substr(4);
    if (n.rfind("ref", 0) == 0) n = n.substr(3);
    
    size_t underscore = n.find('_');
    while (underscore != std::string::npos && underscore > 0) {
        std::string prefix = n.substr(0, underscore);
        if (prefix == "idn" || prefix == "oauth" || prefix == "oauth2" || 
            prefix == "oauth1a" || prefix == "oidc" || prefix == "comtn" || 
            prefix == "vsql" || prefix == "sys" || prefix == "db" || 
            prefix == "tbl" || prefix == "ref" || prefix == "cuds" || 
            prefix == "ext" || prefix.length() <= 2) {
            n = n.substr(underscore + 1);
            underscore = n.find('_');
        } else {
            break;
        }
    }
    return n;
}

/**
 * Expands technical abbreviations to their full English words.
 *
 * Examples:
 *   expandAbbreviation("dept") -> "department"
 *   expandAbbreviation("qty") -> "quantity"
 */
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

/**
 * Expands all technical abbreviations in a tokenized string.
 *
 * Example:
 *   expandAllAbbreviations("dept_mgr") -> "department_manager"
 */
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

/**
 * Strips common role prefixes (like first_, secondary_, parent_, from_) from column names.
 *
 * Example:
 *   stripRolePrefix("parent_company_id") -> "company_id"
 */
std::string stripRolePrefix(const std::string& name) {
    std::string n = to_lower(name);
    std::vector<std::string> roles = {
        "first", "second", "third", "fourth", "one", "two", "primary", "secondary",
        "main", "sub", "old", "new", "parent", "child", "prev", "next",
        "local", "external", "global", "remote", "internal", "mapped", "referred",
        "target", "source", "src", "dest", "original", "orig", "current", "curr",
        "default", "def", "temp", "temporary", "master", "detail", "base", "derived",
        "for", "from", "to"
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

/**
 * Plural-to-singular converter for matching table names.
 *
 * Examples:
 *   singularize("categories") -> "category"
 *   singularize("orders") -> "order"
 */
std::string singularize(const std::string& w) {
    if (w.length() > 3 && w.rfind("ies") == w.length() - 3) {
        return w.substr(0, w.length() - 3) + "y";
    }
    if (w.length() > 2 && w.rfind("es") == w.length() - 2) {
        if (w.length() > 4 && w.rfind("sses") == w.length() - 4) {
            return w.substr(0, w.length() - 2);
        }
        if (w.length() > 4 && w.rfind("uses") == w.length() - 4) {
            return w.substr(0, w.length() - 2);
        }
        if (w.length() > 3 && (w.rfind("che") == w.length() - 3 || w.rfind("she") == w.length() - 3)) {
            return w.substr(0, w.length() - 2);
        }
        if (w.length() > 2 && (w[w.length() - 3] == 'x' || w[w.length() - 3] == 'z')) {
            return w.substr(0, w.length() - 2);
        }
        return w.substr(0, w.length() - 1);
    }
    if (w.length() > 1 && w.back() == 's') {
        if (w.length() > 2 && (w.rfind("ss") == w.length() - 2 || w.rfind("us") == w.length() - 2 || w.rfind("is") == w.length() - 2 || w.rfind("as") == w.length() - 2)) {
            return w;
        }
        return w.substr(0, w.length() - 1);
    }
    return w;
}

/**
 * Returns true if the suffix is a generic table descriptor (such as metadata, log, ref).
 */
bool isGenericTableSuffix(const std::string& suffix) {
    static const std::unordered_set<std::string> SUFFIXES = {
        "session", "sessions", "profile", "profiles", "info", "infos", "detail", "details",
        "history", "histories", "log", "logs", "metadata", "meta", "ext", "extension", "extensions",
        "type", "types", "status", "statuses", "category", "categories", "genre", "genres",
        "state", "states", "level", "levels", "priority", "priorities", "lookup", "lookups",
        "code", "codes", "mode", "modes", "action", "actions", "tag", "tags",
        "list", "lists", "table", "tables", "tbl", "tbls", "data", "datas", "ref", "refs",
        "record", "records", "item", "items", "base", "bases", "catalog", "catalogs",
        "app", "apps", "cfg", "rel", "map", "link", "t"
    };
    return SUFFIXES.count(to_lower(suffix));
}

/**
 * Returns true if the given string is a common technical abbreviation of 2 chars.
 */
bool isKnown2CharAbbreviation(const std::string& s) {
    static const std::unordered_set<std::string> ABBR = {
        "tx", "dt", "cd", "cl", "db", "yr", "wk", "hr", "ms", "nm", "st", "nb", "fk", "pk", "no"
    };
    return ABBR.count(s);
}

/**
 * Compares two cleaned table names, handling pluralization, abbreviation expansion,
 * and optional substring matches.
 */
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
    if (allow_substring) {
        if (tl.rfind(pl + "_", 0) == 0 || pl.rfind(tl + "_", 0) == 0) return true;
        if (pl.rfind(tl + "_", 0) == 0 || (tl.length() > pl.length() && tl.rfind("_" + pl) == tl.length() - pl.length() - 1)) return true;
        
        size_t underscore = tl.find('_');
        std::string first_word = (underscore == std::string::npos) ? tl : tl.substr(0, underscore);
        if (first_word == pl) {
            if (tl.length() > pl.length()) {
                std::string remaining = tl.substr(pl.length() + 1);
                size_t next_under = remaining.find('_');
                std::string rem_word = (next_under == std::string::npos) ? remaining : remaining.substr(0, next_under);
                if (isGenericTableSuffix(rem_word)) return true;
            }
        } else if (first_word.length() > pl.length() && (pl.length() >= 3 || (pl.length() == 2 && isKnown2CharAbbreviation(pl))) && first_word.rfind(pl, 0) == 0) {
            return true;
        }
    }
    
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
    if (allow_substring) {
        if (et.rfind(ep + "_", 0) == 0 || ep.rfind(et + "_", 0) == 0) return true;
        if (ep.rfind(et + "_", 0) == 0 || (et.length() > ep.length() && et.rfind("_" + ep) == et.length() - ep.length() - 1)) return true;
        
        size_t underscore = et.find('_');
        std::string first_word = (underscore == std::string::npos) ? et : et.substr(0, underscore);
        if (first_word == ep) {
            if (et.length() > ep.length()) {
                std::string remaining = et.substr(ep.length() + 1);
                size_t next_under = remaining.find('_');
                std::string rem_word = (next_under == std::string::npos) ? remaining : remaining.substr(0, next_under);
                if (isGenericTableSuffix(rem_word)) return true;
            }
        } else if (first_word.length() > ep.length() && (ep.length() >= 3 || (ep.length() == 2 && isKnown2CharAbbreviation(ep))) && first_word.rfind(ep, 0) == 0) {
            return true;
        }
    }
    
    std::string p_clean = ep;
    std::string t_clean = et;
    p_clean.erase(std::remove(p_clean.begin(), p_clean.end(), '_'), p_clean.end());
    t_clean.erase(std::remove(t_clean.begin(), t_clean.end(), '_'), t_clean.end());
    if (p_clean == t_clean) return true;
    if (t_clean == p_clean + "s" || t_clean == p_clean + "es") return true;
    if (p_clean == t_clean + "s" || p_clean == t_clean + "es") return true;
    
    std::string p_sing = singularize(p_clean);
    std::string t_sing = singularize(t_clean);
    if (p_sing == t_sing) return true;
    
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
        
        if (p_sing.length() >= 3 && t_sing.length() >= 3) {
            if (t_sing.length() >= p_sing.length()) {
                size_t pos = t_sing.rfind(p_sing);
                if (pos != std::string::npos && pos == t_sing.length() - p_sing.length()) return true;
            }
            if (p_sing.length() >= t_sing.length()) {
                size_t pos = p_sing.rfind(t_sing);
                if (pos != std::string::npos && pos == p_sing.length() - t_sing.length()) return true;
            }
        }
    }
    return false;
}

/**
 * Multi-level comparison helper to check if a column's prefix matches a target table name.
 */
bool matchTableName(const std::string& col_prefix, const std::string& tbl_name, bool allow_substring) {
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
 * Tokenizes snake_case or camelCase column names into prefix and suffix parts.
 *
 * Examples:
 *   "customer_id" -> prefix: "customer", suffix: "id"
 *   "customerId" -> prefix: "customer", suffix: "Id"
 */
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
 * Checks for middle identifier conventions in column names.
 *
 * Example:
 *   "customer_id_seq" or "id_customer" -> returns true if target table B matches "customer".
 */
bool matchMiddleIdConvention(const std::string& col, const std::string& tbl_b, bool allow_substring) {
    std::string c = to_lower(col);
    size_t pos = c.find("_id_");
    if (pos != std::string::npos && pos > 0) {
        std::string entity = c.substr(0, pos);
        bool entity_allow_substring = allow_substring;
        if (std::islower(col[0])) {
            entity_allow_substring = false;
        }
        if (matchTableName(entity, tbl_b, entity_allow_substring)) return true;
    }
    if (c.rfind("id_", 0) == 0 && col.length() > 3) {
        std::string entity = c.substr(3);
        bool entity_allow_substring = allow_substring;
        if (std::islower(col[3])) {
            entity_allow_substring = false;
        }
        if (matchTableName(entity, tbl_b, entity_allow_substring)) return true;
    }
    if (c.rfind("id", 0) == 0 && col.length() > 2) {
        std::string entity = c.substr(2);
        bool entity_allow_substring = allow_substring;
        if (std::islower(col[2])) {
            entity_allow_substring = false;
        }
        if (matchTableName(entity, tbl_b, entity_allow_substring)) return true;
    }
    return false;
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

/**
 * Computes table name acronyms.
 *
 * Example:
 *   getTableAcronym("order_items") -> "oi"
 */
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

/**
 * Strips the initials of a table name if it prefixes a column name.
 *
 * Example:
 *   stripAcronymPrefix("oi_quantity", "order_items") -> "quantity"
 */
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

/**
 * Returns true if string matches standard identifier keywords.
 */
bool isGenericIdentifier(const std::string& s) {
    std::string l = to_lower(s);
    return l == "id" || l == "uuid" || l == "guid" || l == "uid";
}
