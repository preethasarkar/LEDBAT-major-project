import sys
import re
import os
import matplotlib.pyplot as plt

if len(sys.argv) < 3:
    print("Usage: python3 plot_throughput_bw.py <RTT> \"<bw_list_in_mbps>\"")
    sys.exit(1)

rtt_label = sys.argv[1]
# Convert the string "10 20 50..." into a list of integers
bw_list = [int(bw) for bw in sys.argv[2].split()]

ledbat_throughputs = []
cubic_throughputs = []
valid_bws = []

def extract_throughput(filename):
    """ Parses an iperf3 text file and returns the receiver bitrate in Mbit/s """
    if not os.path.exists(filename):
        return None
    
    with open(filename, 'r') as f:
        for line in f:
            if "receiver" in line:
                match = re.search(r'([\d.]+)\s+([KMG]?bits)/sec', line)
                if match:
                    value = float(match.group(1))
                    unit = match.group(2)
                    
                    if unit == "Kbits": return value / 1000.0
                    elif unit == "Mbits": return value
                    elif unit == "Gbits": return value * 1000.0
                    elif unit == "bits": return value / 1000000.0
    return 0.0

print("Parsing iperf3 logs...")

# Extract data for each Bandwidth
for bw in bw_list:
    l_file = f"logs/throughput_ledbat_{bw}Mbps.txt"
    c_file = f"logs/throughput_cubic_{bw}Mbps.txt"
    
    l_tput = extract_throughput(l_file)
    c_tput = extract_throughput(c_file)
    
    if l_tput is not None and c_tput is not None:
        valid_bws.append(bw)
        ledbat_throughputs.append(l_tput)
        cubic_throughputs.append(c_tput)
        print(f"BW: {bw} Mbit/s | LEDBAT: {l_tput} Mbit/s | CUBIC: {c_tput} Mbit/s")
    else:
        print(f"Warning: Missing logs for {bw} Mbps. Skipping.")

# Plotting the Graph
plt.figure(figsize=(10, 6))

# Plot the "Perfect Utilization" reference line
plt.plot(valid_bws, valid_bws, label='Theoretical Max Capacity', color='black', linestyle='--', alpha=0.6, linewidth=2)

# Plot the CC algorithms
plt.plot(valid_bws, cubic_throughputs, label='CUBIC', color='red', marker='s', linewidth=2, markersize=8)
plt.plot(valid_bws, ledbat_throughputs, label='LEDBAT', color='blue', marker='o', linewidth=2, markersize=8)

# Formatting the graph
plt.xlabel('Bottleneck Bandwidth (Mbit/s)', fontweight='bold', fontsize=12)
plt.ylabel('Mean Throughput (Mbit/s)', fontweight='bold', fontsize=12)
plt.title(f'Mean Throughput vs Bottleneck Bandwidth (RTT: {rtt_label})', fontsize=14, fontweight='bold')

plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(loc='upper left', fontsize=12)
plt.tight_layout()

output_file = "throughput_vs_bw.png"
plt.savefig(output_file, dpi=300)
print(f"\nSuccess! Graph saved as {output_file}")