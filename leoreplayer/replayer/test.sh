./src/frontend/mm-loss uplink 0.01 sleep 3 &
sleep 1
ip -br addr
iptables -t nat --list
sleep 5
