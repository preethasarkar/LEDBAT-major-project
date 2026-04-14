#!/bin/sh

# =====================================================================
# EXPERIMENT CONFIGURATION
# =====================================================================
# Network Environment
BW="20Mbit/s"       # Total Link Bandwidth
RTT_BASE="20ms"     # The "floor" RTT from the paper
BUFFER_MS=500       # The buffer size in milliseconds (large buffer for LEDBAT++)

# Experiment Timing
NUM_FLOWS=4         # Total number of competing LEDBAT++ flows
INTERVAL=10         # Seconds to wait between starting each flow (Staggered start)
TOTAL_TIME=150      # Total duration for the first flow (all flows end at 150s)

# Protocol
CC="ledbatpp"       # Name of your kernel module

# File Outputs
GRAPH_FILE="figure25_comparison.png"

echo "========================================================="
echo " STARTING LEDBAT++ LATE-COMER EXPERIMENT "
echo " Parameters: BW=$BW, RTT=$RTT_BASE, Buffer=${BUFFER_MS}ms"
echo "========================================================="

# 1. Ensure scripts are executable
chmod +x run_late_comer.sh

# 2. Execute the experiment
# We pass the variables defined above to the runner script
sudo ./run_late_comer.sh "$BW" "$RTT_BASE" "$BUFFER_MS" "$CC" "$NUM_FLOWS" "$INTERVAL" "$TOTAL_TIME"

# 3. SUCCESS CHECK: Look for the JSON files instead of the old CSV
# This checks if at least one flow_*.json file was created and is not empty
if ls flow_*.json >/dev/null 2>&1; then
    echo "---------------------------------------------------------"
    echo " SUCCESS: JSON data collected for flows."
    echo " Generating Figure 25 Graph..."
    
    # 4. Run the Python Plotter
    # This script will search for all flow_*.json files automatically
    python3 plot_multiflow.py --bw "$BW" --rtt "$RTT_BASE" --buffer "$BUFFER_MS"
    
    if [ -f "$GRAPH_FILE" ]; then
        echo " Graph saved as: $GRAPH_FILE"
    else
        echo " ERROR: Python script failed to generate the graph file."
    fi
else
    echo " ERROR: No JSON data found. Check if iperf3 is correctly installed in your jails"
    echo " and if the IP addresses (10.0.0.1) are reachable."
fi

echo "========================================================="
echo " Experiment Suite Finished."