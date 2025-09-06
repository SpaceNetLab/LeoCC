import os

delays = []

for root, dirs, files in os.walk("data"): # data directory contains bw and delay files
    for filename in files:
        if "delay_" in filename:
            delays.append(os.path.join(root, filename))

delays.sort()

with open("./result.txt", "w") as f:
    f.write(f"")

for delay in delays:
    with open(delay, "r") as f:
        delay_datas_before = f.readlines()

    delay_values = []
    for i, delay_data in enumerate(delay_datas_before):
        try:
            value = int(delay_data.strip())
            delay_values.append((value, i))
        except ValueError:
            continue
    delay_values_15s = delay_values[:1500]

    delay_values_15s = sorted(delay_values_15s, key=lambda x: x[0], reverse=True)[:100]
    sorted_delay_values = sorted(delay_values, key=lambda x: x[0], reverse=True)[:100]

    large_value_index = set()
    for value, position in sorted_delay_values:
        large_value_index.add(position)

    possibility = [0] * len(delay_values_15s)

    for k, (value, position) in enumerate(delay_values_15s):
        i = position
        while i < 12000:
            i += 1500
            for j in range(-10, 11):
                if i + j in large_value_index:
                    possibility[k] += 1
                    break

    def find_best_index(possibility, delay_values_15s):
        max_possibility = max(possibility)
        max_poss_indices = [
            i for i, p in enumerate(possibility) if p == max_possibility
        ]

        if len(max_poss_indices) == 1:
            return max_poss_indices[0]

        max_delay_in_candidates = max(delay_values_15s[i][0] for i in max_poss_indices)
        max_delay_indices = [
            i
            for i in max_poss_indices
            if delay_values_15s[i][0] == max_delay_in_candidates
        ]

        if len(max_delay_indices) == 1:
            return max_delay_indices[0]

        avg_index = sum(max_delay_indices) / len(max_delay_indices)
        return round(avg_index)

    best_index = find_best_index(possibility, delay_values_15s)

    with open("./result.txt", "a") as f:
        f.write(f"file: {os.path.basename(delay)}\n")
        f.write(f"time: {delay_values_15s[best_index][1]/100} s\n\n")
