import subprocess
import itertools
import csv

# --- Configuration ---
EXE_PATH = "./build/msvc-release/Release/OpenGLProject.exe"

# Define the ranges of values you want to test
# Example: Testing different widths and heights, or object counts
PARAM_A_VALUES = [5, 10, 15, 16, 20, 32, 50, 64, 96, 100, 128, 256] 
PARAM_B_VALUES = [32, 64, 96, 128, 192, 256, 384, 512]

OUTPUT_FILE = "benchmark_results.csv"
# ---------------------

def run_benchmark(val_a, val_b):
    """Runs the executable and extracts the frametime from the last line."""
    try:
        # Construct the command: ./path/to/exe 10 64
        cmd = [EXE_PATH, str(val_a), str(val_b)]
        
        # Run the process and capture output
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        
        # Get the last line (equivalent to tail -n 1)
        lines = result.stdout.strip().splitlines()
        if not lines:
            return None
        last_line = lines[-1]
        
        print(lines)
        print("###")
        print(last_line)
        
        # Split the line (e.g., "64 10 1.15619") and take the 3rd element
        parts = last_line.split()
        if len(parts) >= 3:
            return float(parts[2])
        
    except (subprocess.CalledProcessError, ValueError, IndexError) as e:
        print(f"Error running {val_a} {val_b}: {e}")
        return None

def main():
    results = []
    
    # Generate all combinations of the provided parameters
    combinations = list(itertools.product(PARAM_A_VALUES, PARAM_B_VALUES))
    total = len(combinations)
    
    print(f"Starting benchmark: {total} combinations to test...")

    for i, (a, b) in enumerate(combinations, 1):
        print(f"[{i}/{total}] Testing {a} {b}...", end=" ", flush=True)
        
        frametime = run_benchmark(a, b)
        
        if frametime is not None:
            print(f"Frametime: {frametime}")
            results.append((a, b, frametime))
        else:
            print("Failed!")

    # Save results to a CSV file
    with open(OUTPUT_FILE, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["ParamA", "ParamB", "Frametime"]) # Header
        writer.writerows(results)

    print(f"\nDone! Results saved to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()