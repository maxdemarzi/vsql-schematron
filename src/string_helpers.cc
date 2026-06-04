#include "string_helpers.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

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
    static const std::unordered_map<std::string, std::string> ABBR_MAP = {
        {"dept", "department"},
        {"cust", "customer"},
        {"emp", "employee"},
        {"mgr", "manager"},
        {"org", "organization"},
        {"dbin", "database_instance"},
        {"expe", "experiment"},
        {"samp", "sample"},
        {"cvte", "controlled_vocabulary_term"},
        {"etpt", "experiment_type_property_type"},
        {"stpt", "sample_type_property_type"},
        {"dstpt", "data_set_type_property_type"},
        {"prty", "property_type"},
        {"mate", "material"},
        {"proj", "project"},
        {"ds", "data"},
        {"covo", "controlled_vocabulary"},
        {"dast", "data_store"},
        {"dsty", "data_set_type"},
        {"exac", "attachment_content"},
        {"del", "deletion"},
        {"pers", "person"},
        {"daty", "data_type"},
        {"maty", "material_type"},
        {"exty", "experiment_type"},
        {"saty", "sample_type"},
        {"ag", "authorization_group"},
        {"mepr", "metaproject"},
        {"loty", "locator_type"},
        {"ffty", "file_format_type"},
        {"src", "source"},
        {"dest", "destination"},
        {"addr", "address"},
        {"desc", "description"},
        {"prod", "product"},
        {"cat", "category"},
        {"msg", "message"},
        {"pos", "position"},
        {"usr", "user"},
        {"grp", "group"},
        {"auth", "authority"},
        {"info", "information"},
        {"spec", "specification"},
        {"dev", "development"},
        {"pkg", "package"},
        {"txn", "transaction"},
        {"tx", "transaction"},
        {"doc", "document"},
        {"ref", "reference"},
        {"rel", "relationship"},
        {"std", "student"},
        {"sch", "school"},
        {"loc", "location"},
        {"lang", "language"},
        {"qty", "quantity"},
        {"amt", "amount"},
        {"val", "value"},
        {"num", "number"},
        {"no", "number"},
        {"pct", "percent"},
        {"dt", "date"},
        {"cd", "code"},
        {"cl", "client"},
        {"cli", "client"},
        {"srv", "server"},
        {"app", "application"},
        {"cfg", "configuration"},
        {"ctx", "context"},
        {"db", "database"},
        {"dir", "directory"},
        {"env", "environment"},
        {"err", "error"},
        {"fun", "function"},
        {"gen", "generator"},
        {"hdr", "header"},
        {"impl", "implementation"},
        {"lib", "library"},
        {"log", "logical"},
        {"max", "maximum"},
        {"min", "minimum"},
        {"opt", "option"},
        {"param", "parameter"},
        {"prop", "property"},
        {"req", "request"},
        {"res", "resource"},
        {"resp", "response"},
        {"stat", "status"},
        {"sys", "system"},
        {"tmp", "temporary"},
        {"temp", "temporary"},
        {"util", "utility"},
        {"ver", "version"},
        {"vol", "volume"},
        {"yr", "year"},
        {"mth", "month"},
        {"wk", "week"},
        {"hr", "hour"},
        {"sec", "second"},
        {"ms", "millisecond"},
        {"int", "intersection"},
        {"nm", "name"},
        {"st", "status"},
        {"nb", "number"},
        {"flg", "flag"},
        {"fk", "key"},
        {"pk", "key"},
        {"crsreq", "course_request"},
        {"corr", "correlation"},
        {"corrset", "correlation_set"}
    };

    auto check = [&](const std::string& s) -> std::string {
        auto it = ABBR_MAP.find(s);
        if (it != ABBR_MAP.end()) {
            return it->second;
        }
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
