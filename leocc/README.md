# LeoCC
Inspired by the design of *BBR*, *LeoCC* integrates new mechanisms specifically designed for LEO satellite environments. It enhances congestion control by:
- efficiently **detect reconfiguration on the endpoint**;
- apply a **reconfiguration-aware** model to characterize and estimate network conditions accurately;
- precisely **regulate the sending rate**.

There are two variants of *LeoCC*: **one designed for live network deployment, and another tailored for our simulation framework, *LeoReplayer***.

## Live-Network version
Taking the uplink direction as an example, *LeoCC* leverages **netlink** to facilitate message exchange between user space and kernel space, enabling the utilization of RTT information and response intervals within *LeoCC*.

### Overall Workflow
1. **Compile the helper program**

    ```
    cd live_network
    gcc monitor_ping.c -o monitor_ping
    ```
    This produces the executable file `monitor_ping`, which is later invoked by the shell script `get_PoP_IP.sh`.
2. **Build and insert the kernel module**

    ```
    make
    sudo insmod leocc.ko
    ```
    *LeoCC* ***must*** be loaded before running the shell script in order to properly initialize the netlink communication channel.
3. **Run the initialization script**

    ```
    bash get_PoP_IP.sh {des_ip} &
    ```
    Here, *dest_ip* specifies the IP address of the **receiving endpoint**. The script first retrieves the Point of Presence (PoP) IP address, and then invokes monitor_ping to generate a lightweight probing flow **between the user terminal and the PoP**.
4. **Start the traffic generator**

    Launch *iperf* (or any other process under test) to evaluate *LeoCC*. 
    
    When running *iperf*, the option `-C leocc` specifies that the current connection should use *LeoCC* as its congestion control algorithm. In networks with a large BDP, the congestion window may not be the limiting factor; instead, the receive window can become the bottleneck. In such cases, the `-w` option should be used to adjust the TCP window size accordingly.

## Simulation version
Different from the live-network version, when loading *LeoCC* as a kernel module, two extra parameters need to be passed.
- `min_rtt_fluctuation` determines the fluctuation of the link minimum RTT, which is used by *LeoCC* to determine whether to use kalman filter bandwidth or max filter bandwidth.  
- `offset` determines the first periodic reconfiguration time from the start point of the trace as well as the algorithm.

Unlike the live-network setup, the simulation version of *LeoCC* requires **two additional parameters** to be specified when the kernel module is loaded:
- `min_rtt_fluctuation`

    **Defines the degree of fluctuation in the link’s minimum RTT**. *LeoCC* relies on this parameter to determine whether to apply the Kalman-filtered bandwidth estimate or the maximum-filtered bandwidth estimate.

    While different traces may exhibit varying fluctuation levels, our large-scale live-network measurements suggest that a practical range is **5,000–20,000**(corresponding to 5–20 ms).

    Choosing a larger value tends to make the algorithm rely on the maximum-filtered bandwidth, thereby achieving higher throughput. Conversely, selecting a smaller value favors the Kalman-filtered bandwidth estimate, resulting in lower latency.

- `offset`

    Specifies the time offset for the first periodic reconfiguration event within the trace.

## Kernel Version Note
In the upstream Linux kernel, the `tcp_congestion_ops` structure in the `/net/ipv4/tcp_bbr.c` defines the field `.min_tso_segs`. However, this project is built on top of a kernel that incorporates BBRv3, where the corresponding field has been replaced by `.tso_segs`.

As a result, you may notice that our implementation uses `.tso_segs` instead of the standard `.min_tso_segs`. This difference is specific to the BBRv3-based kernel tree and does not affect the core functionality of *LeoCC*.

Additionally, because *LeoCC* adds several fields to its congestion-control private state, **its size exceeds the stock kernel’s `ICSK_CA_PRIV_SIZE`**; therefore, to run LeoCC you must **either rebuild a kernel with a larger `ICSK_CA_PRIV_SIZE` or (recommended) build and install a BBRv3-based kernel.** 

As a temporary workaround, you can hoist some fields from LeoCC’s per-connection struct into module-level global variables to stay within `ICSK_CA_PRIV_SIZE`; this should not affect single-flow experiments (though it is not suitable for multi-flow scenarios).