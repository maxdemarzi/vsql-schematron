import os
import json

def main():
    input_file = "schemapile-perm.json"
    output_dir = "schemas"
    
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found in the current directory.")
        return
        
    print(f"Reading {input_file} (this might take a moment as the file is ~327MB)...")
    
    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            schemapile = json.load(f)
    except Exception as e:
        print(f"Error loading JSON file: {e}")
        return
        
    print(f"Successfully loaded {len(schemapile)} schemas.")
    
    # Create the output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)
    print(f"Saving schemas to '{output_dir}/' directory...")
    
    count = 0
    for schema_name, schema_content in schemapile.items():
        # Sanitize filename to avoid path traversal or invalid characters
        safe_name = schema_name.replace("/", "_").replace("\\", "_")
        
        # Replace .sql suffix with .json, or append .json if not present
        if safe_name.lower().endswith(".sql"):
            filename = safe_name[:-4] + ".json"
        else:
            filename = safe_name + ".json"
            
        dest_path = os.path.join(output_dir, filename)
        
        try:
            with open(dest_path, 'w', encoding='utf-8') as out_f:
                json.dump(schema_content, out_f, indent=2)
            count += 1
            if count % 1000 == 0:
                print(f"Processed {count} schemas...")
        except Exception as e:
            print(f"Error writing schema '{schema_name}' to {dest_path}: {e}")
            
    print(f"Success! Split {count} schemas into '{output_dir}/'.")

if __name__ == "__main__":
    main()
