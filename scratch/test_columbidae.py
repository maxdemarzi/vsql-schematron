import json
import subprocess
import os

def main():
    filepath = "/home/maxdemarzi/vsql-schematron/done/328560_columbidae.json"
    with open(filepath, 'r') as f:
        data = json.load(f)
        
    tables = data.get("TABLES", {})
    input_lines = []
    for tbl_name, tbl_info in tables.items():
        input_lines.append(f"TABLE {tbl_name}")
        pks = tbl_info.get("PRIMARY_KEYS", [])
        if pks:
            input_lines.append("PK " + " ".join(pks))
        for col_name, col_meta in tbl_info.get("COLUMNS", {}).items():
            input_lines.append(f"COL {col_name} {col_meta.get('TYPE', 'INT')}")
            
    input_str = "\n".join(input_lines) + "\n"
    
    checker_path = "/home/maxdemarzi/vsql-schematron/build/schema_checker"
    print("Running checker...")
    res = subprocess.run([checker_path], input=input_str, capture_output=True, text=True)
    print("Checker return code:", res.returncode)
    print("STDOUT:")
    print(res.stdout)
    print("STDERR:")
    print(res.stderr)

if __name__ == "__main__":
    main()
