#!/bin/sh

NUM_CLIENTS=$1
CC_LIST=$2
BW=$3
SIZE=$4
DELAY=$5

if [ -z "$NUM_CLIENTS" ] || [ -z "$CC_LIST" ] || [ -z "$BW" ] || [ -z "$SIZE" ] || [ -z "$DELAY" ]; then
    echo "Usage: $0 <num_clients> \"cc1,cc2,...\" <bandwidth> <size> <delay>"
    exit 1
fi

# Convert comma-separated list to space-separated for the loop
CC_LIST_SPACED=$(echo "$CC_LIST" | tr ',' ' ')
CC_COUNT=$(echo $CC_LIST_SPACED | wc -w)

if [ "$CC_COUNT" -ne "$NUM_CLIENTS" ]; then
    echo "ERROR: Number of CC algorithms must match number of clients"
    exit 1
fi

echo "--- Cleaning up previous state ---"
# Clean up client jails
for j in $(jls name | grep client); do
    jail -r $j 2>/dev/null
done

ifconfig bridge0 destroy 2>/dev/null
for iface in $(ifconfig -l | tr ' ' '\n' | grep '^epair'); do
    ifconfig $iface destroy 2>/dev/null
done

echo "--- Loading Base Kernel Modules ---"
kldload if_epair 2>/dev/null
kldload if_bridge 2>/dev/null
sysctl net.inet.ip.fw.default_to_accept=1 2>/dev/null
kldload ipfw 2>/dev/null
kldload dummynet 2>/dev/null

echo "--- Loading Specific CC Modules for this Experiment ---"
CURRENT_DIR=$(pwd)
cd ../CC_LOADER || exit 1

# DYNAMIC LOADING: Only load what is in CC_LIST
for CC in $CC_LIST_SPACED; do
    echo "Processing module: cc_$CC"
    # Check if a custom loader/folder exists for this CC
    if [ -d "./cc_$CC" ]; then
        ./cc_loader.sh "cc_$CC"
    else
        # Otherwise, attempt to load as a standard system module
        kldload "cc_$CC" 2>/dev/null
    fi
done

cd "$CURRENT_DIR" || exit 1

echo "--- Creating Topology ---"
# Create the bridge that connects the host to the client jails
ifconfig bridge0 create 
# Assign the server IP (10.0.0.1) directly to the bridge on the host
ifconfig bridge0 inet 10.0.0.1/24 up

echo "--- Toggling off Delayed Ack ---"
sysctl net.inet.tcp.delayed_ack=0

i=1
for CC in $CC_LIST_SPACED; do
    CLIENT="client$i"
    IP="10.0.0.$((i+1))"

    echo "Creating $CLIENT with CC=$CC"

    jail -c vnet name=$CLIENT persist

    EPAIR=$(ifconfig epair create)
    EPAIR_B=$(echo $EPAIR | sed 's/a$/b/')
    
    ifconfig $EPAIR up
    ifconfig bridge0 addm $EPAIR
    
    # Move the 'b' side into the client jail
    ifconfig $EPAIR_B vnet $CLIENT
    jexec $CLIENT ifconfig $EPAIR_B $IP/24 up

    # Apply CC to the client jail specifically
    jexec $CLIENT sysctl net.inet.tcp.cc.algorithm=$CC
    jexec $CLIENT sysctl net.inet.tcp.delayed_ack=0
    i=$((i+1))
done

echo "--- Configuring Dummynet on Host ---"
# Flush rules on the host and apply the pipe to the bridge interface
ipfw -q flush
# Apply the pipe to traffic passing through the bridge to simulate the bottleneck
ipfw add 100 pipe 1 ip from any to any via bridge0
sysctl net.inet.ip.dummynet.pipe_slot_limit=10000
ipfw pipe 1 config bw $BW queue $SIZE delay $DELAY

echo "Setup complete! Host is now the server at 10.0.0.1"