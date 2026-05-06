import json
import os
import random
import string

def generate_random_string(length=20):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def generate_data_entry(i):
    """Generates a dictionary containing all JSON-compatible types."""
    return {
        "id": i,
        "uuid": generate_random_string(32),
        "active": random.choice([True, False]),
        "metadata": None,
        "score": random.uniform(0, 1000),
        "tags": [generate_random_string(5) for _ in range(5)],
        "info": {
            "version": random.randint(1, 10),
            "label": "experimental",
            "nested_list": [random.randint(1, 100) for _ in range(3)]
        }
    }

def create_giant_json(filename, target_size_gb):
    target_bytes = target_size_gb * 1024 * 1024 * 1024
    current_size = 0
    iteration = 0
    
    print(f"Generating approximately {target_size_gb}GB of JSON data...")

    with open(filename, 'w') as f:
        f.write("[\n")  # Start of the JSON array
        
        while current_size < target_bytes:
            record = generate_data_entry(iteration)
            json_str = json.dumps(record)
            
            # Add a comma if it's not the first record
            if iteration > 0:
                f.write(",\n")
            
            f.write(json_str)
            
            # Update size tracking (approximate)
            current_size = f.tell()
            
            if iteration % 100000 == 0:
                progress = (current_size / target_bytes) * 100
                print(f"Progress: {progress:.2f}% ({current_size / (1024**2):.2f} MB)")
            
            iteration += 1
            
        f.write("\n]")  # End of the JSON array

    print(f"\nDone! File '{filename}' created.")
    print(f"Final size: {os.path.getsize(filename) / (1024**3):.2f} GB")

if __name__ == "__main__":
    # Configuration
    FILE_NAME = "massive_data.json"
    TARGET_GB = 2 
    
    create_giant_json(FILE_NAME, TARGET_GB)