import sys
import csv
import matplotlib.pyplot as plt

if len(sys.argv) < 3:
    print("Usage: python3 plot_competition.py <BW> <RTT>")
    sys.exit(1)

bw = sys.argv[1]
rtt = sys.argv[2]
output_filename = f"compare_{bw.replace('/', '_')}_{rtt}.png"

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

t_ledbat, c_ledbat = load_data("./logs/cwnd_ledbat.csv")
t_cubic, c_cubic = load_data("./logs/cwnd_cubic.csv")

if not t_ledbat and not t_cubic:
    print("No data found to plot.")
    sys.exit(1)

# Find the absolute starting time to sync both graphs
start_times = []
if t_ledbat: start_times.append(t_ledbat[0])
if t_cubic: start_times.append(t_cubic[0])
t0 = min(start_times)

# Normalize times
t_ledbat = [t - t0 for t in t_ledbat]
t_cubic = [t - t0 for t in t_cubic]

plt.figure(figsize=(10, 5))

if t_cubic:
    plt.plot(t_cubic, c_cubic, label="CUBIC CWND", color="red", linewidth=1.5, alpha=0.8)
if t_ledbat:
    plt.plot(t_ledbat, c_ledbat, label="LEDBAT CWND", color="blue", linewidth=1.5, alpha=0.8)

plt.xlabel("Time (s)")
plt.ylabel("CWND (KB)")
plt.title(f"LEDBAT vs CUBIC | BW: {bw} | RTT: {rtt}")
plt.grid(True)
plt.legend(loc="upper left")
plt.tight_layout()

plt.savefig(output_filename)
print(f"--> Saved comparison plot to {output_filename}\n")