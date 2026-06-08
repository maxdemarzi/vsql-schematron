import os
import json
import subprocess
import concurrent.futures
from collections import Counter

def run_checker(input_str):
    checker_path = "/home/maxdemarzi/vsql-schematron/build/schema_checker"
    try:
        res = subprocess.run([checker_path], input=input_str, capture_output=True, text=True, timeout=10.0)
        if res.returncode != 0:
            return None
        return res.stdout
    except subprocess.TimeoutExpired:
        return "TIMEOUT"

def evaluate_schema(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        try:
            data = json.load(f)
        except Exception:
            return None, None

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
            col_type = col_meta.get("TYPE", "INT")
            input_lines.append(f"COL {col_name} {col_type}")

    input_str = "\n".join(input_lines) + "\n"
    
    output = run_checker(input_str)
    if output == "TIMEOUT":
        return "TIMEOUT"
    if output is None:
        return None, None

    inferred_fks = set()
    for line in output.splitlines():
        if line.startswith("RELATIONSHIP "):
            parts = line[len("RELATIONSHIP "):].split(" -> ")
            if len(parts) == 2:
                from_part, to_part = parts
                from_tbl, from_col = from_part.rsplit(".", 1)
                to_tbl, to_col = to_part.rsplit(".", 1)
                inferred_fks.add((from_tbl.lower(), from_col.lower(), to_tbl.lower(), to_col.lower()))

    return declared_fks, inferred_fks, tables

def process_file(filename, failed_dir):
    filepath = os.path.join(failed_dir, filename)
    res = evaluate_schema(filepath)
    if res in ["TIMEOUT", None]:
        return None
    declared, inferred, tables = res
    if declared is None or inferred is None:
        return None
    
    missing_fks = declared - inferred
    extra_fks = inferred - declared
    
    if len(missing_fks) > 0:
        return {
            "filename": filename,
            "missing_count": len(missing_fks),
            "extra_count": len(extra_fks),
            "missing": list(missing_fks),
            "extra": list(extra_fks)
        }
    return None

def main():
    failed_dir = "/home/maxdemarzi/vsql-schematron/failed"
    files = sorted([f for f in os.listdir(failed_dir) if f.endswith(".json")])
    
    print(f"Scanning {len(files)} failed schemas for MISSING relationship patterns...")
    
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
        futures = {executor.submit(process_file, filename, failed_dir): filename for filename in files}
        for idx, future in enumerate(concurrent.futures.as_completed(futures)):
            res = future.result()
            if res is not None:
                results.append(res)
            if (idx + 1) % 1000 == 0:
                print(f"Processed {idx + 1}/{len(files)}...")
                
    print(f"\nFound {len(results)} schemas with at least 1 missing relationship.")
    
    # Analyze the missing relationships by column pair names
    col_pairs = Counter()
    for item in results:
        for from_tbl, from_col, to_tbl, to_col in item["missing"]:
            col_pairs[(from_col, to_col)] += 1
            
    print("\nTop Missing Column Name Pairs:")
    for (from_col, to_col), count in col_pairs.most_common(30):
        print(f"  {count:4d} x {from_col} -> {to_col}")
        
    # Let's inspect some example schemas for the top missing patterns
    for (from_col, to_col), count in col_pairs.most_common(10):
        print(f"\n--- Examples of missing `{from_col} -> {to_col}` (showing up to 5) ---")
        found = 0
        for item in results:
            for from_tbl, fc, to_tbl, tc in item["missing"]:
                if fc == from_col and tc == to_col:
                    print(f"  - {item['filename']}: {from_tbl}.{from_col} -> {to_tbl}.{to_col} (extras: {item['extra_count']})")
                    found += 1
                    break
            if found >= 5:
                break

if __name__ == "__main__":
    main()
