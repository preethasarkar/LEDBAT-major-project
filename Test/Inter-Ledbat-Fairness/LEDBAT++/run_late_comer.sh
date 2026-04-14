#!/bin/sh

# =====================================================================
# POSITIONAL ARGUMENTS (Passed from main.sh)
# =====================================================================
BW=$1           # e.g., "20Mbit/s"
RTT_BASE=$2      # e.g., "20ms"
BUFFER_MS=$3    # e.g., 500
CC=$4           # e.g., "ledbatpp"
NUM_FLOWS=$5    # e.g., 4
INTERVAL=$6     # e.g., 10
TOTAL_TIME=$7   # e.g., 150

# --- 1. Math: Convert Buffer Time to Packet Slots ---
# Formula: (Bandwidth * Buffer_Time) / (Packet_Size * 8)
# For 20Mbps and 500ms: (20,000,000 * 0.5) / (1500 * 8) = 833
BW_NUM=$(echo $BW | sed 's/[^0-9]//g')
# We multiply by 1,000,000 for 'M' bit/s. 
SIZE=$(( (BW_NUM * 1000000 / 8) * BUFFER_MS / 1000 / 1500 ))

# --- 2. Math: Convert RTT to One-Way Delay (OWD) ---
RTT_NUM=$(echo $RTT_BASE | sed 's/[^0-9]//g')
OWD="$((RTT_NUM / 2))ms"

echo "--- Calculated Config: Queue Size = $SIZE slots, OWD = $OWD ---"

# --- 3. Pre-flight Cleanup ---
killall iperf3 2>/dev/null
sleep 2

# --- 4. Infrastructure Setup ---
# Create the CC list for setup_experiment (e.g., "ledbatpp,ledbatpp...")
CC_LIST=$(printf "$CC,%.0s" $(seq 1 $NUM_FLOWS) | sed 's/,$//')

cd ../../
./setup_experiment.sh "$NUM_FLOWS" "$CC_LIST" "$BW" "$SIZE" "$OWD" || exit 1
cd - > /dev/null

# --- 5. Start iperf3 Server ---
# Flush host firewall to ensure no blocks
sudo ipfw -q flush
# Allow all traffic on the bridge and jails
sudo ipfw add 100 allow ip from any to any

# Start the server on the host (10.0.0.1)
# We use 'killall' first to make sure no old servers are stuck
killall iperf3 2>/dev/null
iperf3 -s -p 5201 -D
sleep 2

# --- 6. Launch Staggered Flows ---
# Clear kernel logs so we only get data from THIS run
dmesg -c > /dev/null

for i in $(seq 1 $NUM_FLOWS); do
    CLIENT="client$i"
    RUN_TIME=$((TOTAL_TIME - (i-1) * INTERVAL))
    
    echo "[Flow $i] Preparing $CLIENT and starting flow..."
    
    # 1. Open the internal firewall of the jail (CRITICAL)
    jexec $CLIENT ipfw -q flush
    jexec $CLIENT ipfw add 100 allow ip from any to any
    
    # 2. Ensure the loopback interface is up inside the jail
    jexec $CLIENT ifconfig lo0 127.0.0.1/8 up
    
    # 3. Start the flow
    jexec $CLIENT iperf3 -c 10.0.0.1 -t $RUN_TIME -i 0.1 --json > "flow_$i.json" &
    
    if [ $i -lt $NUM_FLOWS ]; then
        sleep $INTERVAL
    fi
done

echo "Waiting for all flows to complete..."
wait 

# --- 7. Create the Log File ---
# This matches the LOG_FILE name in main.sh
TRACE_TAG="$(echo "$CC" | tr '[:lower:]' '[:upper:]')_TRACE"
echo "Extracting $TRACE_TAG logs from dmesg..."

dmesg | grep "^${TRACE_TAG}" > "late_comer_results.csv"

# Check if file is empty
if [ ! -s "late_comer_results.csv" ]; then
    echo "Warning: late_comer_results.csv is empty. Is your kernel module tracing?"
fi