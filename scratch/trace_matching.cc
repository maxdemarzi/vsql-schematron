#include <iostream>
#include <string>
#include "name_matching.h"
#include "schema_analyzer_helpers.h"

int main() {
    std::cout << "matchTableName(custom_page_ordered, SAKAI_SITE_PAGE, true): " 
              << matchTableName("custom_page_ordered", "SAKAI_SITE_PAGE", true) << "\n";
    std::cout << "matchTableName(custom_page, SAKAI_SITE_PAGE, true): " 
              << matchTableName("custom_page", "SAKAI_SITE_PAGE", true) << "\n";
    std::cout << "matchCleanTableNames(custom_page, SAKAI_SITE_PAGE, true): " 
              << matchCleanTableNames("custom_page", "SAKAI_SITE_PAGE", true) << "\n";
    std::cout << "matchCleanTableNames(custom_page_ordered, SAKAI_SITE_PAGE, true): " 
              << matchCleanTableNames("custom_page_ordered", "SAKAI_SITE_PAGE", true) << "\n";
    return 0;
}
