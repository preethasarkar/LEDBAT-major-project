#!/bin/sh

chmod +x run_experiment.sh

run_and_plot() {
    CC=$1
    BW=$2
    RTT=$3
    CAPACITY=$4
    DUR=$5
    
    echo "================================================="
    echo " COMPETITION RUN: Bandwidth = $BW, RTT = $RTT"
    echo "================================================="
    
    # 1. Run the concurrent experiment
    ./run_experiment.sh "$CC" "$BW" "$RTT" "$CAPACITY" "$DUR"
    
    # 2. Plot the comparative graph
    python3 plot_compare.py "$CC" "$BW" "$RTT"
}

run_and_plot "ledbatpp" "20Mbit/s" "20ms" "200" "60"


echo "================================================="
echo "All competition experiments finished!"