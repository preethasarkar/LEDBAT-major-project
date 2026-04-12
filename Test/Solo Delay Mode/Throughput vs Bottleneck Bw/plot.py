import sys
import re
import os
import matplotlib.pyplot as plt

if len(sys.argv) < 4:
    print("Usage: python3 plot.py <CC_ALGO> <RTT> \"<bw_list_in_mbps>\"")
    sys.exit(1)

cc_algo = sys.argv[1]
rtt_label = sys.argv[2]
bw_list = [int(bw) for bw in sys.argv[3].split()]

cc_throughputs = []
valid_bws = []

# --- CONFIGURATION ---
# How many seconds of the initial start up to ignore before averaging
IGNORE_FIRST_SEC = 100 

def extract_throughput(filename):
    """ Parses an iperf3 text file, skips the transient phase, and returns the average Mbit/s """
    if not os.path.exists(filename):
        return None
    
    bitrates = []
    
    with open(filename, 'r') as f:
        for line in f:
            # Skip the final summary lines so they don't skew our manual average
            if "receiver" in line or "sender" in line:
                continue
                
            # Regex to match the interval lines, e.g.:
            # [  5]  60.01-61.02  sec  1.88 MBytes  15.6 Mbits/sec  0  114 KBytes
            match = re.search(r'([\d.]+)-([\d.]+)\s+sec\s+[\d.]+\s+[KMG]?Bytes\s+([\d.]+)\s+([KMG]?bits)/sec', line)
            
            if match:
                start_time = float(match.group(1))
                end_time = float(match.group(2))
                value = float(match.group(3))
                unit = match.group(4)
                
                # Only collect data AFTER our threshold
                if end_time > IGNORE_FIRST_SEC:
                    if unit == "Kbits": value_mbps = value / 1000.0
                    elif unit == "Mbits": value_mbps = value
                    elif unit == "Gbits": value_mbps = value * 1000.0
                    elif unit == "bits": value_mbps = value / 1000000.0
                    else: value_mbps = value
                        
                    bitrates.append(value_mbps)
                    
    # Calculate the mean of the valid intervals
    if len(bitrates) > 0:
        return sum(bitrates) / len(bitrates)
    
    return 0.0

print(f"Parsing iperf3 logs for {cc_algo} at RTT = {rtt_label}... (Ignoring first {IGNORE_FIRST_SEC}s)")

# Extract data for each Bandwidth
for bw in bw_list:
    # Look for the log file dynamically based on the algorithm and RTT
    l_file = f"logs/throughput_{cc_algo}_{rtt_label}_{bw}Mbps.txt"
    
    l_tput = extract_throughput(l_file)
    
    if l_tput is not None and l_tput > 0:
        valid_bws.append(bw)
        cc_throughputs.append(l_tput)
        print(f"BW: {bw} Mbit/s | {cc_algo.upper()} (Steady State): {l_tput:.2f} Mbit/s")
    else:
        print(f"Warning: Missing or invalid log {l_file}. Skipping.")

# Plotting the Graph
plt.figure(figsize=(10, 6))

# Plot the "True Max Goodput" as individual short horizontal dashes
added_label = False
for bw in valid_bws:
    max_goodput = bw * 0.9653
    # Draw a line from (bw - 1.5) to (bw + 1.5)
    plt.hlines(y=max_goodput, xmin=bw-1.5, xmax=bw+1.5, color='black', linestyle='--', alpha=0.8, linewidth=2, 
               label='Max application rate' if not added_label else "")
    added_label = True # Ensure the legend only gets one entry for these lines

# Plot Solo CC Algorithm
algo_display_name = cc_algo.upper()
plt.plot(valid_bws, cc_throughputs, label=f'{algo_display_name} Solo', color='blue', marker='o', linewidth=2, markersize=8)

# Formatting the graph
plt.xlabel('Bottleneck Bandwidth (Mbit/s)', fontweight='bold', fontsize=12)
plt.ylabel('Mean Throughput (Mbit/s)', fontweight='bold', fontsize=12)
plt.title(f'{algo_display_name} Throughput vs Bottleneck (RTT: {rtt_label})', fontsize=14, fontweight='bold')

plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(loc='upper left', fontsize=12)
plt.tight_layout()

# Save a unique file for each Algorithm and RTT
output_file = f"throughput_{cc_algo}_vs_bw_{rtt_label}.png"
plt.savefig(output_file, dpi=300)
print(f"\nSuccess! Graph saved as {output_file}")