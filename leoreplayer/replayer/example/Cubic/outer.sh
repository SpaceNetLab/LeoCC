DIR=$(cd "$(dirname "$0")"; pwd)
while true; do
    DEV=$(ip -br link | grep -o 'delay-[[:digit:]]*')
    
    if [ -n "$DEV" ] && ip link show ${DEV} >/dev/null 2>&1; then
        break
    fi
    
    echo "Waiting for Network Interface Creation ..."
done
echo "Network Interface Created ..."
ip route add 100.64.0.0/24 dev $DEV
tcpdump -w $DIR/n2.pcap -s 66 -i $DEV &
CAP=$!

sleep $1
kill $CAP
