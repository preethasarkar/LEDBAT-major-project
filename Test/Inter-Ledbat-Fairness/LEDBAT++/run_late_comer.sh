#!/bin/sh

# =====================================================================
# 1. POSITIONAL ARGUMENTS (Passed from main.sh)
# =====================================================================
BW=$1           # e.g., "20Mbit/s"
RTT_BASE=$2      # e.g., "20ms"
BUFFER_MS=$3    # e.g., 500
CC=$4           # e.g., "ledbatpp"
NUM_FLOWS=$5    # e.g., 4
INTERVAL=$6     # e.g., 10
TOTAL_TIME=$7   # e.g., 150

# --- Math: Convert Buffer Time to Packet Slots ---
# Formula: (Bandwidth * Buffer_Duration) / (Bits_per_Byte * Packet_Size)
BW_NUM=$(echo $BW | sed 's/[^0-9]//g')
SIZE=$(( (BW_NUM * 1000000 / 8) * BUFFER_MS / 1000 / 1500 ))

# --- Math: Convert RTT to One-Way Delay (OWD) ---
RTT_NUM=$(echo $RTT_BASE | sed 's/[^0-9]//g')
OWD="$((RTT_NUM / 2))ms"

echo "--- Calculated Config: Queue Size = $SIZE slots, OWD = $OWD ---"

# =====================================================================
# 2. INFRASTRUCTURE SETUP
# =====================================================================
# Clean up any lingering processes
killall iperf3 2>/dev/null
sleep 2

# Generate a comma-separated list of the CC algorithm (e.g., ledbatpp,ledbatpp...)
CC_LIST=$(printf "$CC,%.0s" $(seq 1 $NUM_FLOWS) | sed 's/,$//')

cd ../../
./setup_experiment.sh "$NUM_FLOWS" "$CC_LIST" "$BW" "$SIZE" "$OWD" || exit 1
cd - > /dev/null

# =====================================================================
# 3. START IPERF3 SERVERS
# =====================================================================
echo "--- Starting $NUM_FLOWS iperf3 Servers ---"
# Open firewall to allow communication between host and jails
sudo ipfw add 100 allow ip from any to any

# Start a server for EACH port (5201, 5202, 5203, 5204)
for i in $(seq 1 $NUM_FLOWS); do
    PORT=$((5200 + i))
    iperf3 -s -p $PORT -D
done
sleep 2

# =====================================================================
# 4. LAUNCH STAGGERED FLOWS
# =====================================================================
echo "--- Launching $NUM_FLOWS Staggered Flows ---"
# CRITICAL: We do not clear dmesg here; main.sh is already recording to disk.

for i in $(seq 1 $NUM_FLOWS); do
    CLIENT="client$i"
    PORT=$((5200 + i))
    # Flow 1 runs 150s, Flow 2 starts 10s later and runs 140s, etc.
    RUN_TIME=$((TOTAL_TIME - (i-1) * INTERVAL))
    
    echo "[Flow $i] Starting $CLIENT on Port $PORT for $RUN_TIME sec"
    
    # Prep jail network environment
    jexec $CLIENT ipfw -q flush
    jexec $CLIENT ipfw add 100 allow ip from any to any
    jexec $CLIENT ifconfig lo0 127.0.0.1/8 up
    
    # Start flow in background
    jexec $CLIENT iperf3 -c 10.0.0.1 -p $PORT -t $RUN_TIME -i 0.1 --json > "flow_$i.json" &
    
    # Stagger the next client start
    if [ $i -lt $NUM_FLOWS ]; then
        sleep $INTERVAL
    fi
done

echo "Waiting for all flows to complete their cycles..."
wait 

# =====================================================================
# EXPERIMENT COMPLETE
# =====================================================================
# Note: Log splitting and prefix stripping are now handled by main.sh
# to ensure high-frequency data is cleaned correctly from syslog.

echo "================================================="
echo "run_late_comer.sh is complete. Control returning to main.sh."