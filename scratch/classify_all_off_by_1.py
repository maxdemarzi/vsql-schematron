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
    
    sym_diff = declared ^ inferred
    if len(sym_diff) == 1:
        diff_item = list(sym_diff)[0]
        is_missing = diff_item in declared
        tbl_a, col_a, tbl_b, col_b = diff_item
        
        # Gather table info
        pk_a = tables.get(tbl_a, {}).get("PRIMARY_KEYS", [])
        pk_b = tables.get(tbl_b, {}).get("PRIMARY_KEYS", [])
        
        type_a = tables.get(tbl_a, {}).get("COLUMNS", {}).get(col_a, {}).get("TYPE", "UNKNOWN")
        type_b = tables.get(tbl_b, {}).get("COLUMNS", {}).get(col_b, {}).get("TYPE", "UNKNOWN")
        
        is_col_a_pk = col_a in [k.lower() for k in pk_a]
        is_col_b_pk = col_b in [k.lower() for k in pk_b]
        
        num_tables = len(tables)
        
        return {
            "filename": filename,
            "type": "MISSING" if is_missing else "EXTRA",
            "tbl_a": tbl_a, "col_a": col_a,
            "tbl_b": tbl_b, "col_b": col_b,
            "type_a": type_a, "type_b": type_b,
            "is_col_a_pk": is_col_a_pk,
            "is_col_b_pk": is_col_b_pk,
            "num_tables": num_tables
        }
    return None

def main():
    failed_dir = "/home/maxdemarzi/vsql-schematron/failed"
    files = sorted([f for f in os.listdir(failed_dir) if f.endswith(".json")])
    
    print(f"Scanning {len(files)} failed schemas for off-by-one clusters...")
    
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
        futures = {executor.submit(process_file, filename, failed_dir): filename for filename in files}
        for idx, future in enumerate(concurrent.futures.as_completed(futures)):
            res = future.result()
            if res is not None:
                results.append(res)
            if (idx + 1) % 1000 == 0:
                print(f"Processed {idx + 1}/{len(files)}...")
                
    # Grouping by pattern: (type, col_a, col_b, is_col_a_pk, is_col_b_pk, num_tables_group)
    # num_tables_group could be "2" or ">2"
    patterns = {}
    for item in results:
        t_group = "2-tables" if item["num_tables"] == 2 else "multi-table"
        pattern = (item["type"], item["col_a"], item["col_b"], item["is_col_a_pk"], item["is_col_b_pk"], t_group)
        if pattern not in patterns:
            patterns[pattern] = []
        patterns[pattern].append(item)
        
    sorted_patterns = sorted(patterns.items(), key=lambda x: len(x[1]), reverse=True)
    
    print(f"\nFound {len(results)} Off by 1 schemas in total.")
    print("Top discrepancy clusters:")
    for idx, (pattern, items) in enumerate(sorted_patterns[:15]):
        p_type, col_a, col_b, pk_a, pk_b, t_group = pattern
        print(f"\nCluster {idx+1}: {len(items)} schemas")
        print(f"  Pattern: {p_type} relationship from `{col_a}` (PK={pk_a}) to `{col_b}` (PK={pk_b}) in a {t_group} schema.")
        print("  Examples:")
        for ex in items[:5]:
            print(f"    - {ex['filename']}: {ex['tbl_a']}.{ex['col_a']} -> {ex['tbl_b']}.{ex['col_b']} (types: {ex['type_a']} -> {ex['type_b']})")

if __name__ == "__main__":
    main()
