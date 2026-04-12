#!/bin/sh

SCENARIO=$1
RTT=$2
BW_VAL=$3
SIZE=$4

BW="${BW_VAL}Mbit/s"
# 500ms queue delay based on the 20Mbps bandwidth
CAPACITY=$(( BW_VAL * 500000 / 12000 ))
if [ "$CAPACITY" -lt 20 ]; then CAPACITY=20; fi

echo "\n>>> Scenario: Short flow VS $SCENARIO | RTT: $RTT"

echo "--- 0. Setup ---"
cd ../../
# Determine setup based on scenario
if [ "$SCENARIO" = "solo" ]; then
    ./setup_experiment.sh 1 "cubic" "$BW" "$CAPACITY" "$RTT" || exit 1
elif [ "$SCENARIO" = "ledbat" ]; then
    ./setup_experiment.sh 2 "cubic,ledbat" "$BW" "$CAPACITY" "$RTT" || exit 1
elif [ "$SCENARIO" = "cubic" ]; then
    ./setup_experiment.sh 2 "cubic,cubic" "$BW" "$CAPACITY" "$RTT" || exit 1
fi
cd - > /dev/null

killall iperf3 2>/dev/null

# Configure loopbacks and IPFW for client1 (Short Flow)
jexec client1 ifconfig lo0 127.0.0.1/8 up
jexec client1 ipfw -q flush
jexec client1 ipfw add 65534 allow ip from any to any

# If there's a background flow, setup client2
if [ "$SCENARIO" != "solo" ]; then
    jexec client2 ifconfig lo0 127.0.0.1/8 up
    jexec client2 ipfw -q flush
    jexec client2 ipfw add 65534 allow ip from any to any
    
    if [ "$SCENARIO" = "ledbat" ]; then
        jexec client2 sysctl net.inet.tcp.cc.ledbat.target=10 2>/dev/null
    fi
fi

ipfw add 1000 allow ip from any to any 2>/dev/null
sysctl kern.ipc.maxsockbuf=33554432 >/dev/null

# Start iperf servers
iperf3 -s -p 5201 -D
if [ "$SCENARIO" != "solo" ]; then
    iperf3 -s -p 5202 -D
fi

LOG_FILE="./logs/fct_${SCENARIO}_${RTT}.txt"

# Run Experiment based on scenario
if [ "$SCENARIO" = "solo" ]; then
    echo "Running Solo Short Flow ($SIZE)..."
    jexec client1 iperf3 -c 10.0.0.1 -n $SIZE -p 5201 > "$LOG_FILE"
    
else
    echo "Starting Background $SCENARIO Flow..."
    # Start long flow in the background (-t 60)
    jexec client2 iperf3 -c 10.0.0.1 -t 60 -p 5202 > /dev/null &
    BG_PID=$!
    
    echo "Waiting 15s for background flow to hit steady state..."
    sleep 30
    
    echo "Running Short Flow ($SIZE)..."
    jexec client1 iperf3 -c 10.0.0.1 -n $SIZE -p 5201 > "$LOG_FILE"
    
    # Kill the background flow once the short flow completes
    kill $BG_PID 2>/dev/null
fi

echo "Done! Logs saved."