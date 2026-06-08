import json

def main():
    with open("/home/maxdemarzi/.gemini/antigravity-ide/brain/b52a7292-4558-472c-82c1-d49e9c4294bb/scratch/off_by_1_schemas.json", "r") as f:
        data = json.load(f)
        
    act_extras = []
    for item in data:
        if item["type"] == "EXTRA":
            tbl_a, col_a, tbl_b, col_b = item["diff"]
            if tbl_a.lower().startswith("act_") or tbl_b.lower().startswith("act_"):
                act_extras.append(item)
                
    print(f"Total ACT_ extras: {len(act_extras)}")
    for item in act_extras:
        tbl_a, col_a, tbl_b, col_b = item["diff"]
        print(f"  {item['filename']}: {tbl_a}.{col_a} -> {tbl_b}.{col_b}")

if __name__ == "__main__":
    main()
