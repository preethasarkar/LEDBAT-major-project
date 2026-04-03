#!/bin/sh

echo "--- 1. Pre-Flight Checks & Environment Setup ---"
killall iperf3 2>/dev/null

jexec server ifconfig lo0 127.0.0.1/8 up
jexec cubic_client ifconfig lo0 127.0.0.1/8 up
jexec rledbat_client ifconfig lo0 127.0.0.1/8 up


echo "--- 2. Configuring strict IPFW Rules ---"
jexec cubic_client ipfw -q flush
jexec rledbat_client ipfw -q flush

jexec server ipfw add 65534 allow ip from any to any
jexec cubic_client ipfw add 65534 allow ip from any to any
jexec rledbat_client ipfw add 65534 allow ip from any to any


echo "Testing network bridge connectivity..."
jexec rledbat_client ping -c 1 10.0.0.1 >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: rledbat_client cannot ping server"
    exit 1
fi

echo "Connectivity verified!"


echo "Starting iperf3 servers..."
dmesg -c > /dev/null

jexec server iperf3 -s -p 5201 -D
jexec server iperf3 -s -p 5202 -D


echo "Starting RLEDBAT background flow (200 seconds)..."
jexec rledbat_client iperf3 -c 10.0.0.1 -t 200 -p 5201 > rledbat_throughput.txt &


sleep 50


echo "Starting CUBIC flow (100 seconds)..."
jexec cubic_client iperf3 -c 10.0.0.1 -t 100 -p 5202 > cubic_throughput.txt &


wait


echo "Results saved. Extracting traces..."

dmesg | grep "RLEDBAT_TRACE" | awk -F "RLEDBAT_TRACE," '{print $2}' > rledbat_trace.csv
dmesg | grep "CUBICX_TRACE" | awk -F "CUBICX_TRACE," '{print $2}' > cubicx_trace.csv