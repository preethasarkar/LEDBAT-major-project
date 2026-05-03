import sys
import csv
import glob
import argparse
import matplotlib.pyplot as plt

def main():
    # 1. Setup Argument Parsing
    parser = argparse.ArgumentParser(description='Plot LEDBAT++ flows from kernel CSV logs.')
    parser.add_argument('--bw', default="20Mbit/s", help='Bandwidth for graph title')
    parser.add_argument('--rtt', default="20ms", help='Base RTT for graph title')
    parser.add_argument('--buffer', default="500", help='Buffer size in ms for graph title')
    args = parser.parse_args()

    # 2. Find all the CSV flow files
    csv_files = sorted(glob.glob("cwnd_flow_*.csv"))

    if not csv_files:
        print("Error: No cwnd_flow_*.csv files found.")
        sys.exit(1)

    flow_data = {}
    global_min_time = float('inf')

    # 3. Read Data
    for filename in csv_files:
        try:
            # Assumes filename format: cwnd_flow_1.csv
            flow_id = int(filename.split('_')[2].split('.')[0])
        except (IndexError, ValueError):
            continue

        raw_times = []
        cwnd_packets = []

        try:
            with open(filename, "r") as f:
                reader = csv.reader(f)
                for row in reader:
                    if len(row) < 5:
                        continue
                    try:
                        t = float(row[0])
                        c_bytes = float(row[2])
                        
                        # --- SANITY FILTER (Updated) ---
                        # 2,000,000 bytes is ~1.9MB. Perfect for a 20Mbps/500ms link.
                        if c_bytes > 2000000 or c_bytes < 0:
                            continue
                        
                        # MSS Divisor (1448 for standard Ethernet minus TCP headers)
                        c_packets = c_bytes / 1024.0
                        
                        raw_times.append(t)
                        cwnd_packets.append(c_packets)

                        if t < global_min_time:
                            global_min_time = t

                    except ValueError:
                        continue
        except FileNotFoundError:
            continue

        if raw_times:
            flow_data[flow_id] = {"time": raw_times, "cwnd": cwnd_packets}

    if global_min_time == float('inf'):
        print("Error: No valid data found.")
        sys.exit(1)

    # 4. Plotting
    plt.figure(figsize=(14, 8)) # Slightly wider for 150s timeline

    # Use a nice color cycle
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']

    for i, (flow_id, data) in enumerate(sorted(flow_data.items())):
        normalized_time = [t - global_min_time for t in data["time"]]
        
        # Thinner lines and slight alpha help see the "fairness" overlap
        plt.plot(normalized_time, data["cwnd"], 
                 label=f"Flow {flow_id} (Port {5200+flow_id})", 
                 linewidth=1.0, 
                 alpha=0.8,
                 color=colors[i % len(colors)])

    # 5. Graph Styling
    plt.title(f"LEDBAT++ Late-Comer Fairness & Convergence\n"
              f"Config: {args.bw} BW | {args.rtt} RTT | {args.buffer}ms Buffer", 
              fontsize=14, fontweight='bold', pad=20)
    
    plt.xlabel("Experiment Time (Seconds)", fontsize=12)
    plt.ylabel("Congestion Window (KiloBytes)", fontsize=12)
    plt.legend(loc='upper right', title="Active Flows", frameon=True, shadow=True)
    plt.grid(True, which='both', linestyle=':', alpha=0.5)
    
    # Ensure the 150s window is clearly visible
    plt.xlim(0, 510) 
    plt.ylim(bottom=0)
    plt.tight_layout()

    # 6. Save
    output_name = "figure25_cwnd_convergence.png"
    plt.savefig(output_name, dpi=300)
    print(f"--> Success! {output_name} generated with {len(flow_data)} flows.")

if __name__ == "__main__":
    main()