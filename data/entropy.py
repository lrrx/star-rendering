import numpy as np
import matplotlib.pyplot as plt
from math import log2

# -----------------------------
# CONFIGURATION
# -----------------------------
FILENAME = "batches.bin"          # flat binary file of uint32_t
WINDOW_SIZE = 1_000_000        # sliding window size (1M entries)
HIST_BINS = 200                # histogram bins for plotting
# -----------------------------


def compute_entropy(arr):
    """Compute Shannon entropy (bits) of a numpy array of uint32."""
    if arr.size == 0:
        return 0.0

    # Count occurrences
    values, counts = np.unique(arr, return_counts=True)
    p = counts / counts.sum()

    # Shannon entropy
    return -(p * np.log2(p)).sum()


def sliding_window_entropy(memmap_arr, window_size):
    """Compute entropy for each sliding window."""
    n = memmap_arr.size
    entropies = []

    for start in range(0, n, window_size):
        end = min(start + window_size, n)
        window = memmap_arr[start:end]
        entropies.append(compute_entropy(window))

    return np.array(entropies)


def main():
    # Memory-map the file (zero RAM cost)
    data = np.memmap(FILENAME, dtype=np.uint32, mode="r")

    print(f"Loaded {data.size:,} uint32 entries")

    # ---- Global entropy ----
    global_entropy = compute_entropy(data)
    print(f"Global entropy: {global_entropy:.4f} bits")

    # ---- Sliding window entropy ----
    ent = sliding_window_entropy(data, WINDOW_SIZE)

    # ---- Plot histogram ----
    plt.figure(figsize=(12, 5))
    plt.hist(data[:5_000_000], bins=HIST_BINS, color="steelblue")
    plt.title("Value Distribution (first 5M entries)")
    plt.xlabel("uint32 value")
    plt.ylabel("Count")
    plt.tight_layout()
    plt.show()

    # ---- Plot entropy over windows ----
    plt.figure(figsize=(12, 5))
    plt.plot(ent, marker="o", markersize=3)
    plt.title(f"Entropy per {WINDOW_SIZE:,}-entry window")
    plt.xlabel("Window index")
    plt.ylabel("Entropy (bits)")
    plt.grid(True)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()

