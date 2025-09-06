DIR=$(cd "$(dirname "$0")"; pwd)
DEV=ingress

tcpdump -i $DEV -s 66 -w $DIR/n1.pcap &
CAP=$!

iperf3 -c 100.64.0.1 -C $2 -t $1

kill $CAP
