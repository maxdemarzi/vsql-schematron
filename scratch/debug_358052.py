import json
import subprocess

def main():
    filepath = "/home/maxdemarzi/vsql-schematron/done/358052_db_schema.json"
    with open(filepath) as f:
        data = json.load(f)
    input_lines = []
    for tbl_name, tbl_info in data.get("TABLES", {}).items():
        input_lines.append(f"TABLE {tbl_name}")
        pks = tbl_info.get("PRIMARY_KEYS", [])
        if pks:
            input_lines.append("PK " + " ".join(pks))
        for col_name, col_meta in tbl_info.get("COLUMNS", {}).items():
            input_lines.append(f"COL {col_name} {col_meta.get('TYPE', 'INT')}")
        for fk in tbl_info.get("FOREIGN_KEYS", []):
            cols = fk.get("COLUMNS", [])
            ref_tbl = fk.get("FOREIGN_TABLE", '')
            ref_cols = fk.get("REFERRED_COLUMNS", [])
            if len(cols) == 1 and len(ref_cols) == 1:
                input_lines.append(f"FK {cols[0]} {ref_tbl} {ref_cols[0]}")
    input_str = "\n".join(input_lines) + "\n"
    res = subprocess.run(["/home/maxdemarzi/vsql-schematron/build/schema_checker"], input=input_str, capture_output=True, text=True)
    print("STDOUT:")
    print(res.stdout)
    print("STDERR:")
    print(res.stderr)

if __name__ == "__main__":
    main()
