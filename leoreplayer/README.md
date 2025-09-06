# LeoReplayer: An Accurate Record-and-Replay Tool for LEO networks
LeoReplayer reproduces highly-dynamic LEO network conditions. Specifically, LeoReplayer leverages an important insight that **currently Starlink adopts a separate queue management on its access bottleneck links for ICMP and TCP/UDP traffic, and bandwidth competition between TCP and UDP flows does not affect ICMP traffic**. LeoReplayer records the real dynamic network conditions between the terminal and the server by issuing two concurrent flows: **one heavy UDP flow** saturating the link to record the time-varying maximum capacity, together with **one light ICMP ping flow** to record the base RTT and loss rate. LeoReplayer then extracts the base network conditions and precisely reproduce time-varying network conditions.

## Components
1. **Trace generation scripts**, located in the `recorder` directory. These scripts are used to generate network traces for experiments.

2. **Extended Replayer Environment**. While [Mahimahi](https://github.com/ravinet/mahimahi) provides a useful measurement toolkit for web performance analysis, its built-in replayer **assumes a fixed RTT that cannot vary over time, which makes it unsuitable for highly dynamic environments such as LEO satellite networks**. Our replayer extends Mahimahi by **introducing the ability to reproduce time-varying RTTs and bandwidth conditions**, while still reusing Mahimahi’s original features. This enables accurate replay of dynamic link behaviors observed in real-world satellite networks. Major modifications have been made in the `src` directory:
    - **frontend**: `delay_queue.hh/.cc` + `link_queue.hh/.cc`
    - **packet**: `packetshell.hh/.cc`
    - **util**: `timestamp.hh/.cc` + `interfaces.hh/.cc` + `util.hh/.cc`

## Prerequisites
For Ubuntu 22.04:
```
apt install iproute2 protobuf-compiler libprotobuf-dev autotools-dev dh-autoreconf iptables pkg-config dnsmasq-base  apache2-bin debhelper libssl-dev ssl-cert libxcb-present-dev libcairo2-dev libpango1.0-dev apache2-dev iperf3
```
We have validated the installation process on Ubuntu 22.04. The project may also be portable to other operating systems, but our current validation is limited to Linux environments.

## Installation and Preparation
```
cd replayer
./autogen.sh
./configure
make && sudo make install

sudo sysctl -w net.ipv4.ip_forward=1
```

## Usage Example
The `example` directory gives a basic example on how to start LeoReplayer with `bw_example.txt` and `delay_example.txt` and test Cubic performance using *iperf*.
```
cd example/Cubic/ && sudo bash run.sh
```

## Parameters Suggestion
We fine-tuned several parameters to faithfully reflect their real-world performance.

The following parameters can be found in `example/Cubic/run.sh`:
- `PACKET_LENGTH`:

    set to **500** for the uplink direction and **50000** for the downlink direction.
- `LOSS_RATE/UPLINK_LOSS_RATE`:

    In our measurements of real Starlink networks, we observed that **the uplink loss rate is generally higher than the downlink loss rate**. Accordingly, we set the uplink loss rate to **0.002** and the downlink loss rate to **0.0001**.

    Interestingly, the uplink loss rate also **appears to vary with the sending rate**. For example: With Cubic, an uplink loss rate of 0.002 closely matches the real-world throughput performance. With BBRv3, however, the same 0.002 setting leads to abnormally high throughput in the simulation, whereas in real networks its throughput is only about 60%–70% of BBRv1 (uplink case). On the downlink side, since the loss rate is very low, the throughput of BBRv3 is similar to that of BBRv1.

    Based on these observations, we adopt the following empirical configuration in the uplink:

    - Low-throughput algorithms (Copa, Cubic, Reno, Vegas): `LOSS_RATE = 0.002`

    - High-throughput algorithms (BBRv1, BBRv3, LeoCC): `LOSS_RATE = 0.005`

    For the downlink, the loss rate is set to 0.0001 for all algorithms.

    Note: These values are empirical and not grounded in strong theoretical foundations. Further detailed analysis will be conducted in future work.