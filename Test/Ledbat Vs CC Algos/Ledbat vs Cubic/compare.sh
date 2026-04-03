#!/bin/sh

chmod +x run_experiment.sh

run_and_plot() {
    BW=$1
    RTT=$2
    CAPACITY=$3
    
    echo "================================================="
    echo " COMPETITION RUN: Bandwidth = $BW, RTT = $RTT"
    echo "================================================="
    
    # 1. Run the concurrent experiment
    ./run_experiment.sh "$BW" "$RTT" "$CAPACITY"
    
    # 2. Plot the comparative graph
    python3 plot_compare.py "$BW" "$RTT"
}

run_and_plot "20Mbit/s" "20ms" "200" "60"


echo "================================================="
echo "All competition experiments finished!"