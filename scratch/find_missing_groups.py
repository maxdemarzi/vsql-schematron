import json
from collections import Counter

def main():
    with open("/home/maxdemarzi/.gemini/antigravity-ide/brain/b52a7292-4558-472c-82c1-d49e9c4294bb/scratch/off_by_1_schemas.json", "r") as f:
        data = json.load(f)
        
    missing = [x for x in data if x["type"] == "MISSING"]
    print(f"Total MISSING off-by-one schemas: {len(missing)}")
    
    # Let's count how many schemas have the same (col_a, col_b) or similar patterns
    patterns = Counter()
    for item in missing:
        tbl_a, col_a, tbl_b, col_b = item["diff"]
        # Normalize the table names slightly (e.g. remove prefixes/suffixes)
        patterns[(col_a, col_b)] += 1
        
    print("\n=== TOP MISSING COLUMN PAIRS ===")
    for pat, count in patterns.most_common(50):
        print(f"  {count:3d} x {pat[0]} -> {pat[1]}")
        
    # Let's print the file names for the top ones to see what schemas they are
    for pat, count in patterns.most_common(10):
        if count >= 3:
            print(f"\n--- {count} x {pat[0]} -> {pat[1]} ---")
            for item in missing:
                tbl_a, col_a, tbl_b, col_b = item["diff"]
                if col_a == pat[0] and col_b == pat[1]:
                    print(f"  {item['filename']}: {tbl_a}.{col_a} -> {tbl_b}.{col_b}")

if __name__ == "__main__":
    main()
