#include "string_helpers.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>

/**
 * Converts a string to lowercase.
 */
std::string to_lower(std::string s) {
    for (char &c : s) c = std::tolower(c);
    return s;
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
 * Returns true if the given string is a common technical abbreviation of 2 chars.
 */
bool isKnown2CharAbbreviation(const std::string& s) {
    static const std::unordered_set<std::string> ABBR = {
        "tx", "dt", "cd", "cl", "db", "yr", "wk", "hr", "ms", "nm", "st", "nb", "fk", "pk", "no"
    };
    return ABBR.count(s);
}
