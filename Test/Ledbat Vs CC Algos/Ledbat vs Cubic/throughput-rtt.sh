#!/bin/sh

BW_VAL="25"
BW_STRING="25Mbit/s"
CC="ledbat"

# List of RTTs to test (Numeric only)
RTT_LIST="10 20 50 100 150"

# Set a proper duration so the CC algorithms can stabilize
DUR="60"

echo "================================================="
echo " STARTING THROUGHPUT VS RTT ANALYSIS "
echo "================================================="

mkdir -p logs

for RTT_VAL in $RTT_LIST; do
    # Re-attach the "ms" string for the run script
    RTT="${RTT_VAL}ms"
    echo "\n>>> Running competition for BW = "$BW_STRING" | RTT = $RTT"
    
   #500ms queue delay
    CAPACITY=$(( BW_VAL * 500000 / 12000 ))
    
    # Pass the correctly formatted strings to your experiment script
    ./run_experiment.sh  "$CC" "$BW_STRING" "$RTT" "$CAPACITY" "$DUR"
    
    # Rename the throughput files so they aren't overwritten
    cp ./logs/throughput_${CC}.txt "logs/throughput_${CC}_${RTT_VAL}ms.txt"
    cp ./logs/throughput_cubic.txt "logs/throughput_cubic_${RTT_VAL}ms.txt"
    
    echo "Saved throughput logs for $RTT"
done

echo "\n================================================="
echo "All experiments finished! Generating Throughput Graph..."
echo "================================================="

# Pass the numeric list to your Python script
python3 plot_throughput_rtt.py "$CC" "$BW_STRING" "$RTT_LIST"