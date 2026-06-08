#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "name_matching.h"
#include "string_helpers.h"

bool matchCleanTableNamesTrace(const std::string& p, const std::string& t, bool allow_substring) {
    std::string pl = to_lower(p);
    std::string tl = to_lower(t);
    std::cout << "[TRACE] p='" << p << "', t='" << t << "'" << std::endl;
    if (pl == tl) { std::cout << "  match: pl == tl" << std::endl; return true; }
    if (tl == pl + "s" || tl == pl + "es") { std::cout << "  match: tl == pl + s/es" << std::endl; return true; }
    
    if (pl.length() == 1 && tl.length() > 1) {
        bool all_same = true;
        for (char c : tl) {
            if (c != pl[0]) { all_same = false; break; }
        }
        if (all_same) return true;
    }
    if (tl.length() == 1 && pl.length() > 1) {
        bool all_same = true;
        for (char c : pl) {
            if (c != tl[0]) { all_same = false; break; }
        }
        if (all_same) return true;
    }
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
        if (tl.rfind(pl + "_", 0) == 0) {
            size_t last_under = tl.rfind('_');
            std::string suffix = (last_under != std::string::npos) ? tl.substr(last_under + 1) : "";
            static const std::unordered_set<std::string> AUXILIARY_SUFFIXES = {
                "token", "tokens", "request", "requests", "key", "keys", "code", "codes", "secret", "secrets",
                "hash", "hashes", "password", "passwords", "ticket", "tickets", "meta", "metadata",
                "config", "configs", "setting", "settings", "option", "options", "preference", "preferences",
                "session", "sessions", "backup", "backups", "file", "files", "log", "logs", "history", "histories",
                "detail", "details", "item", "items", "line", "lines", "event", "events", "record", "records",
                "profile", "profiles", "role", "roles", "group", "groups", "permission", "permissions", "privilege", "privileges",
                "status", "statuses", "type", "types", "category", "categories", "tag", "tags", "link", "links",
                "map", "maps", "relation", "relations", "relationship", "relationships", "association", "associations",
                "membership", "memberships", "address", "addresses", "contact", "contacts", "attachment", "attachments",
                "comment", "comments", "message", "messages", "notification", "notifications"
            };
            if (AUXILIARY_SUFFIXES.count(suffix) > 0) {
                size_t pl_last_under = pl.rfind('_');
                std::string pl_suffix = (pl_last_under != std::string::npos) ? pl.substr(pl_last_under + 1) : pl;
                if (pl_suffix == suffix || singularize(pl_suffix) == singularize(suffix)) {
                    std::cout << "  match: auxiliary suffix" << std::endl;
                    return true;
                }
            } else {
                std::cout << "  match: tl starts with pl + _" << std::endl;
                return true;
            }
        }
        if (pl.rfind(tl + "_", 0) == 0) {
            std::cout << "  match: pl starts with tl + _" << std::endl;
            return true;
        }
        if (tl.length() > pl.length() + 1 && tl.rfind("_" + pl) == tl.length() - pl.length() - 1) {
            std::cout << "  match: tl ends with _ + pl" << std::endl;
            return true;
        }
        if (pl.length() > tl.length() + 1 && pl.rfind("_" + tl) == pl.length() - tl.length() - 1) {
            std::cout << "  match: pl ends with _ + tl" << std::endl;
            return true;
        }
        
        size_t underscore = tl.find('_');
        std::string first_word = (underscore == std::string::npos) ? tl : tl.substr(0, underscore);
        if (first_word == pl || singularize(first_word) == singularize(pl) || first_word == pl + "s" || first_word + "s" == pl) {
            if (tl.length() > first_word.length()) {
                std::string remaining = tl.substr(first_word.length() + 1);
                size_t next_under = remaining.find('_');
                std::string rem_word = (next_under == std::string::npos) ? remaining : remaining.substr(0, next_under);
                if (isGenericTableSuffix(rem_word)) {
                    std::cout << "  match: generic table suffix" << std::endl;
                    return true;
                }
            }
        } else if (first_word.length() > pl.length() && (pl.length() >= 3 || (pl.length() == 2 && isKnown2CharAbbreviation(pl))) && first_word.rfind(pl, 0) == 0) {
            std::cout << "  match: first word starts with pl" << std::endl;
            return true;
        }
    }
    
    std::string ep = expandAllAbbreviations(p);
    std::string et = expandAllAbbreviations(t);
    if ((ep.rfind("database", 0) == 0 && et == "data") || (et.rfind("database", 0) == 0 && ep == "data")) {
        return false;
    }
    if (ep == et) { std::cout << "  match: ep == et" << std::endl; return true; }
    if (et == ep + "s" || et == ep + "es") { std::cout << "  match: et == ep + s/es" << std::endl; return true; }
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
        std::string ep_sing = ep;
        std::string et_sing = et;
        size_t under_p = ep.rfind('_');
        if (under_p != std::string::npos) {
            ep_sing = ep.substr(0, under_p + 1) + singularize(ep.substr(under_p + 1));
        } else {
            ep_sing = singularize(ep);
        }
        size_t under_t = et.rfind('_');
        if (under_t != std::string::npos) {
            et_sing = et.substr(0, under_t + 1) + singularize(et.substr(under_t + 1));
        } else {
            et_sing = singularize(et);
        }
        if (et_sing.rfind(ep_sing + "_", 0) == 0 || ep_sing.rfind(et_sing + "_", 0) == 0) {
            std::cout << "  match: et_sing/ep_sing prefix (ep_sing=" << ep_sing << ", et_sing=" << et_sing << ")" << std::endl;
            return true;
        }
        
        size_t underscore = et.find('_');
        std::string first_word = (underscore == std::string::npos) ? et : et.substr(0, underscore);
        if (first_word == ep || singularize(first_word) == singularize(ep) || first_word == ep + "s" || first_word + "s" == ep) {
            if (et.length() > first_word.length()) {
                std::string remaining = et.substr(first_word.length() + 1);
                size_t next_under = remaining.find('_');
                std::string rem_word = (next_under == std::string::npos) ? remaining : remaining.substr(0, next_under);
                if (isGenericTableSuffix(rem_word)) {
                    std::cout << "  match: expanded generic table suffix" << std::endl;
                    return true;
                }
            }
        } else if (first_word.length() > ep.length() && (ep.length() >= 3 || (ep.length() == 2 && isKnown2CharAbbreviation(ep))) && first_word.rfind(ep, 0) == 0) {
            std::cout << "  match: expanded first word starts with ep" << std::endl;
            return true;
        }
    }
    
    return false;
}

int main() {
    std::string prefix = "schembl_chem";
    std::string tbl = "schembl_chemical";
    
    std::string norm_prefix = to_lower(prefix);
    std::string norm_tbl = stripSchemaPrefix(to_lower(tbl));
    
    std::string clean_tbl = stripTablePrefix(stripTableSuffix(norm_tbl));
    std::string clean_prefix = stripTablePrefix(stripTableSuffix(norm_prefix));
    
    std::string prefix_norole = stripRolePrefix(norm_prefix);
    std::string clean_prefix_norole = stripRolePrefix(clean_prefix);
    
    if (matchCleanTableNamesTrace(norm_prefix, norm_tbl, true)) return 0;
    if (matchCleanTableNamesTrace(clean_prefix, clean_tbl, true)) return 0;
    if (matchCleanTableNamesTrace(norm_prefix, clean_tbl, true)) return 0;
    if (matchCleanTableNamesTrace(clean_prefix, norm_tbl, true)) return 0;
    if (matchCleanTableNamesTrace(prefix_norole, norm_tbl, true)) return 0;
    if (matchCleanTableNamesTrace(clean_prefix_norole, clean_tbl, true)) return 0;
    
    return 0;
}
