#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <algorithm>

std::string to_lower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}

std::string stripSchemaPrefix(const std::string& name) {
    size_t dot = name.find('.');
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
        static const std::unordered_set<std::string> TECHNICAL_PREFIXES = {
            "idn", "oauth", "oauth2", "oauth1a", "oidc", "comtn",
            "vsql", "sys", "db", "tbl", "ref", "cuds", "ext", "act", "qrtz"
        };
        if (TECHNICAL_PREFIXES.count(prefix) > 0 || prefix.length() <= 2) {
            n = n.substr(underscore + 1);
            underscore = n.find('_');
        } else {
            break;
        }
    }
    return n;
}

std::string stripTableSuffix(const std::string& name) {
    std::string n = to_lower(name);
    size_t underscore = n.find_last_of('_');
    while (underscore != std::string::npos && underscore > 0 && underscore < n.length() - 1) {
        std::string suffix = n.substr(underscore + 1);
        static const std::unordered_set<std::string> TECHNICAL_SUFFIXES = {
            "all", "ext", "base", "v", "b", "t", "all_v",
            "tbl", "table", "tab", "pk", "fk", "ptr"
        };
        if (TECHNICAL_SUFFIXES.count(suffix) > 0) {
            n = n.substr(0, underscore);
            underscore = n.find_last_of('_');
        } else {
            break;
        }
    }
    return n;
}

bool isJunctionOrHistoryTable(const std::string& tbl_name) {
    std::string clean = stripTablePrefix(stripTableSuffix(stripSchemaPrefix(to_lower(tbl_name))));
    static const std::unordered_set<std::string> JUNCTION_SUFFIXES = {
        "map", "maps", "link", "links", "relation", "relations",
        "relationship", "relationships", "membership", "memberships",
        "association", "associations", "history", "histories", "queue", "queues",
        "log", "logs", "entry", "entries", "audit", "audits"
    };
    if (JUNCTION_SUFFIXES.count(clean) > 0) return true;
    size_t underscore = clean.rfind('_');
    if (underscore != std::string::npos && underscore < clean.length() - 1) {
        std::string suffix = clean.substr(underscore + 1);
        if (JUNCTION_SUFFIXES.count(suffix) > 0) return true;
    }
    return false;
}

int main() {
    std::cout << "isJunctionOrHistoryTable: " << (isJunctionOrHistoryTable("building_location_status") ? "true" : "false") << std::endl;
    return 0;
}
