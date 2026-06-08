#include <iostream>
#include <string>
#include "name_matching.h"

int main() {
    bool res = matchTableName("custo_medicamento_conta", "TB_MEDICAMENTO", true);
    std::cout << "matchTableName(custo_medicamento_conta, TB_MEDICAMENTO, true): " << (res ? "true" : "false") << "\n";
    
    bool res2 = matchTableName("custo_medicamento", "TB_MEDICAMENTO", true);
    std::cout << "matchTableName(custo_medicamento, TB_MEDICAMENTO, true): " << (res2 ? "true" : "false") << "\n";
    
    return 0;
}
