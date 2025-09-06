DIR=$(cd "$(dirname "$0")"; pwd)
ALG=cubic
RUNNING_TIME=10
PACKET_LENGTH=500
UPLINK_LOSS_RATE=0.002
DELAY_INTERVAL=10
TRACE_BW_PATH=$DIR/bw_example.txt
TRACE_DELAY_PATH=$DIR/delay_example.txt

iperf3 -s -D
bash $DIR/outer.sh $RUNNING_TIME &

mm-delay $DELAY_INTERVAL $TRACE_DELAY_PATH mm-loss uplink $UPLINK_LOSS_RATE mm-link $TRACE_BW_PATH $TRACE_BW_PATH \
--uplink-queue droptail --uplink-queue-args packets=$PACKET_LENGTH --downlink-queue droptail --downlink-queue-args packets=$PACKET_LENGTH \
bash $DIR/inner.sh $RUNNING_TIME $ALG

pkill iperf