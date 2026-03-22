import matplotlib.pyplot as plt
import os

LEDBAT_LOG = 'ledbat_trace.csv'
CUBICX_LOG = 'cubicx_trace.csv'

def parse_trace_logs(ledbat_file, cubicx_file):
    ledbat_raw_times, ledbat_delays, ledbat_cwnds = [], [], []
    cubicx_raw_times, cubicx_cwnds = [], []

    # 1. Parse LEDBAT Data
    print(f"Parsing {ledbat_file}...")
    if os.path.exists(ledbat_file):
        with open(ledbat_file, 'r') as file:
            for line in file:
                row = line.strip().split(',')
                if len(row) < 3:
                    continue
                try:
                    t = float(row[0])
                    delay = int(row[1])
                    cwnd = int(row[2])
                    
                    if cwnd > 10000000: continue
                    
                    ledbat_raw_times.append(t)
                    ledbat_delays.append(delay)
                    ledbat_cwnds.append(cwnd)
                except ValueError:
                    pass
    else:
        print(f"Warning: {ledbat_file} not found.")

    # 2. Parse CUBICX Data
    print(f"Parsing {cubicx_file}...")
    if os.path.exists(cubicx_file):
        with open(cubicx_file, 'r') as file:
            for line in file:
                row = line.strip().split(',')
                if len(row) < 2:
                    continue
                try:
                    t = float(row[0])
                    cwnd = int(row[1])
                    
                    if cwnd > 10000000: continue
                    
                    cubicx_raw_times.append(t)
                    cubicx_cwnds.append(cwnd)
                except ValueError:
                    pass
    else:
        print(f"Warning: {cubicx_file} not found.")

    # 3. Time Synchronization
    # Find the absolute earliest timestamp across both files so they share T=0
    first_timestamps = []
    if ledbat_raw_times: first_timestamps.append(ledbat_raw_times[0])
    if cubicx_raw_times: first_timestamps.append(cubicx_raw_times[0])
    
    if not first_timestamps:
        return [], [], [], [], []
        
    t0 = min(first_timestamps)

    # Normalize all times to start at 0
    ledbat_times = [t - t0 for t in ledbat_raw_times]
    cubicx_times = [t - t0 for t in cubicx_raw_times]

    return ledbat_times, ledbat_delays, ledbat_cwnds, cubicx_times, cubicx_cwnds

def plot_graphs(l_time, l_delay, l_cwnd, c_time, c_cwnd):
    if not l_time and not c_time:
        print("No valid data to plot. Did you run the experiment?")
        return

    print("Plotting graphs...")

    # --- Graph 1: LEDBAT vs CUBICX CWND ---
    plt.figure(figsize=(12, 6))
    
    if l_time:
        plt.plot(l_time, l_cwnd, color='tab:blue', alpha=0.8, linewidth=1.5, label="LEDBAT CWND")
    if c_time:
        plt.plot(c_time, c_cwnd, color='tab:red', alpha=0.8, linewidth=1.5, label="CUBIC CWND")
    
    plt.xlabel('Time (Seconds)', fontweight='bold')
    plt.ylabel('Congestion Window (Bytes)', fontweight='bold')
    plt.title('Congestion Window over Time: LEDBAT vs CUBICX', fontsize=14)
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.legend()
    plt.tight_layout()
    
    cwnd_output = 'ledbat_vs_cubicx_cwnd.png'
    plt.savefig(cwnd_output, dpi=300)
    print(f"CWND vs CWND graph successfully generated and saved to {cwnd_output}")

    # --- Graph 2: LEDBAT Delay over Time ---
    if l_time:
        plt.figure(figsize=(12, 6))
        plt.plot(l_time, l_delay, color='tab:orange', alpha=0.8, linewidth=1.5, label="LEDBAT Delay")
        
        # Add a horizontal line to show where your 30ms target is
        plt.axhline(y=30, color='red', linestyle=':', linewidth=2, label="LEDBAT Target (30ms)")
        
        plt.xlabel('Time (Seconds)', fontweight='bold')
        plt.ylabel('Queueing Delay (Milliseconds)', color='tab:orange', fontweight='bold')
        plt.title('LEDBAT Internal View: Queueing Delay over Time', fontsize=14)
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.legend()
        plt.tight_layout()
        
        delay_output = 'ledbat_delay_only.png'
        plt.savefig(delay_output, dpi=300)
        print(f"Delay graph successfully generated and saved to {delay_output}")

if __name__ == '__main__':
    lt, ld, lc, ct, cc = parse_trace_logs(LEDBAT_LOG, CUBICX_LOG)
    plot_graphs(lt, ld, lc, ct, cc)