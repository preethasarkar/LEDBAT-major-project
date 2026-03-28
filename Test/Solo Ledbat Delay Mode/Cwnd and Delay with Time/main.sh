#!/bin/sh

# Make sure scripts are executable
chmod +x run_experiment.sh

# Function to run an experiment and plot the results
run_and_plot() {
    BW=$1
    RTT=$2
    CC=$3
    
    echo "================================================="
    echo " STARTING RUN: Bandwidth = $BW, RTT = $RTT", CC=$CC
    echo "================================================="
    
    # 1. Run the experiment
    ./run_experiment.sh "$BW" "$RTT" "$CC"
    
    # 2. Generate the stacked graph image
    python3 plot.py "$BW" "$RTT" "$CC"
}

# Run 1
run_and_plot "20Mbit/s" "50ms" "ledbat"

# Run 2
run_and_plot "20Mbit/s" "150ms" "ledbat"
 
# Run 3 (Added to make 3 total runs)
run_and_plot "20Mbit/s" "300ms" "ledbat"

echo "================================================="
echo "All experiments finished! ."