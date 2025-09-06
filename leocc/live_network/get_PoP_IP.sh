#!/bin/bash

TARGET_IP="$1"

if [ -z "$TARGET_IP" ]; then
    echo "Usage: $0 <target_ip>"
    exit 1
fi

pop_ip=$(traceroute -n "$TARGET_IP" | awk '$2 ~ /^206\.224/ {print $2; exit}')

if [ -z "$pop_ip" ]; then
    echo "No IP address starting with 206.224 found in the traceroute output."
else
    echo "First IP address starting with 206.224 to $TARGET_IP is: $pop_ip"
fi

if ping -c 5 -W 2 "$pop_ip" > /dev/null 2>&1; then
    echo "Successfully pinged $pop_ip"
else
    echo "Failed to ping $pop_ip, trying to XOR the last octet and ping again."

    last_octet=$(echo "$pop_ip" | awk -F. '{print $4}')
    xor_octet=$((last_octet ^ 1))
    new_ip=$(echo "$pop_ip" | awk -F. '{print $1 "." $2 "." $3 "."}')$xor_octet
    echo "New IP address after XOR: $new_ip"

    if ping -c 5 -W 2 "$new_ip" > /dev/null 2>&1; then
        echo "Successfully pinged $new_ip"
   	pop_ip=$new_ip
    else
        echo "Failed to ping $new_ip, Exception Caught"
        exit 1
    fi
fi

echo "Pingable POP_IP to $TARGET_IP is: $pop_ip"

CUR_DIR=$(cd "$(dirname "$0")"; pwd)
$CUR_DIR/monitor_ping "$pop_ip"