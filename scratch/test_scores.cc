#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include "string_helpers.h"
#include "name_matching.h"
#include "domain_specific_matching.h"
#include "schema_analyzer_helpers.h"
#include "schema_analyzer.h"
#include "implied_relationships.h"

int main() {
    std::vector<std::string> table_names = {
        "QRTZ_TRIGGERS", "QRTZ_SIMPLE_TRIGGERS", "QRTZ_CRON_TRIGGERS", "rds_security_group"
    };

    std::unordered_map<std::string, TableInfo> tables_info;
    std::unordered_map<std::string, std::vector<std::string>> effective_pks;

    // QRTZ_TRIGGERS
    {
        TableInfo info;
        info.column_types["TRIGGER_NAME"] = "VARCHAR";
        info.column_types["TRIGGER_GROUP"] = "VARCHAR";
        info.column_types["JOB_NAME"] = "VARCHAR";
        info.column_types["JOB_GROUP"] = "VARCHAR";
        info.pk_columns = {"TRIGGER_NAME", "TRIGGER_GROUP"};
        tables_info["QRTZ_TRIGGERS"] = info;
        effective_pks["QRTZ_TRIGGERS"] = info.pk_columns;
    }

    // QRTZ_SIMPLE_TRIGGERS
    {
        TableInfo info;
        info.column_types["TRIGGER_NAME"] = "VARCHAR";
        info.column_types["TRIGGER_GROUP"] = "VARCHAR";
        info.column_types["REPEAT_COUNT"] = "BIGINT";
        info.column_types["REPEAT_INTERVAL"] = "BIGINT";
        info.pk_columns = {"TRIGGER_NAME", "TRIGGER_GROUP"};
        tables_info["QRTZ_SIMPLE_TRIGGERS"] = info;
        effective_pks["QRTZ_SIMPLE_TRIGGERS"] = info.pk_columns;
    }

    // QRTZ_CRON_TRIGGERS
    {
        TableInfo info;
        info.column_types["TRIGGER_NAME"] = "VARCHAR";
        info.column_types["TRIGGER_GROUP"] = "VARCHAR";
        info.column_types["CRON_EXPRESSION"] = "VARCHAR";
        info.pk_columns = {"TRIGGER_NAME", "TRIGGER_GROUP"};
        tables_info["QRTZ_CRON_TRIGGERS"] = info;
        effective_pks["QRTZ_CRON_TRIGGERS"] = info.pk_columns;
    }

    // rds_security_group
    {
        TableInfo info;
        info.column_types["rds_db_security_group_id"] = "BIGINT";
        info.column_types["dbsecurityGroupDescription"] = "VARCHAR";
        info.pk_columns = {"rds_db_security_group_id"};
        tables_info["rds_security_group"] = info;
        effective_pks["rds_security_group"] = info.pk_columns;
    }

    std::set<std::pair<std::string, std::string>> explicit_mapped_cols;
    std::set<Relationship> relationships;

    findImpliedRelationships(table_names, tables_info, explicit_mapped_cols, relationships);

    std::cout << "\nResults:\n";
    for (const auto& rel : relationships) {
        std::cout << rel.from_table << "." << rel.from_column << " -> "
                  << rel.to_table << "." << rel.to_column << "\n";
    }

    return 0;
}
