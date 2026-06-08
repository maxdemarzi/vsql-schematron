import json
import subprocess

with open("failed/680894_004quartz.json") as f:
    data = json.load(f)
tables = data.get("TABLES", {})
declared_fks = set()
for tbl_name, tbl_info in tables.items():
    for fk in tbl_info.get("FOREIGN_KEYS", []):
        cols = fk.get("COLUMNS", [])
        ref_tbl = fk.get("FOREIGN_TABLE", "")
        ref_cols = fk.get("REFERRED_COLUMNS", [])
        if len(cols) == 1 and len(ref_cols) == 1:
            declared_fks.add((tbl_name.lower(), cols[0].lower(), ref_tbl.lower(), ref_cols[0].lower()))

input_lines = []
for tbl_name, tbl_info in tables.items():
    input_lines.append(f"TABLE {tbl_name}")
    pks = tbl_info.get("PRIMARY_KEYS", [])
    if pks:
        input_lines.append("PK " + " ".join(pks))
    for col_name, col_meta in tbl_info.get("COLUMNS", {}).items():
        input_lines.append(f"COL {col_name} {col_meta.get('TYPE', 'INT')}")
input_str = "\n".join(input_lines) + "\n"

res = subprocess.run(["build/schema_checker"], input=input_str, capture_output=True, text=True, timeout=5.0)
inferred_fks = set()
for line in res.stdout.splitlines():
    if line.startswith("RELATIONSHIP "):
        parts = line[len("RELATIONSHIP "):].split(" -> ")
        if len(parts) == 2:
            from_part, to_part = parts
            from_tbl, from_col = from_part.rsplit(".", 1)
            to_tbl, to_col = to_part.rsplit(".", 1)
            inferred_fks.add((from_tbl.lower(), from_col.lower(), to_tbl.lower(), to_col.lower()))

sym_diff = declared_fks ^ inferred_fks
print(f"Symmetric Difference Size: {len(sym_diff)}")
print(f"Missing: {declared_fks - inferred_fks}")
print(f"Extra: {inferred_fks - declared_fks}")
