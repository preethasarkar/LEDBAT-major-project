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

with open("cwnd.csv", "r") as f:
    reader = csv.reader(f)
    for row in reader:
        if len(row) < 3:
            continue
        try:
            t = float(row[0])
            d = float(row[1])            # Delay in ms
            c = float(row[2]) / 1024.0   # CWND in KB (from bytes)

            time.append(t)
            delay.append(d)
            cwnd_kb.append(c)
        except ValueError:
            continue

if not time:
    print("No data found in cwnd.csv")
    sys.exit(1)

# Normalize time to start at 0
t0 = time[0]
time = [t - t0 for t in time]

# Create a figure with 2 subplots (stacked vertically)
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

# --- Top Plot: CWND ---
ax1.plot(time, cwnd_kb, label="CWND", color="blue", linewidth=1)
ax1.set_ylabel("CWND (KB)")
ax1.set_title(f"LEDBAT Behavior | BW: {bw} | RTT: {rtt}")
ax1.grid(True)
ax1.legend(loc="upper left")

# --- Bottom Plot: Queue Delay ---
ax2.plot(time, delay, label="Queue Delay", color="orange", linewidth=1)
ax2.axhline(y=75, color='red', linestyle='--', label='Target (75ms)')
ax2.set_xlabel("Time (s)")
ax2.set_ylabel("Delay (ms)")
ax2.grid(True)
ax2.legend(loc="upper left")

# Adjust layout to prevent overlap and save the image
plt.tight_layout()
plt.savefig(output_filename)
print(f"--> Saved combined plot to {output_filename}\n")