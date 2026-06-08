import json
with open("failed/680894_004quartz.json") as f:
    data = json.load(f)
for tbl, info in data.get("TABLES", {}).items():
    if info.get("FOREIGN_KEYS"):
        print(f"Table {tbl}:")
        for fk in info["FOREIGN_KEYS"]:
            print(f"  {fk['COLUMNS']} -> {fk['FOREIGN_TABLE']}{fk['REFERRED_COLUMNS']}")
