import sys
import re
import os
import matplotlib.pyplot as plt

if len(sys.argv) < 3:
    print("Usage: python3 plot_comparison.py <RTT> \"<bw_list_in_mbps>\"")
    sys.exit(1)

rtt_label = sys.argv[1]
bw_list = [int(bw) for bw in sys.argv[2].split()]

# --- CONFIGURATION ---
# We now define both algorithms we want to compare
algos = ["ledbatpp", "ledbatpp_old"]
colors = {"ledbatpp": "blue", "ledbatpp_old": "red"}
labels = {"ledbatpp": "Adaptive LEDBAT++", "ledbatpp_old": "Original LEDBAT++"}

# How many seconds of the initial start up to ignore before averaging
IGNORE_FIRST_SEC = 100 

def extract_throughput(filename):
    """ Parses an iperf3 text file, skips the transient phase, and returns the average Mbit/s """
    if not os.path.exists(filename):
        return None
    
    bitrates = []
    
    with open(filename, 'r') as f:
        for line in f:
            if "receiver" in line or "sender" in line:
                continue
                
            # Regex to match the interval lines
            match = re.search(r'([\d.]+)-([\d.]+)\s+sec\s+[\d.]+\s+[KMG]?Bytes\s+([\d.]+)\s+([KMG]?bits)/sec', line)
            
            if match:
                start_time = float(match.group(1))
                end_time = float(match.group(2))
                value = float(match.group(3))
                unit = match.group(4)
                
                if end_time > IGNORE_FIRST_SEC:
                    if unit == "Kbits": value_mbps = value / 1000.0
                    elif unit == "Mbits": value_mbps = value
                    elif unit == "Gbits": value_mbps = value * 1000.0
                    elif unit == "bits": value_mbps = value / 1000000.0
                    else: value_mbps = value
                        
                    bitrates.append(value_mbps)
                    
    if len(bitrates) > 0:
        return sum(bitrates) / len(bitrates)
    
    return 0.0

# Plotting the Graph
plt.figure(figsize=(10, 7))

# 1. Process each algorithm
for algo in algos:
    cc_throughputs = []
    valid_bws = []
    
    print(f"\nProcessing logs for {algo}...")
    
    for bw in bw_list:
        l_file = f"logs/throughput_{algo}_{rtt_label}_{bw}Mbps.txt"
        l_tput = extract_throughput(l_file)
        
        if l_tput is not None:
            valid_bws.append(bw)
            cc_throughputs.append(l_tput)
            print(f"  BW: {bw} Mbit/s | Avg: {l_tput:.2f} Mbit/s")
        else:
            print(f"  Warning: Missing log {l_file}")

    # Plot this algorithm's line
    plt.plot(valid_bws, cc_throughputs, label=labels[algo], 
             color=colors[algo], marker='o', linewidth=2.5, markersize=8)

# 2. Plot the "Max Application Rate" Reference Lines
added_ref_label = False
for bw in bw_list:
    max_goodput = bw * 0.9653
    ninety_percent = 0.9 * max_goodput
    
    plt.hlines(y=max_goodput, xmin=bw-1.5, xmax=bw+1.5, color='black', 
               linestyle='--', alpha=0.8, linewidth=1,
               label='Max application rate' if not added_ref_label else "")
    plt.hlines(y=ninety_percent, xmin=bw-1.5, xmax=bw+1.5, color='gray', 
               linestyle=':', alpha=0.8, linewidth=1,
               label='90% Threshold' if not added_ref_label else "")
    added_ref_label = True

# 3. Formatting the graph
plt.xlabel('Bottleneck Bandwidth (Mbit/s)', fontweight='bold', fontsize=12)
plt.ylabel('Steady-State Mean Throughput (Mbit/s)', fontweight='bold', fontsize=12)
plt.title(f'Throughput Comparison: Adaptive vs Original (RTT: {rtt_label})', fontsize=14, fontweight='bold')

plt.grid(True, linestyle='--', alpha=0.5)
plt.legend(loc='upper left', fontsize=10)
plt.tight_layout()

# Save the comparison graph
output_file = f"comparison_throughput_vs_bw_{rtt_label}.png"
plt.savefig(output_file, dpi=300)
print(f"\nSuccess! Comparison graph saved as {output_file}")