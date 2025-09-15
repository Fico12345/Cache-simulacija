import pandas as pd
import matplotlib.pyplot as plt

csv_path = "results/cache_results.csv"

rows = []
with open(csv_path) as f:
    for line in f:
        line = line.strip()
        # preskoči prazne i komentare
        if not line or line.startswith("---") or line.startswith("Cache") or line.startswith("Asocijativnost"):
            continue
        parts = line.split(",")
        if len(parts) == 7:  # očekujemo 7 vrijednosti
            size, block, ways, hits, misses, hit_rate, miss_rate = parts
            rows.append({
                "Cache Size": int(size),
                "Block Size": int(block),
                "Ways": int(ways),
                "Hits": int(hits),
                "Misses": int(misses),
                "Hit Rate": float(hit_rate),
                "Miss Rate": float(miss_rate),
            })

# prebaci u DataFrame
df = pd.DataFrame(rows)

print("Učitani podaci:")
print(df)

# nacrtaj usporedbu hit/miss rate-a
plt.figure(figsize=(8,5))
for ways, group in df.groupby("Ways"):
    plt.plot(group["Cache Size"], group["Miss Rate"], marker="o", label=f"{ways}-way")

plt.xlabel("Cache Size (B)")
plt.ylabel("Miss Rate (%)")
plt.title("Usporedba cache politika")
plt.legend()
plt.grid(True)
plt.show()
