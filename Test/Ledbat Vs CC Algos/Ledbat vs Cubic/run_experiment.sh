#!/bin/sh

echo "--- 1. Pre-Flight Checks & Environment Setup ---"
# Kill any leftover iperf3 server processes
killall iperf3 2>/dev/null

# Bring up loopback interfaces in all jails (iperf3 needs this to bind properly)
jexec server ifconfig lo0 127.0.0.1/8 up
jexec cubic_client ifconfig lo0 127.0.0.1/8 up
jexec ledbat_client ifconfig lo0 127.0.0.1/8 up

echo "--- 2. Configuring strict IPFW Rules ---"
# Flush all existing rules in the jails
jexec cubic_client ipfw -q flush
jexec ledbat_client ipfw -q flush

# explicitly ALLOW all traffic to pass in all jails
jexec server ipfw add 65534 allow ip from any to any
jexec cubic_client ipfw add 65534 allow ip from any to any
jexec ledbat_client ipfw add 65534 allow ip from any to any

# Verify basic connectivity before starting the test
echo "Testing network bridge connectivity..."
jexec ledbat_client ping -c 1 10.0.0.1 >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: ledbat_client cannot ping the server (10.0.0.1). Bridge or firewall is broken."
    exit 1
fi
echo "Connectivity verified!"

echo "Starting iperf3 server on port 5201 & 5202..."
dmesg -c > /dev/null
# Start two server instances so both clients can connect simultaneously
jexec server iperf3 -s -p 5201 -D
jexec server iperf3 -s -p 5202 -D

echo "Starting LEDBAT background flow (200 seconds)..."
jexec ledbat_client iperf3 -c 10.0.0.1 -t 200 -p 5201 > ledbat_throughput.txt &

sleep 50

echo "Starting CUBIC flow (100 seconds)..."
jexec cubic_client iperf3 -c 10.0.0.1 -t 100 -p 5202 > cubic_throughput.txt &

# Wait for background jobs to finish
wait


echo "Results saved. You can now run the Python plot script!"

dmesg | grep "LEDBAT_TRACE" | awk -F "LEDBAT_TRACE," '{print $2}' > ledbat_trace.csv
dmesg | grep "CUBICX_TRACE" | awk -F "CUBICX_TRACE," '{print $2}' > cubicx_trace.csv