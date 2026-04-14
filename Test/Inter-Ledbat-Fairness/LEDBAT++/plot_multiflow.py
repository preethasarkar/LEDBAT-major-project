import json
import matplotlib.pyplot as plt
import argparse
import glob
import sys
import os

def main():
    # 1. Setup Argument Parsing to catch variables from main.sh
    parser = argparse.ArgumentParser(description='Plot LEDBAT++ flows from iperf3 JSON files.')
    parser.add_argument('--bw', default="20Mbit/s", help='Bandwidth for graph title')
    parser.add_argument('--rtt', default="20ms", help='Base RTT for graph title')
    parser.add_argument('--buffer', default="500", help='Buffer size in ms for graph title')
    args = parser.parse_args()

    # 2. Automatically find all JSON flow files
    # This looks for flow_1.json, flow_2.json, etc.
    json_files = sorted(glob.glob("flow_*.json"))

    if not json_files:
        print("Error: No flow_*.json files found. Did the iperf3 commands run successfully?")
        sys.exit(1)

    plt.figure(figsize=(12, 7))

    # 3. Loop through each file and add a line to the graph
    for filename in json_files:
        # Extract Flow ID from filename (e.g., flow_2.json -> 2)
        try:
            flow_id_str = filename.split('_')[1].split('.')[0]
            flow_id = int(flow_id_str)
        except (IndexError, ValueError):
            print(f"Warning: Could not determine Flow ID from {filename}. Skipping.")
            continue

        with open(filename, 'r') as f:
            try:
                data = json.load(f)
                
                # Basic error check for the JSON content
                if 'intervals' not in data:
                    print(f"Warning: No interval data in {filename}. Check iperf3 logs.")
                    continue
                
                intervals = data['intervals']
                
                # Extract Start Time and CWND
                # Note: snd_cwnd is in bytes, we divide by 1500 to get 'Packets'
                times = [i['streams'][0]['start'] for i in intervals]
                cwnd = [i['streams'][0]['snd_cwnd'] / 1500 for i in intervals]
                
                # 4. Apply the Staggered Offset
                # Since each iperf3 client thinks it starts at 0, we shift them
                # by 10s intervals (Flow 1 starts at 0, Flow 2 at 10, etc.)
                offset = (flow_id - 1) * 10
                global_times = [t + offset for t in times]
                
                # Draw the line for this specific flow
                plt.plot(global_times, cwnd, label=f"Flow {flow_id}", linewidth=1.5)

            except (json.JSONDecodeError, KeyError, IndexError) as e:
                print(f"Warning: Failed to parse {filename}: {e}")

    # 5. Graph Styling for Figure 25 Replication
    plt.title(f"LEDBAT++ Late-Comer Analysis (Replication)\n"
              f"BW: {args.bw} | RTT: {args.rtt} | Buffer: {args.buffer}ms", 
              fontsize=14, fontweight='bold', pad=15)
    
    plt.xlabel("Global Experiment Time (seconds)", fontsize=12)
    plt.ylabel("Congestion Window (Packets)", fontsize=12)
    
    plt.legend(loc='upper right', title="Active Flows", shadow=True)
    plt.grid(True, which='both', linestyle='--', alpha=0.6)
    
    # Ensure graph starts at zero
    plt.xlim(left=0)
    plt.ylim(bottom=0)
    
    plt.tight_layout()

    # 6. Save the final image
    output_name = "figure25_comparison.png"
    plt.savefig(output_name, dpi=300)
    print(f"Successfully generated graph: {output_name}")

if __name__ == "__main__":
    main()