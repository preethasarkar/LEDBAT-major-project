#!/bin/sh

echo "--- Cleaning up previous state ---"
# Destroy old jails if they exist
jail -r server cubic_client ledbat_client 2>/dev/null
# Destroy old bridges and epair interfaces
ifconfig bridge0 destroy 2>/dev/null
for iface in $(ifconfig -l | tr ' ' '\n' | grep '^epair'); do
    ifconfig $iface destroy 2>/dev/null
done

echo "--- 1. Loading Kernel Modules ---"
kldload if_epair 2>/dev/null
kldload if_bridge 2>/dev/null

# Safely load IPFW and Dummynet BEFORE creating the jails
# Setting default_to_accept prevents the host from dropping all network traffic
sysctl net.inet.ip.fw.default_to_accept=1
kldload ipfw 2>/dev/null
kldload dummynet 2>/dev/null

echo "--- 1.5. Loading Custom LEDBAT Module ---"
# Save the current directory, move to CC_LOADER, execute the loader, and return
CURRENT_DIR=$(pwd)
cd ../CC_LOADER || { echo "ERROR: Could not find ../CC_LOADER directory!"; exit 1; }
./cc_loader.sh cc_ledbat
./cc_loader.sh cc_cubicX
cd "$CURRENT_DIR" || exit 1

echo "--- 2. Creating Virtual Topology ---"
# Create the virtual bridge (Switch)
ifconfig bridge0 create up

# Create VNET Jails (Now they will inherit the IPFW/Dummynet hooks!)
jail -c vnet name=server persist
jail -c vnet name=cubic_client persist
jail -c vnet name=ledbat_client persist

# --- 3. Create and attach epair interfaces ---
# Server
SRV_EPAIR=$(ifconfig epair create)
SRV_EPAIR_B=$(echo $SRV_EPAIR | sed 's/a$/b/')
ifconfig $SRV_EPAIR up
ifconfig bridge0 addm $SRV_EPAIR
ifconfig $SRV_EPAIR_B vnet server

# CUBIC Client
CUB_EPAIR=$(ifconfig epair create)
CUB_EPAIR_B=$(echo $CUB_EPAIR | sed 's/a$/b/')
ifconfig $CUB_EPAIR up
ifconfig bridge0 addm $CUB_EPAIR
ifconfig $CUB_EPAIR_B vnet cubic_client

# LEDBAT Client
LED_EPAIR=$(ifconfig epair create)
LED_EPAIR_B=$(echo $LED_EPAIR | sed 's/a$/b/')
ifconfig $LED_EPAIR up
ifconfig bridge0 addm $LED_EPAIR
ifconfig $LED_EPAIR_B vnet ledbat_client

echo "--- 4. Configuring IPs and Routing ---"
jexec server ifconfig $SRV_EPAIR_B 10.0.0.1/24 up
jexec cubic_client ifconfig $CUB_EPAIR_B 10.0.0.2/24 up
jexec ledbat_client ifconfig $LED_EPAIR_B 10.0.0.3/24 up

echo "--- 5. Applying Congestion Control Algorithms ---"
jexec cubic_client sysctl net.inet.tcp.cc.algorithm=cubicX
jexec ledbat_client sysctl net.inet.tcp.cc.algorithm=ledbat
jexec ledbat_client sysctl net.inet.tcp.cc.ledbat.target=30

echo "--- 6. Creating the Dummynet Bottleneck ---"
# Simulate a 20Mbps router link with a 100-packet queue and 20ms base delay
jexec server ipfw -q flush
jexec server ipfw add 100 pipe 1 ip from any to any
jexec server ipfw pipe 1 config bw 20Mbit/s queue 100 delay 20ms

echo "Topology setup complete! The environment is ready."
