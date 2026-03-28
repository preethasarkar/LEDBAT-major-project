import sys
import csv
import matplotlib.pyplot as plt

# Get BW and RTT from command line arguments for the title and filename
if len(sys.argv) < 3:
    print("Usage: python3 plot_combined.py <BW> <RTT>")
    sys.exit(1)

bw = sys.argv[1]
rtt = sys.argv[2]
# Sanitize filename (replace / with _)
filename_bw = bw.replace("/", "_")
output_filename = f"ledbat_{filename_bw}_{rtt}.png"

time = []
delay = []
cwnd_kb = []

# Using 'with' is good practice; ensure your run_experiment.sh produces 'cwnd.csv'
try:
    with open("cwnd.csv", "r") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 3:
                continue
            try:
                # Based on your printf: LEDBATPP_TRACE,time,delay,cwnd
                t = float(row[0])
                d = float(row[1])             # Delay in ms
                c = float(row[2]) / 1024.0    # CWND in KB
                
                time.append(t)
                delay.append(d)
                cwnd_kb.append(c)
            except ValueError:
                continue
except FileNotFoundError:
    print("Error: cwnd.csv not found.")
    sys.exit(1)

if not time:
    print("No data found in cwnd.csv")
    sys.exit(1)

# Normalize time to start at 0
t0 = min(time)
time = [t - t0 for t in time]

# Create subplots
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

# --- Top Plot: CWND ---
ax1.plot(time, cwnd_kb, label=f"CWND ({cc})", color="blue", linewidth=1)
ax1.set_ylabel("CWND (KB)")
# Title now dynamically reflects the algorithm being tested
ax1.set_title(f"{cc.upper()} Behavior | BW: {bw} | RTT: {rtt}")
ax1.grid(True)
ax1.legend(loc="upper left")

# --- Bottom Plot: Queue Delay ---
ax2.plot(time, delay, label="Queue Delay", color="orange", linewidth=1)
ax2.axhline(y=75, color='red', linestyle='--', label='Target (75ms)')
ax2.set_xlabel("Time (s)")
ax2.set_ylabel("Delay (ms)")
ax2.grid(True)
ax2.legend(loc="upper left")

plt.tight_layout()
plt.savefig(output_filename)
print(f"--> Saved combined plot to {output_filename}\n")