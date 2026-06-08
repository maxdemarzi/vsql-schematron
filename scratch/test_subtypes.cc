#include <iostream>
#include <string>
#include "schema_analyzer_helpers.h"
#include "name_matching.h"

int main() {
    std::cout << "isSubtypeTable(project_logs, logs): " << isSubtypeTable("project_logs", "logs") << "\n";
    std::cout << "isSubtypeTable(logs, project_logs): " << isSubtypeTable("logs", "project_logs") << "\n";
    std::cout << "matchTableName(project_log, logs, false): " << matchTableName("project_log", "logs", false) << "\n";
    std::cout << "matchTableName(log, project_logs, false): " << matchTableName("log", "project_logs", false) << "\n";
    return 0;
}
