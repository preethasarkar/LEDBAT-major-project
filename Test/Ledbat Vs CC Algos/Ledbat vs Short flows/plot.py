import sys
import re
import os
import matplotlib.pyplot as plt
import numpy as np

if len(sys.argv) < 2:
    print("Usage: python3 plot.py \"<rtt_list>\"")
    sys.exit(1)

# Convert string "20ms 100ms 200ms 300ms" to a list
rtt_list = sys.argv[1].split()

def extract_fct(filename):
    """ Extracts the total transfer time (Flow Completion Time) from iperf3 log """
    if not os.path.exists(filename):
        return 0.0
    
    with open(filename, 'r') as f:
        content = f.read()
        # Look for the receiver summary line to find total time
        # Example: [  5]  0.00- 4.32  sec  10.0 MBytes  19.4 Mbits/sec  receiver
        match = re.search(r'0\.00-\s*([\d.]+)\s*sec.*receiver', content)
        if match:
            return float(match.group(1))
    return 0.0

print("Parsing Flow Completion Times...")

fct_solo = []
fct_ledbat = []
fct_cubic = []

for rtt in rtt_list:
    t_solo = extract_fct(f"logs/fct_solo_{rtt}.txt")
    t_ledbat = extract_fct(f"logs/fct_ledbat_{rtt}.txt")
    t_cubic = extract_fct(f"logs/fct_cubic_{rtt}.txt")
    
    fct_solo.append(t_solo)
    fct_ledbat.append(t_ledbat)
    fct_cubic.append(t_cubic)
    
    print(f"RTT: {rtt} | Solo: {t_solo}s | vs LEDBAT: {t_ledbat}s | vs CUBIC: {t_cubic}s")

# Plotting the Grouped Bar Chart
x = np.arange(len(rtt_list))  # Label locations
width = 0.25  # Width of the bars

fig, ax = plt.subplots(figsize=(10, 6))

# Create the bars side-by-side
rects1 = ax.bar(x - width, fct_solo, width, label='Solo Short Flow', color='green', alpha=0.8)
rects2 = ax.bar(x, fct_ledbat, width, label='vs Background LEDBAT', color='blue', alpha=0.8)
rects3 = ax.bar(x + width, fct_cubic, width, label='vs Background CUBIC', color='red', alpha=0.8)

# Add some text for labels, title and custom x-axis tick labels, etc.
ax.set_ylabel('Flow Completion Time (Seconds)', fontweight='bold', fontsize=12)
ax.set_xlabel('Network Latency (RTT)', fontweight='bold', fontsize=12)
ax.set_title('Short Flow Completion Time (10MB Transfer at 20Mbps)', fontsize=14, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(rtt_list, fontsize=12)
ax.legend(loc='upper left', fontsize=11)

# Add a grid strictly on the Y-axis to make FCT readable
ax.grid(axis='y', linestyle='--', alpha=0.7)

plt.tight_layout()

# Save the grouped graph
output_file = "fct_comparison_bar_chart.png"
plt.savefig(output_file, dpi=300)
print(f"\nSuccess! Grouped bar chart saved as {output_file}")