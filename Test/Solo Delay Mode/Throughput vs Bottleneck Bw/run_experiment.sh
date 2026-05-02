#!/bin/sh

CC_ALGO=$1
RTT=$2
BW_LIST=$3
DUR=$4

if [ -z "$CC_ALGO" ] || [ -z "$RTT" ] || [ -z "$BW_LIST" ] || [ -z "$DUR" ]; then
    echo "Usage: $0 <CC_ALGO> <RTT> <BW_LIST> <DURATION>"
    echo "Example: $0 ledbatpp 50ms \"5 10 20 30 40\" 60"
    exit 1
fi

# The Bandwidth loop is now handled entirely inside run_experiment.sh
for BW_VAL in $BW_LIST; do
    BW="${BW_VAL}Mbit/s"
    echo "\n>>> Running solo $CC_ALGO for Bottleneck = $BW, RTT = $RTT"

    # 500ms queue delay
    CAPACITY=$(( BW_VAL * 500000 / 12000 ))
    
    # Safety net: Don't let the queue drop below 20 packets on tiny links
    if [ "$CAPACITY" -lt 20 ]; then
        CAPACITY=20
    fi

    echo "--- 0. Running setup script ---"
    cd ../../
    # Pass the dynamic CC_ALGO variable to your setup script
    ./setup_experiment.sh 1 "$CC_ALGO" "$BW" "$CAPACITY" "$RTT" || exit 1
    cd - > /dev/null

    echo "--- 1. Pre-flight setup ---"
    killall iperf3 2>/dev/null

    # Bring up loopback for the single client
    jexec client1 ifconfig lo0 127.0.0.1/8 up

    echo "--- 2. Configure IPFW ---"
    jexec client1 ipfw -q flush
    jexec client1 ipfw add 65534 allow ip from any to any

    # Host-side safety rule
    ipfw add 1000 allow ip from any to any 2>/dev/null

    echo "--- 3. Connectivity test ---"
    jexec client1 ping -c 1 10.0.0.1 >/dev/null 2>&1 || { echo "ERROR: client1 failed"; exit 1; }
    echo "Connectivity OK for client1"

    echo "--- 4. Start experiment ---"
    dmesg -c > /dev/null

    # Start ONE iperf server on the host
    iperf3 -s -p 5201 -D

    # Increasing buffer size to 33MB
    sysctl kern.ipc.maxsockbuf=33554432

    echo "Running SOLO $CC_ALGO for $DUR seconds..."

    # Write log file directly with the dynamic CC_ALGO name
    jexec client1 iperf3 -c 10.0.0.1 -t $DUR -p 5201 > "./logs/throughput_${CC_ALGO}_${RTT}_${BW_VAL}Mbps.txt"

    echo "--- 5. Extract CWND logs ---"
    # Dynamically look for the correct trace tag (e.g., LEDBAT_TRACE or LEDBATPP_TRACE)
    TRACE_TAG=$(echo "$CC_ALGO" | tr '[:lower:]' '[:upper:]')_TRACE
    dmesg | grep "$TRACE_TAG" | awk -F "${TRACE_TAG}," '{print $2}' > "./logs/cwnd_${CC_ALGO}_${RTT}_${BW_VAL}Mbps.csv"

    echo "Run complete for Bottleneck = $BW"
done