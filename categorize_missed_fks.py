import os
import json
from collections import Counter

def to_lower(s):
    return s.lower()

def strip_table_prefix(name):
    n = name.lower()
    if n.startswith("tbl_"):
        return n[4:]
    if n.startswith("ref"):
        return n[3:]
    underscore = n.find('_')
    while underscore != -1 and underscore > 0:
        prefix = n[:underscore]
        if prefix in ["idn", "oauth", "comtn", "vsql", "sys", "db", "tbl", "ref"] or len(prefix) <= 3:
            n = n[underscore + 1:]
            underscore = n.find('_')
        else:
            break
    return n

def match_table_name(col_prefix, tbl_name):
    prefix = col_prefix.lower()
    tbl = tbl_name.lower()
    
    clean_tbl = strip_table_prefix(tbl)
    clean_prefix = strip_table_prefix(prefix)
    
    # Common abbreviations list
    abbreviations = {
        "dept": "department",
        "cust": "customer",
        "emp": "employee",
        "mgr": "manager",
        "org": "organization",
        "src": "source",
        "dest": "destination",
        "addr": "address",
        "desc": "description",
        "prod": "product",
        "cat": "category",
        "msg": "message",
        "pos": "position",
        "usr": "user",
        "grp": "group",
        "auth": "authority",
        "info": "information",
        "spec": "specification"
    }
    
    def match_clean(p, t):
        if p == t:
            return True
        if t == p + "s":
            return True
        if t == p + "es":
            return True
        if len(p) > 1 and p[-1] == 'y':
            if t == p[:-1] + "ies":
                return True
                
        # Handle abbreviations
        for abb, full in abbreviations.items():
            if p == abb:
                if t == full or t == full + "s" or t == full + "es":
                    return True
            if t == abb:
                if p == full or p == full + "s" or p == full + "es":
                    return True
                    
        p_clean = p.replace("_", "")
        t_clean = t.replace("_", "")
        if p_clean == t_clean:
            return True
        if t_clean == p_clean + "s":
            return True
        if len(p_clean) >= 3 and (t_clean.startswith(p_clean) or t_clean.endswith(p_clean)):
            return True
        return False
        
    if match_clean(prefix, tbl): return True
    if match_clean(clean_prefix, clean_tbl): return True
    if match_clean(prefix, clean_tbl): return True
    if match_clean(clean_prefix, tbl): return True
    return False

# Check if types are compatible (e.g. numeric types are compatible, character types are compatible)
def type_compatible(t1, t2):
    t1 = t1.lower()
    t2 = t2.lower()
    if t1 == t2:
        return True
    
    # Integer compatibility
    integers = ["int", "integer", "bigint", "smallint", "tinyint", "mediumint", "unsigned int", "unsigned bigint", "serial", "numeric", "number"]
    is_int_1 = any(x in t1 for x in ["int", "serial", "numeric", "number"])
    is_int_2 = any(x in t2 for x in ["int", "serial", "numeric", "number"])
    if is_int_1 and is_int_2:
        return True
        
    # Character/string compatibility
    strings = ["char", "varchar", "text", "varchar2", "nvarchar", "string", "uuid"]
    is_str_1 = any(x in t1 for x in strings)
    is_str_2 = any(x in t2 for x in strings)
    if is_str_1 and is_str_2:
        return True
        
    return False

def split_column_name(col):
    underscore_pos = col.rfind('_')
    if underscore_pos != -1 and underscore_pos > 0 and underscore_pos < len(col) - 1:
        return col[:underscore_pos], col[underscore_pos + 1:]
    if len(col) > 1:
        for i in range(len(col) - 1, 0, -1):
            if col[i].isupper() and (not col[i-1].isupper() or (i + 1 < len(col) and col[i+1].islower())):
                return col[:i], col[i:]
    return None

