import os
import json

def get_quartz_fks(directory):
    files = [f for f in os.listdir(directory) if f.endswith(".json")]
    for filename in files:
        filepath = os.path.join(directory, filename)
        with open(filepath, 'r') as f:
            try:
                data = json.load(f)
            except:
                continue
        tables = data.get("TABLES", {})
        # Check if it's Quartz
        qrtz_count = sum(1 for t in tables if t.lower().startswith("qrtz_"))
        if qrtz_count < 3:
            continue
            
        print(f"Schema: {filename} in {directory} (tables starting with qrtz_: {qrtz_count})")
        for tbl_name, tbl_info in tables.items():
            for fk in tbl_info.get("FOREIGN_KEYS", []):
                cols = fk.get("COLUMNS", [])
                ref_tbl = fk.get("FOREIGN_TABLE", "")
                ref_cols = fk.get("REFERRED_COLUMNS", [])
                if len(cols) == 1 and len(ref_cols) == 1:
                    print(f"  {tbl_name.lower()}.{cols[0].lower()} -> {ref_tbl.lower()}.{ref_cols[0].lower()}")

print("=== FAILED DIRECTORY ===")
get_quartz_fks("failed")
print("\n=== DONE DIRECTORY ===")
get_quartz_fks("done")
