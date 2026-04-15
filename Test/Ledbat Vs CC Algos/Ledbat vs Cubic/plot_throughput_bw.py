import sys
import re
import os
import matplotlib.pyplot as plt

if len(sys.argv) < 4:
    print('Usage: python3 plot_throughput_bw.py <CC> <RTT> "<bw_list>"')
    sys.exit(1)

test_cc = sys.argv[1]
rtt_label = sys.argv[2]
bw_list = [int(bw) for bw in sys.argv[3].split()]

test_cc_throughputs = []
cubic_throughputs = []
valid_bws = []

ignore_first_sec = 15

def extract_throughput(filename, ignore_first_sec):
    if not os.path.exists(filename):
        return None

    bitrates = []

    with open(filename, 'r') as f:
        for line in f:
            # Ignore iperf summary lines
            if "receiver" in line or "sender" in line:
                continue

            match = re.search(
                r'([\d.]+)-([\d.]+)\s+sec\s+[\d.]+\s+[KMG]?Bytes\s+([\d.]+)\s+([KMG]?bits)/sec',
                line
            )

            if match:
                start_time = float(match.group(1))
                end_time = float(match.group(2))
                value = float(match.group(3))
                unit = match.group(4)

                # Skip startup period
                if end_time <= ignore_first_sec:
                    continue

                if unit == "Kbits":
                    value_mbps = value / 1000.0
                elif unit == "Mbits":
                    value_mbps = value
                elif unit == "Gbits":
                    value_mbps = value * 1000.0
                elif unit == "bits":
                    value_mbps = value / 1000000.0
                else:
                    value_mbps = value

                bitrates.append(value_mbps)

    if not bitrates:
        return 0.0

    return sum(bitrates) / len(bitrates)

print("Parsing iperf3 logs...")

for bw in bw_list:
    test_file = f"logs/throughput_{test_cc}_{bw}Mbps.txt"
    cubic_file = f"logs/throughput_cubic_{bw}Mbps.txt"

    test_tput = extract_throughput(test_file,ignore_first_sec)
    cubic_tput = extract_throughput(cubic_file,ignore_first_sec)

    if test_tput is not None and cubic_tput is not None:
        valid_bws.append(bw)
        test_cc_throughputs.append(test_tput)
        cubic_throughputs.append(cubic_tput)

        print(
            f"BW: {bw} Mbit/s | "
            f"{test_cc.upper()}: {test_tput} Mbit/s | "
            f"CUBIC: {cubic_tput} Mbit/s"
        )
    else:
        print(f"Warning: Missing logs for {bw} Mbps. Skipping.")

plt.figure(figsize=(10, 6))

# Max application rate = 96.5% of BW
max_app_rates = [0.965 * bw for bw in valid_bws]

# Max application rate = 96.5% of BW
max_app_rates = [0.965 * b for b in valid_bws]

# Create lists for the start and end points of each horizontal dash
xmins = [b - 1.5 for b in valid_bws]
xmaxs = [b + 1.5 for b in valid_bws]

plt.hlines(
    y=max_app_rates, 
    xmin=xmins, 
    xmax=xmaxs, 
    color='black', 
    linestyle='--', 
    alpha=0.8, 
    linewidth=2, 
    label='Max application rate'
)

plt.plot(
    valid_bws,
    cubic_throughputs,
    label='CUBIC',
    color='red',
    marker='s',
    linewidth=2,
    markersize=8
)

plt.plot(
    valid_bws,
    test_cc_throughputs,
    label=test_cc.upper(),
    color='blue',
    marker='o',
    linewidth=2,
    markersize=8
)

plt.xlabel('Bottleneck Bandwidth (Mbit/s)', fontweight='bold', fontsize=12)
plt.ylabel('Mean Throughput (Mbit/s)', fontweight='bold', fontsize=12)
plt.title(
    f'Mean Throughput vs Bottleneck Bandwidth ({test_cc.upper()} vs CUBIC, RTT: {rtt_label})',
    fontsize=14,
    fontweight='bold'
)

plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(loc='upper left', fontsize=12)
plt.tight_layout()

output_file = f"throughput_vs_bw_{test_cc}.png"
plt.savefig(output_file, dpi=300)

print(f"\nSuccess! Graph saved as {output_file}")