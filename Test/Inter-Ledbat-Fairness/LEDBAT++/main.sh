#!/bin/sh

# =====================================================================
# EXPERIMENT CONFIGURATION
# =====================================================================
BW="20Mbit/s"
RTT_BASE="20ms"
BUFFER_MS=500
NUM_FLOWS=4
INTERVAL=30
TOTAL_TIME=500
CC="ledbatpp"

GRAPH_FILE="figure25_cwnd_convergence.png"

echo "========================================================="
echo " STARTING LEDBAT++ LATE-COMER EXPERIMENT (SYSLOG MODE) "
echo "========================================================="

# 1. Clean up old data to ensure a fresh run
# This ensures we don't plot old "zombie" data
sudo killall tail 2>/dev/null
chmod +x run_late_comer.sh
rm -f cwnd_flow_*.csv
rm -f full_trace.log
rm -f "$GRAPH_FILE"

# 2. Start Background Logging (Syslog Streaming)
# We use tail -F to follow the system messages file
echo "--> Clearing kernel buffer and starting disk logger..."
sudo dmesg -c > /dev/null
sudo truncate -s 0 /var/log/messages
tail -n 0 -F /var/log/messages > full_trace.log &
LOGGER_PID=$!

# 3. Execute the experiment
echo "--> Running flows for ${TOTAL_TIME} seconds..."
sudo ./run_late_comer.sh "$BW" "$RTT_BASE" "$BUFFER_MS" "$CC" "$NUM_FLOWS" "$INTERVAL" "$TOTAL_TIME"

# 4. Stop Logging
echo "--> Experiment done. Saving trace to disk..."
sleep 2                # Wait for final ACKs to be written to /var/log/messages
kill $LOGGER_PID       # Stop the background tail process

# =========================================================
# 5. SPLITTER LOGIC (With Prefix Stripping)
# =========================================================
echo "--> Cleaning and splitting full_trace.log into clean CSVs..."

# This loop extracts data for each port and removes the syslog header
for i in $(seq 1 $NUM_FLOWS); do
    PORT=$((5200 + i))
    
    # Grep only relevant lines, filter by port, then strip everything before the trace
    awk -F',' -v port="$PORT" '/LEDBATPP_TRACE/ && $NF == port { sub(/^.*kernel: LEDBATPP_TRACE,/, ""); print }' full_trace.log > "cwnd_flow_${i}.csv"
    
    if [ -s "cwnd_flow_${i}.csv" ]; then
        echo "   [Port $PORT] Extracted $(wc -l < "cwnd_flow_${i}.csv") clean lines."
    fi
done

echo "--> Data extraction complete."
# =========================================================

# 6. Success Check & Plotting
if ls cwnd_flow_*.csv >/dev/null 2>&1; then
    echo "---------------------------------------------------------"
    echo " SUCCESS: High-frequency data captured and cleaned."
    echo " Generating Convergence Graph..."
    
    python3 plot_multiflow.py --bw "$BW" --rtt "$RTT_BASE" --buffer "$BUFFER_MS"
    
    if [ -f "$GRAPH_FILE" ]; then
        echo " Graph saved as: $GRAPH_FILE"
    else
        echo " ERROR: Python plotter failed to generate the image."
    fi
else
    echo " ERROR: No CWND data extracted. Check if /var/log/messages is being updated."
fi

echo "========================================================="
echo " Experiment Suite Finished."