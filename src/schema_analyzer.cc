#include "schema_analyzer.h"
#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include "implied_relationships.h"
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace {

/**
 * Retrieves all base table names in the specified database.
 *
 * Example:
 *   Given db_name = "sales", it executes:
 *     SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = 'sales' AND TABLE_TYPE = 'BASE TABLE'
 *   Returns a vector of table names like {"orders", "customers"}.
 */
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

/**
 * Retrieves structural metadata for all tables in the database, including column names,
 * data types, and primary key status.
 *
 * Example:
 *   Given db_name = "sales", it constructs a map mapping table name to TableInfo structs:
 *     "orders" -> { name: "orders", pk_columns: {"id"}, column_types: {"id": "int", "customer_id": "int"} }
 */
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
            } else if (key == "UNI") {
                info.uni_columns.push_back(col);
            }
        }
    }
    return info_map;
}

/**
 * Fetches explicit foreign key relationships defined as constraints in the database.
 * Also populates a set of columns that are explicitly mapped to avoid guessing them later.
 *
 * Example:
 *   If orders.customer_id is a foreign key constraint referencing customers.id:
 *     Returns Relationship { from_table: "orders", from_column: "customer_id", to_table: "customers", to_column: "id", is_explicit: true }
 *     and adds {"orders", "customer_id"} to explicit_mapped_cols.
 */
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

} // namespace

/**
 * Checks if two SQL types are compatible for joining (e.g., both numeric or both string).
 *
 * Examples:
 *   typeMatches("INT", "BIGINT") -> true
 *   typeMatches("VARCHAR(255)", "TEXT") -> true
 *   typeMatches("INT", "VARCHAR") -> false
 */
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
    auto isFloat = [](const std::string& t) {
        return t.find("double") != std::string::npos ||
               t.find("float") != std::string::npos ||
               t.find("real") != std::string::npos;
    };
    
    if (isNumeric(s1) && isNumeric(s2)) {
        if (isFloat(s1) != isFloat(s2)) return false;
        return true;
    }
    auto isString = [](const std::string& t) {
        return t.find("char") != std::string::npos ||
               t.find("text") != std::string::npos ||
               t.find("string") != std::string::npos ||
               t.find("uuid") != std::string::npos;
    };
    
    if (isString(s1) && isString(s2)) return true;
    
    return false;
}

/**
 * Returns true if the database is a standard database engine catalog or system database.
 *
 * Examples:
 *   isSystemDatabase("mysql") -> true
 *   isSystemDatabase("information_schema") -> true
 *   isSystemDatabase("my_app_db") -> false
 */
bool isSystemDatabase(const std::string& db) {
    std::string name = to_lower(db);
    static const std::unordered_set<std::string> SYSTEM_DBS = {
        "information_schema", "performance_schema", "mysql", "sys"
    };
    return SYSTEM_DBS.count(name) > 0;
}

/**
 * Validates if two column types are compatible, with additional semantic checks.
 * For example, it prevents joining non-key columns of mismatching boolean representations
 * (e.g., a tinyint(1) boolean vs a regular tinyint counter).
 *
 * Examples:
 *   typesAreSemanticallyCompatible("status_id", "TINYINT", "TINYINT") -> true
 *   typesAreSemanticallyCompatible("is_active", "TINYINT(1)", "TINYINT") -> false (since is_active is boolean-like, but right side is not)
 */
bool typesAreSemanticallyCompatible(const std::string& col_a, const std::string& type_a, const std::string& type_b) {
    if (!typeMatches(type_a, type_b)) return false;
    
    std::string ta = to_lower(type_a);
    std::string tb = to_lower(type_b);
    std::string col = to_lower(col_a);
    
    bool a_is_bool = (ta.find("tinyint") != std::string::npos || ta.find("bool") != std::string::npos);
    bool b_is_bool = (tb.find("tinyint") != std::string::npos || tb.find("bool") != std::string::npos);
    
    if (a_is_bool != b_is_bool) {
        bool col_is_key = (col == "id" || col == "uid" || col == "uuid" || col == "guid" || col == "key");
        if (!col_is_key) {
            if (col.length() > 3 && (col.rfind("_id") == col.length() - 3 || col.rfind("_cd") == col.length() - 3)) {
                col_is_key = true;
            } else if (col.length() > 4 && (col.rfind("_key") == col.length() - 4 || col.rfind("_uid") == col.length() - 4)) {
                col_is_key = true;
            } else if (col.length() > 5) {
                std::vector<std::string> suffixes = {"_uuid", "_guid", "_code"};
                for (const auto& sfx : suffixes) {
                    if (col.rfind(sfx) == col.length() - sfx.length()) {
                        col_is_key = true;
                        break;
                    }
                }
            }
        }
        if (!col_is_key) {
            return false;
        }
    }
    return true;
}

/**
 * The main orchestrator for schema relationship validation.
 * Queries metadata from the database, runs the heuristic/implied relationship passes,
 * formats the found connections, and returns the result as a text report.
 *
 * Example return:
 *   Database: test_db
 *
 *   Table Connections:
 *   ------------------
 *   [Explicit] orders.customer_id -> customers.id
 *   [Implied]  order_items.order_id -> orders.id
 */
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
