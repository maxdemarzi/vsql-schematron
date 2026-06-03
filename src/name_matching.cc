#include "name_matching.h"
#include "string_helpers.h"
#include <algorithm>
#include <cctype>
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
