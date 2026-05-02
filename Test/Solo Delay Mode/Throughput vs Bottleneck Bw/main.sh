#!/bin/sh

CC_ALGO="ledbatpp" 
RTT_LIST="50ms"
# RTT_LIST="100ms"
BW_LIST="5 10 20 30 40"
DUR="150"

chmod +x ./run_experiment.sh

echo "================================================="
echo " STARTING SOLO $CC_ALGO THROUGHPUT VS BW ANALYSIS"
echo "================================================="

mkdir -p logs

for RTT in $RTT_LIST; do
    echo "\n================================================="
    echo " RUNNING EXPERIMENTS FOR RTT = $RTT"
    echo "================================================="

    # Pass the algorithm, RTT, Bandwidth list, and duration to the experiment runner
    ./run_experiment.sh "$CC_ALGO" "$RTT" "$BW_LIST" "$DUR"

    echo "\n>>> Generating Graph for RTT = $RTT..."
    # Pass the CC_ALGO to the Python script for accurate file targeting and graph labels
    python3 plot.py "$CC_ALGO" "$RTT" "$BW_LIST"
done

echo "\n================================================="
echo "All experiments finished! 3 graphs generated."
echo "================================================="