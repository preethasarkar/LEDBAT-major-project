#!/bin/sh

chmod +x ./run_experiment.sh

# --- Fixed Parameters ---
BW_VAL="20"            # 20 Mbit/s Bottleneck
RTT_LIST="300ms"
# RTT_LIST="100ms"
SHORT_FLOW_SIZE="20M"

echo "================================================="
echo " STARTING FLOW COMPLETION TIME (FCT) ANALYSIS "
echo "================================================="

mkdir -p logs

for RTT in $RTT_LIST; do
    echo "\n================================================="
    echo " RUNNING EXPERIMENTS FOR RTT = $RTT"
    echo "================================================="

    # 1. Short Flow Solo
    ./run_experiment.sh "solo" "$RTT" "$BW_VAL" "$SHORT_FLOW_SIZE"

    sleep 5
    # 2. Short Flow vs Long LEDBAT Flow
    ./run_experiment.sh "ledbat" "$RTT" "$BW_VAL" "$SHORT_FLOW_SIZE"

    sleep 5
    # 3. Short Flow vs Long CUBIC Flow
    ./run_experiment.sh "cubic" "$RTT" "$BW_VAL" "$SHORT_FLOW_SIZE"
done

echo "\n================================================="
echo "All experiments finished! Generating FCT Graph..."
echo "================================================="

# Pass the RTT list to the plotting script
python3 plot.py "$RTT_LIST"