def get_effective_pks(tbl_name, info):
    if len(info.get("pk", [])) > 0:
        return info["pk"]
    pks = []
    tbl_clean = strip_table_prefix(tbl_name)
    for col_name in info.get("columns", {}).keys():
        col_lower = col_name.lower()
        if col_lower == "id" or col_lower == tbl_clean + "_id" or col_lower == tbl_clean + "id" or col_lower == tbl_name.lower() + "_id" or col_lower == tbl_name.lower() + "id" or col_lower == "name" or col_lower == tbl_clean + "_name" or col_lower == tbl_clean + "name" or col_lower == tbl_name.lower() + "_name" or col_lower == tbl_name.lower() + "name":
            pks.append(col_name)
    return pks

def match_middle_id_convention(col, tbl_b):
    c = col.lower()
    pos = c.find("_id_")
    if pos != -1 and pos > 0:
        entity = c[:pos]
        if match_table_name(entity, tbl_b): return True
    if c.startswith("id_"):
        entity = c[3:]
        if match_table_name(entity, tbl_b): return True
    if c.startswith("id") and len(col) > 2 and col[2].isupper():
        entity = c[2:]
        if match_table_name(entity, tbl_b): return True
    return False

def is_subtype_table(tbl_a, tbl_b):
    a = tbl_a.lower()
    b = tbl_b.lower()
    clean_a = strip_table_prefix(a)
    clean_b = strip_table_prefix(b)
    if clean_a == clean_b: return True
    if len(clean_a) > len(clean_b) and clean_a.endswith(clean_b): return True
    if len(clean_b) > len(clean_a) and clean_b.endswith(clean_a): return True
    return False

