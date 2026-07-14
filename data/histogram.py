import numpy as np
import matplotlib.pyplot as plt

FILENAME = "batches.bin"          # flat binary file of uint32_t
TOP_N = 1000                   # number of most common values to plot


def main():
    # Memory-map the file (efficient for 100M entries)
    data = np.memmap(FILENAME, dtype=np.uint32, mode="r")
    print(f"Loaded {data.size:,} uint32 entries")

    # Count occurrences of all values
    values, counts = np.unique(data, return_counts=True)

    # Sort by frequency (descending)
    idx = np.argsort(counts)[::-1]
    top_values = values[idx][:TOP_N]
    top_counts = counts[idx][:TOP_N]

    print(f"Most common value: {top_values[0]} (count={top_counts[0]:,})")

    # Plot histogram of the top-N counts
    plt.figure(figsize=(14, 6))
    plt.bar(range(TOP_N), top_counts, color="steelblue")
    plt.title(f"Top {TOP_N} Most Common uint32 Values")
    plt.xlabel("Rank (0 = most common)")
    plt.ylabel("Count")
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
