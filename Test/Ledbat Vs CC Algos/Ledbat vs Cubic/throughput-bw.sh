#!/bin/sh

# --- Fixed Parameters ---
RTT="20ms"
DUR="60"
CC="ledbatpp"
# List of Bandwidths to test (Numeric values in Mbit/s)
BW_LIST="5 10 20 30 40"

echo "================================================="
echo " STARTING THROUGHPUT VS BANDWIDTH ANALYSIS "
echo "================================================="

mkdir -p logs

for BW_VAL in $BW_LIST; do
    BW="${BW_VAL}Mbit/s"
    echo "\n>>> Running competition for Bottleneck = $BW"

   #500ms queue delay
    CAPACITY=$(( BW_VAL * 500000 / 12000 ))
    
    # Safety net: Don't let the queue drop below 20 packets on tiny links
    if [ "$CAPACITY" -lt 20 ]; then
        CAPACITY=20
    fi
    ./run_experiment.sh "$CC" "$BW" "$RTT" "$CAPACITY" "$DUR"
    
    # 2. Rename and save the logs with the BW value
    cp logs/throughput_${CC}.txt "logs/throughput_${CC}_${BW_VAL}Mbps.txt"
    cp logs/throughput_cubic.txt "logs/throughput_cubic_${BW_VAL}Mbps.txt"
    
    echo "Saved throughput logs for $BW in ./logs/"
done

echo "\n================================================="
echo "All experiments finished! Generating Bandwidth Graph..."
echo "================================================="

# 3. Call the Python script
python3 plot_throughput_bw.py "$CC" "$RTT" "$BW_LIST"