def main():
    schemas_dir = "failed"
    files = [f for f in os.listdir(schemas_dir) if f.lower().endswith(".json")]
    
    type_mismatch_count = 0
    abbreviation_match_count = 0
    both_count = 0
    other_count = 0
    
    missed_examples = []
    
    for filename in files:
        filepath = os.path.join(schemas_dir, filename)
        with open(filepath, 'r', encoding='utf-8') as f:
            try:
                data = json.load(f)
            except:
                continue
                
        tables_info = {}
        tables = data.get("TABLES", {})
        for tbl_name, tbl_info in tables.items():
            columns = {}
            for col_name, col_meta in tbl_info.get("COLUMNS", {}).items():
                columns[col_name] = col_meta.get("TYPE", "")
            pk = tbl_info.get("PRIMARY_KEYS", [])
            tables_info[tbl_name] = {
                "columns": columns,
                "pk": pk
            }
            
        # Collect declared FKs and check how they were missed
        for tbl_a, tbl_info in tables.items():
            for fk in tbl_info.get("FOREIGN_KEYS", []):
                cols = fk.get("COLUMNS", [])
                ref_tbl = fk.get("FOREIGN_TABLE", "")
                ref_cols = fk.get("REFERRED_COLUMNS", [])
                
                if len(cols) == 1 and len(ref_cols) == 1:
                    col_a = cols[0]
                    col_b = ref_cols[0]
                    
                    type_a = tables_info[tbl_a]["columns"].get(col_a, "")
                    type_b = tables_info.get(ref_tbl, {}).get("columns", {}).get(col_b, "")
                    
                    # Simulated check WITHOUT type matching and WITH abbreviations
                    # Let's see if it would match if we allowed compatible types and abbreviations
                    
                    # 1. Check if it matches with exact match
                    is_exact_match = (col_a.lower() == col_b.lower() and col_a.lower() != "id")
                    
                    # 2. Check suffix matching
                    suffix_match = False
                    pks_b = get_effective_pks(ref_tbl, tables_info.get(ref_tbl, {}))
                    if col_b in pks_b:
                        split = split_column_name(col_a)
                        if split:
                            prefix_a, suffix_a = split
                            if suffix_a.lower() == col_b.lower():
                                suffix_match = match_table_name(prefix_a, ref_tbl)
                            else:
                                split_b = split_column_name(col_b)
                                if split_b:
                                    prefix_b, suffix_b = split_b
                                    if suffix_a.lower() == suffix_b.lower():
                                        suffix_match = match_table_name(prefix_a, ref_tbl)
                        if match_middle_id_convention(col_a, ref_tbl):
                            suffix_match = True
                            
                    # 3. Check subtype matching
                    subtype_match = (col_a.lower() == "id" and col_b.lower() == "id" and is_subtype_table(tbl_a, ref_tbl))
                    
                    would_match_name = is_exact_match or suffix_match or subtype_match
                    would_match_type = type_compatible(type_a, type_b)
                    
                    # Check if it was missed in our real run (which was strict on type and lacked abbreviations)
                    # Let's see if the strictly typed name-only without abbreviations matched
                    strict_type_match = (type_a.lower() == type_b.lower())
                    
                    # Real strict name match
                    strict_name_match = False
                    if is_exact_match:
                        strict_name_match = True
                    elif col_b in pks_b:
                        split = split_column_name(col_a)
                        if split:
                            prefix_a, suffix_a = split
                            # Standard table name matching without abbreviations
                            # Let's check table name match cleanly
                            clean_tbl = strip_table_prefix(ref_tbl)
                            clean_prefix = strip_table_prefix(prefix_a)
                            def match_no_abb(p, t):
                                if p == t or t == p + "s" or t == p + "es": return True
                                if len(p) > 1 and p[-1] == 'y' and t == p[:-1] + "ies": return True
                                p_clean = p.replace("_", "")
                                t_clean = t.replace("_", "")
                                if p_clean == t_clean or t_clean == p_clean + "s": return True
                                return False
                            # Suffix match check
                            suffix_matches_strict = False
                            if suffix_a.lower() == col_b.lower():
                                suffix_matches_strict = True
                            else:
                                split_b = split_column_name(col_b)
                                if split_b:
                                    prefix_b, suffix_b = split_b
                                    if suffix_a.lower() == suffix_b.lower():
                                        suffix_matches_strict = True
                            if suffix_matches_strict:
                                if match_no_abb(prefix_a, ref_tbl) or match_no_abb(clean_prefix, clean_tbl) or match_no_abb(prefix_a, clean_tbl) or match_no_abb(clean_prefix, ref_tbl):
                                    strict_name_match = True
                                    
                        # Middle ID convention check
                        # In real run we used matchTableName which lacked abbreviations, so middle id match is name-only
                        # Let's check if it matched
                        # We can simplify:
                        if match_middle_id_convention(col_a, ref_tbl):
                            # Check if the prefix table match required abbreviation
                            # Since we don't have abbreviation list in C++, we assume it lacked it
                            # If it matched under C++ rules, it would be strict
                            pass
                    
                    # If strictly matched, it's not missed. We only look at missed ones!
                    real_detected = strict_name_match and strict_type_match
                    if not real_detected:
                        # Categorize why it was missed
                        # Did it fail because of strict types?
                        type_fail = not strict_type_match and would_match_name and would_match_type
                        # Did it fail because of abbreviations/name?
                        name_fail = strict_type_match and not strict_name_match and would_match_name
                        # Both?
                        both_fail = not strict_type_match and not strict_name_match and would_match_name and would_match_type
                        
                        if type_fail:
                            type_mismatch_count += 1
                        elif name_fail:
                            abbreviation_match_count += 1
                        elif both_fail:
                            both_count += 1
                        else:
                            other_count += 1
                            if len(missed_examples) < 100:
                                missed_examples.append({
                                    "from": f"{tbl_a}.{col_a} ({type_a})",
                                    "to": f"{ref_tbl}.{col_b} ({type_b})",
                                    "schema": filename
                                })
                                
    print(f"Missed due to strict type mismatch (but compatible): {type_mismatch_count}")
    print(f"Missed due to abbreviation name mismatch (but matched abbreviation): {abbreviation_match_count}")
    print(f"Missed due to both: {both_count}")
    print(f"Missed due to other complex reasons: {other_count}")
    
    print("\n--- Other Complex Reason Examples ---")
    for m in missed_examples[:30]:
        print(f"Schema: {m['schema']}")
        print(f"  {m['from']} -> {m['to']}")
        print("-" * 40)

if __name__ == "__main__":
    main()
