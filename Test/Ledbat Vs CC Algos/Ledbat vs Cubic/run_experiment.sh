#!/bin/sh

BW=$1
RTT=$2
CAPACITY=$3
DUR=$4
if [ -z "$BW" ] || [ -z "$RTT" ] || [ -z "$CAPACITY" ] || [ -z "$DUR" ]; then
    echo "Usage: $0 <BW> <RTT> <CAPACITY> <DURATION>"
    echo "Example: $0 20Mbit/s 50ms 200 60"
    exit 1
fi

echo "--- 0. Running setup script ---"
cd ../../
# Ask setup script for 2 clients, using ledbat and cubicX
./setup_experiment.sh 2 "ledbat,cubicX" "$BW" "$CAPACITY" "$RTT" || exit 1
cd - > /dev/null

echo "--- 1. Pre-flight setup ---"
killall iperf3 2>/dev/null

# Bring up loopbacks for both clients
jexec client1 ifconfig lo0 127.0.0.1/8 up
jexec client2 ifconfig lo0 127.0.0.1/8 up

echo "--- 2. Configure IPFW ---"
jexec client1 ipfw -q flush
jexec client1 ipfw add 65534 allow ip from any to any

jexec client2 ipfw -q flush
jexec client2 ipfw add 65534 allow ip from any to any

# Host-side safety rule
ipfw add 1000 allow ip from any to any 2>/dev/null

jexec client1 sysctl net.inet.tcp.cc.ledbat.target=10

echo "--- 3. Connectivity test ---"
jexec client1 ping -c 1 10.0.0.1 >/dev/null 2>&1 || { echo "ERROR: client1 failed"; exit 1; }
jexec client2 ping -c 1 10.0.0.1 >/dev/null 2>&1 || { echo "ERROR: client2 failed"; exit 1; }
echo "Connectivity OK for both clients"

echo "--- 4. Start experiment ---"
dmesg -c > /dev/null

# Start TWO iperf servers on the host on different ports
iperf3 -s -p 5201 -D
iperf3 -s -p 5202 -D

#increasing buffer size to 33MB
sysctl kern.ipc.maxsockbuf=33554432

echo "Running LEDBAT vs CUBIC competition for $DUR seconds..."

# Start both clients in the BACKGROUND using '&' so they run simultaneously
jexec client1 iperf3 -c 10.0.0.1 -w 16M -t $DUR -p 5201 > ./logs/throughput_ledbat.txt &
sleep 1
jexec client2 iperf3 -c 10.0.0.1 -w 16M -t $DUR -p 5202 > ./logs/throughput_cubic.txt &

# Wait for both background processes to finish before continuing
wait

echo "--- 5. Extract CWND logs ---"
# Extract traces into two separate files
dmesg | grep "LEDBAT_TRACE" | awk -F "LEDBAT_TRACE," '{print $2}' > ./logs/cwnd_ledbat.csv
dmesg | grep "CUBIC_TRACE"  | awk -F "CUBIC_TRACE," '{print $2}' > ./logs/cwnd_cubic.csv

echo "Experiment complete."