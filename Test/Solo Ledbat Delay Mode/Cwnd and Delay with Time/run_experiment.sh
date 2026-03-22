#!/bin/sh

BW=$1
RTT=$2

if [ -z "$BW" ] || [ -z "$RTT" ]; then
    echo "Usage: $0 <BW> <RTT>"
    echo "Example: $0 20Mbit/s 50ms"
    exit 1
fi

echo "--- 0. Running setup script ---"
cd ../../
# This calls the setup script we modified earlier
./setup_experiment.sh 1 "ledbat" "$BW" 500 "$RTT" || exit 1
cd - > /dev/null

echo "--- 1. Pre-flight setup ---"
# Kill iperf3 on both the host and any stray jail processes
killall iperf3 2>/dev/null

# Host loopback is usually already up, but we ensure the client's is active
jexec client1 ifconfig lo0 127.0.0.1/8 up

echo "--- 2. Configure IPFW ---"

# Client-side firewall rules
jexec client1 ipfw -q flush
jexec client1 ipfw add 65534 allow ip from any to any

# Host-side firewall rules (Since host is now the server)
# Note: Be careful with 'flush' on a host if you have existing SSH/Firewall rules.
# We add an allow rule specifically for the experiment traffic.
ipfw add 1000 allow ip from any to any

echo "--- 3. Connectivity test ---"
# Client1 pings the Host (10.0.0.1)
jexec client1 ping -c 1 10.0.0.1 >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: client1 cannot reach Host (10.0.0.1)"
    exit 1
fi
echo "Connectivity OK"

echo "--- 4. Start experiment ---"

# Clear kernel logs
dmesg -c > /dev/null

# Start iperf server DIRECTLY on the host
iperf3 -s -p 5201 -D

echo "Running LEDBAT flow for 300 seconds..."
# Client1 connects to the host
jexec client1 iperf3 -c 10.0.0.1 -t 300 -p 5201 > throughput.txt

echo "--- 5. Extract CWND logs ---"

# Note: LEDBAT_TRACE must be supported/enabled in your kernel module
dmesg | grep "LEDBAT_TRACE" | awk -F "LEDBAT_TRACE," '{print $2}' > cwnd.csv

echo "Done. Now run: python3 plot.py"