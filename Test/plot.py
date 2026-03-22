import matplotlib.pyplot as plt
import os

TRACE_LOG = 'rledbat_trace.csv'
CUBICX_LOG = 'cubicx_trace.csv'

def parse_trace_logs(rledbat_file, cubicx_file):
    rledbat_raw_times, rledbat_delays, rledbat_cwnds = [], [], []
    cubicx_raw_times, cubicx_cwnds = [], []

    # 1. Parse RLEDBAT Data
    print(f"Parsing {rledbat_file}...")
    if os.path.exists(rledbat_file):
        with open(rledbat_file, 'r') as file:
            for line in file:
                row = line.strip().split(',')
                if len(row) < 3:
                    continue
                try:
                    t = float(row[0])
                    delay = int(row[1])
                    cwnd = int(row[2])

                    if cwnd > 10000000:
                        continue

                    rledbat_raw_times.append(t)
                    rledbat_delays.append(delay)
                    rledbat_cwnds.append(cwnd)

                except ValueError:
                    pass
    else:
        print(f"Warning: {rledbat_file} not found.")

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

                    if cwnd > 10000000:
                        continue

                    cubicx_raw_times.append(t)
                    cubicx_cwnds.append(cwnd)

                except ValueError:
                    pass
    else:
        print(f"Warning: {cubicx_file} not found.")

    # 3. Time Synchronization
    first_timestamps = []
    if rledbat_raw_times:
        first_timestamps.append(rledbat_raw_times[0])
    if cubicx_raw_times:
        first_timestamps.append(cubicx_raw_times[0])

    if not first_timestamps:
        return [], [], [], [], []

    t0 = min(first_timestamps)

    # Normalize times
    rledbat_times = [t - t0 for t in rledbat_raw_times]
    cubicx_times = [t - t0 for t in cubicx_raw_times]

    return rledbat_times, rledbat_delays, rledbat_cwnds, cubicx_times, cubicx_cwnds


def plot_graphs(r_time, r_delay, r_cwnd, c_time, c_cwnd):

    if not r_time and not c_time:
        print("No valid data to plot. Did you run the experiment?")
        return

    print("Plotting graphs...")

    # --- Graph 1: RLEDBAT vs CUBICX CWND ---
    plt.figure(figsize=(12, 6))

    if r_time:
        plt.plot(r_time, r_cwnd, color='tab:blue', alpha=0.8,
                 linewidth=1.5, label="RLEDBAT CWND")

    if c_time:
        plt.plot(c_time, c_cwnd, color='tab:red', alpha=0.8,
                 linewidth=1.5, label="CUBICX CWND")

    plt.xlabel('Time (Seconds)', fontweight='bold')
    plt.ylabel('Congestion Window (Bytes)', fontweight='bold')
    plt.title('Congestion Window over Time: RLEDBAT vs CUBICX', fontsize=14)

    plt.grid(True, linestyle='--', alpha=0.6)
    plt.legend()
    plt.tight_layout()

    cwnd_output = 'rledbat_vs_cubicx_cwnd.png'
    plt.savefig(cwnd_output, dpi=300)

    print(f"CWND comparison graph saved to {cwnd_output}")

    # --- Graph 2: RLEDBAT Delay over Time ---
    if r_time:

        plt.figure(figsize=(12, 6))

        plt.plot(r_time, r_delay,
                 color='tab:orange',
                 alpha=0.8,
                 linewidth=1.5,
                 label="RLEDBAT Queue Delay")

        plt.axhline(
            y=30,
            color='red',
            linestyle=':',
            linewidth=2,
            label="RLEDBAT Target (30 ms)"
        )

        plt.xlabel('Time (Seconds)', fontweight='bold')
        plt.ylabel('Queueing Delay (Milliseconds)',
                   color='tab:orange',
                   fontweight='bold')

        plt.title('RLEDBAT Internal View: Queueing Delay over Time', fontsize=14)

        plt.grid(True, linestyle='--', alpha=0.6)
        plt.legend()
        plt.tight_layout()

        delay_output = 'rledbat_delay_only.png'
        plt.savefig(delay_output, dpi=300)

        print(f"Delay graph saved to {delay_output}")


if __name__ == '__main__':

    rt, rd, rc, ct, cc = parse_trace_logs(TRACE_LOG, CUBICX_LOG)

    plot_graphs(rt, rd, rc, ct, cc)