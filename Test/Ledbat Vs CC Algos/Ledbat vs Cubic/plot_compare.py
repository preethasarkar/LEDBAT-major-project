import sys
import csv
import matplotlib.pyplot as plt

if len(sys.argv) < 4:
    print("Usage: python3 plot_competition.py <CC_ALGO> <BW> <RTT>")
    sys.exit(1)

cc_algo = sys.argv[1]
bw = sys.argv[2]
rtt = sys.argv[3]
# Include the dynamic CC algorithm in the output filename
output_filename = f"compare_{cc_algo}_{bw.replace('/', '_')}_{rtt}.png"

def load_data(filename):
    time, cwnd = [], []
    try:
        with open(filename, "r") as f:
            reader = csv.reader(f)
            for row in reader:
                if len(row) >= 3:
                    try:
                        time.append(float(row[0]))
                        cwnd.append(float(row[2]) / 1024.0) # CWND in KB
                    except ValueError:
                        continue
    except FileNotFoundError:
        print(f"Warning: {filename} not found.")
    return time, cwnd

# Dynamically load the chosen algorithm's CSV
t_cc, c_cc = load_data(f"./logs/cwnd_{cc_algo}.csv")
# Keep CUBIC as the static baseline
t_cubic, c_cubic = load_data("./logs/cwnd_cubic.csv")

if not t_cc and not t_cubic:
    print("No data found to plot.")
    sys.exit(1)

# Find the absolute starting time to sync both graphs
start_times = []
if t_cc: start_times.append(t_cc[0])
if t_cubic: start_times.append(t_cubic[0])
t0 = min(start_times)

# Normalize times
t_cc = [t - t0 for t in t_cc]
t_cubic = [t - t0 for t in t_cubic]

plt.figure(figsize=(10, 5))

# Plot CUBIC
if t_cubic:
    plt.plot(t_cubic, c_cubic, label="CUBIC CWND", color="red", linewidth=1.5, alpha=0.8)

# Plot the dynamic CC Algorithm
if t_cc:
    cc_display_name = cc_algo.upper()
    plt.plot(t_cc, c_cc, label=f"{cc_display_name} CWND", color="blue", linewidth=1.5, alpha=0.8)

plt.xlabel("Time (s)")
plt.ylabel("CWND (KB)")
plt.title(f"{cc_algo.upper()} vs CUBIC | BW: {bw} | RTT: {rtt}")
plt.grid(True)
plt.legend(loc="upper left")
plt.tight_layout()

plt.savefig(output_filename)
print(f"--> Saved comparison plot to {output_filename}\n")