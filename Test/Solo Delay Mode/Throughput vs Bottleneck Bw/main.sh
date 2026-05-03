#!/bin/sh

# 1. Define the list of algorithms you want to compare
# This matches the folder names: cc_ledbatpp and cc_ledbatpp_old
ALGO_LIST="ledbatpp ledbatpp_old" 

RTT_LIST="50ms"
BW_LIST="10"
DUR="150"

chmod +x ./run_experiment.sh

echo "================================================="
echo " STARTING LEDBAT++ COMPARISON: ADAPTIVE VS OLD"
echo "================================================="

mkdir -p logs

for RTT in $RTT_LIST; do
    echo "\n================================================="
    echo " RUNNING EXPERIMENTS FOR RTT = $RTT"
    echo "================================================="

    # 2. Outer loop to run the full suite for each version
    for CC_ALGO in $ALGO_LIST; do
        echo "\n>>> Testing Algorithm: $CC_ALGO"
        
        # This will call setup_experiment.sh inside, which will look for
        # the folder cc_ledbatpp or cc_ledbatpp_old automatically.
        ./run_experiment.sh "$CC_ALGO" "$RTT" "$BW_LIST" "$DUR"
    done

    echo "\n================================================="
    echo ">>> Generating Comparison Graph for RTT = $RTT..."
    echo "================================================="
    
    # 3. Call a comparison script instead of solo plot.py
    # This script should look for BOTH ledbatpp and ledbatpp_old logs
    python3 plot.py "$RTT" "$BW_LIST"
done

echo "\n================================================="
echo "All experiments finished! Comparison graphs generated."
echo "================================================="