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
        "TB_MEDICAMENTO", "TB_QUARTO", "TB_TESTE", "TB_CONTA"
    };

    std::unordered_map<std::string, TableInfo> tables_info;
    std::unordered_map<std::string, std::vector<std::string>> effective_pks;

    // TB_MEDICAMENTO
    {
        TableInfo info;
        info.column_types["ID_MEDICAMENTO"] = "INT";
        info.column_types["NOME_MEDICAMENTO"] = "VARCHAR";
        info.column_types["CUSTO_MEDICAMENTO"] = "DECIMAL";
        info.pk_columns = {"ID_MEDICAMENTO"};
        tables_info["TB_MEDICAMENTO"] = info;
        effective_pks["TB_MEDICAMENTO"] = info.pk_columns;
    }

    // TB_QUARTO
    {
        TableInfo info;
        info.column_types["ID_QUARTO"] = "INT";
        info.column_types["CUSTO_QUARTO"] = "DECIMAL";
        info.pk_columns = {"ID_QUARTO"};
        tables_info["TB_QUARTO"] = info;
        effective_pks["TB_QUARTO"] = info.pk_columns;
    }

    // TB_TESTE
    {
        TableInfo info;
        info.column_types["ID_TESTE"] = "INT";
        info.column_types["NOME_TESTE"] = "VARCHAR";
        info.column_types["CUSTO_TESTE"] = "DECIMAL";
        info.pk_columns = {"ID_TESTE"};
        tables_info["TB_TESTE"] = info;
        effective_pks["TB_TESTE"] = info.pk_columns;
    }

    // TB_CONTA
    {
        TableInfo info;
        info.column_types["DATA_CONTA"] = "DATETIME";
        info.column_types["ID_PACIENTE"] = "INT";
        info.column_types["CUSTO_TESTE_CONTA"] = "DECIMAL";
        info.column_types["CUSTO_QUARTO_CONTA"] = "DECIMAL";
        info.column_types["CUSTO_MEDICAMENTO_CONTA"] = "DECIMAL";
        info.column_types["CUSTO_OUTROS_CONTA"] = "DECIMAL";
        info.pk_columns = {"DATA_CONTA", "ID_PACIENTE"};
        tables_info["TB_CONTA"] = info;
        effective_pks["TB_CONTA"] = info.pk_columns;
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
