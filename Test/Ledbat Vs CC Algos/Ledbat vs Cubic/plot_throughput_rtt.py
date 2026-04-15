import sys
import re
import os
import matplotlib.pyplot as plt

if len(sys.argv) < 4:
    print("Usage: python3 plot_throughput_rtt.py <CC> <BW> \"<rtt_list_in_ms>\" ")
    sys.exit(1)

test_cc = sys.argv[1]
bw_label = sys.argv[2]
rtt_list = [int(rtt) for rtt in sys.argv[3].split()]


test_cc_throughputs = []
cubic_throughputs = []
valid_rtts = []

ignore_first_sec=30

def extract_throughput(filename, ignore_first_sec):
    if not os.path.exists(filename):
        return None

    bitrates = []

    with open(filename, 'r') as f:
        for line in f:
            # Skip summary lines
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

                # Ignore startup phase
                if end_time <= ignore_first_sec:
                    continue
                # print("Endtime ",end_time," Ignore ",ignore_first_sec)

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
    # print(bitrates)

    return sum(bitrates) / len(bitrates)

print("Parsing iperf3 logs...")

for rtt in rtt_list:
    test_file = f"logs/throughput_{test_cc}_{rtt}ms.txt"
    cubic_file = f"logs/throughput_cubic_{rtt}ms.txt"

    test_tput = extract_throughput(test_file,ignore_first_sec)
    cubic_tput = extract_throughput(cubic_file,ignore_first_sec)

    if test_tput is not None and cubic_tput is not None:
        valid_rtts.append(rtt)
        test_cc_throughputs.append(test_tput)
        cubic_throughputs.append(cubic_tput)

        print(
            f"RTT: {rtt}ms | "
            f"{test_cc.upper()}: {test_tput} Mbit/s | "
            f"CUBIC: {cubic_tput} Mbit/s"
        )
    else:
        print(f"Warning: Missing logs for {rtt}ms. Skipping.")

plt.figure(figsize=(10, 6))

# Convert BW string like 20Mbit/s → 20
bw_value = float(re.findall(r'[\d.]+', bw_label)[0])

plt.plot(
    valid_rtts,
    cubic_throughputs,
    label='CUBIC',
    color='red',
    marker='s',
    linewidth=2,
    markersize=8
)

plt.plot(
    valid_rtts,
    test_cc_throughputs,
    label=test_cc.upper(),
    color='blue',
    marker='o',
    linewidth=2,
    markersize=8
)

plt.xlabel('Round Trip Time (ms)', fontweight='bold', fontsize=12)
plt.ylabel('Mean Throughput (Mbit/s)', fontweight='bold', fontsize=12)
plt.title(
    f'Mean Throughput vs RTT ({test_cc.upper()} vs CUBIC, Bottleneck: {bw_label})',
    fontsize=14,
    fontweight='bold'
)

plt.ylim(0, bw_value + 2)

plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(loc='lower right', fontsize=12)
plt.tight_layout()

output_file = f"throughput_vs_rtt_{test_cc}.png"
plt.savefig(output_file, dpi=300)

print(f"\nSuccess! Graph saved as {output_file}")