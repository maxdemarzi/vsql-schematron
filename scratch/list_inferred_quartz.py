import json
import subprocess

with open("failed/680894_004quartz.json") as f:
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

res = subprocess.run(["build/schema_checker"], input=input_str, capture_output=True, text=True, timeout=5.0)
print("=== INFERRED ===")
for line in sorted(res.stdout.splitlines()):
    if line.startswith("RELATIONSHIP "):
        print(line)
