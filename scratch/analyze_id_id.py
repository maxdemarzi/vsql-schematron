import json
import os

def main():
    off_by_1_path = "/home/maxdemarzi/.gemini/antigravity-ide/brain/b52a7292-4558-472c-82c1-d49e9c4294bb/scratch/off_by_1_schemas.json"
    with open(off_by_1_path, "r") as f:
        data = json.load(f)
    
    id_id_missing = [x for x in data if x["type"] == "MISSING" and x["diff"][1] == "id" and x["diff"][3] == "id"]
    print(f"Found {len(id_id_missing)} cases of missing id -> id:")
    
    for idx, case in enumerate(id_id_missing):
        filename = case["filename"]
        tbl_a, col_a, tbl_b, col_b = case["diff"]
        filepath = os.path.join("/home/maxdemarzi/vsql-schematron/failed", filename)
        
        with open(filepath, "r") as f_schema:
            schema = json.load(f_schema)
            
        tables = schema.get("TABLES", {})
        info_a = tables.get(tbl_a, {})
        info_b = tables.get(tbl_b, {})
        
        pks_a = info_a.get("PRIMARY_KEYS", [])
        pks_b = info_b.get("PRIMARY_KEYS", [])
        
        type_a = info_a.get("COLUMNS", {}).get("id", {}).get("TYPE", "UNKNOWN")
        type_b = info_b.get("COLUMNS", {}).get("id", {}).get("TYPE", "UNKNOWN")
        
        print(f"{idx+1}. File: {filename}")
        print(f"   Relationship: {tbl_a}.id ({type_a}, PKs: {pks_a}) -> {tbl_b}.id ({type_b}, PKs: {pks_b})")

if __name__ == "__main__":
    main()
