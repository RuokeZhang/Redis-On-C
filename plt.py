import matplotlib.pyplot as plt

# Data
items = {
    "window": 674,
    "door": 580,
    "cabinet": 980,
    "sideboardcabinet": 28,
    "teatable": 30,
    "nightstand": 102,
    "toilet": 120,
    "washingmachine": 92,
    "faucet": 245,
    "desk": 133,
    "tvstand": 33,
    "dishwasher": 67,
    "pan": 102,
    "electriccooker": 69,
    "microwave": 67,
    "other": 24,
    "pot": 76,
    "hearth": 69,
    "oven": 68,
    "shoecabinet": 32,
    "chestofdrawers": 30,
    "refrigerator": 70,
    "table": 25,
    "trashcan": 162,
    "basket": 15,
    "backpack": 6,
    "bottle": 266,
    "clock": 28,
    "laptop": 15,
    "mouse": 13,
    "pen": 243,
    "bowl": 87,
    "tray": 36,
    "telephone": 5,
    "plate": 251,
    "toy": 45,
    "keyboard": 14,
    "picture": 266,
    "cup": 254
}

# Sort items by count and take the top 12
sorted_items = sorted(items.items(), key=lambda x: x[1], reverse=True)[:12]

# Separate labels and values
labels, values = zip(*sorted_items)

# Plot
plt.figure(figsize=(10, 6))
plt.bar(labels, values)
plt.xticks(rotation=45, ha="right", fontsize=10)
plt.ylabel("Counts", fontsize=12)
plt.title("Top 12 Interactive Items", fontsize=14)
plt.tight_layout()

plt.show()
