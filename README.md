<p align="center">
  <img src="./assets\LeoCC_Logo.png" alt="LeoCC Logo" width="200">
</p>

<p align="center">
    <a href="./README.md">
      <img alt="Documentation" src="https://img.shields.io/badge/docs-gray?logo=readthedocs&logoColor=f5f5f5">
    </a>
    <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
    <img src="https://img.shields.io/badge/Platform-Linux-purple" alt="Platform">
</p>

<h2 align="center" tabindex="-1" class="heading-element" dir="auto">
    LeoCC: Making Internet Congestion Control Robust to LEO Satellite Dynamics
</h2>
The recent renaissance of LEO satellite networks expands the boundaries of global Internet access, but also introduces substantial new challenges for existing end-to-end CCAs. The rapid and continuous movement of LEO satellites leads to infrastructure-level dynamics, resulting in frequent, LEO-dynamics-induced changes in link capacity, delay, and packet loss rate, which can further mislead the rate control in existing CCAs and cause self-limited performance.

Therefore, we presents *LeoCC*, a novel CCA that addresses the above challenges and is robust to LEO satellite dynamics. The core idea behind *LeoCC* lies in a critical characteristic of emerging LEO networks called **“connection reconfiguration”**, which implicitly reflects satellite path changes and is strongly correlated to network variations. Specifically, *LeoCC* employs a suite of new techniques to: **(i) efficiently detect reconfiguration on the endpoint; (ii) apply a reconfiguration-aware model to characterize and estimate network conditions accurately; and (iii) precisely regulate the sending rate.** 

## Overview
The directories list our contributions, comprising ***LeoCC*, *LeoReplayer* and traces we collect in real world**.
### LeoCC
Inspired by the design of *BBR*, *LeoCC* integrates new mechanisms specifically designed for LEO satellite environments. There are several major modifications:
- Detecting connection reconfigurations efficiently at the endpoint.
- Applying a reconfiguration-aware model for accurate bandwidth and delay estimation.
- Precisely regulating the sending rate.

There are **two variants** of *LeoCC*:
- **Live-Network version**: designed for deployment in live networks, using Netlink to exchange RTT and response interval information between kernel space and user space.
- **Simulation version**: adapted to the *LeoReplayer* framework, requiring additional parameters (`min_rtt_fluctuation`, `offset`) to model realistic reconfiguration behaviors observed in satellite networks.

>**Note**: This implementation is based on a BBRv3-derived kernel. As a result, `.tso_segs` is used instead of the standard `.min_tso_segs`, though this difference does not affect *LeoCC's* functionality.

### LeoReplayer
*LeoReplayer* is a record-and-replay tool designed to reproduce the highly dynamic conditions of LEO satellite networks.
- It records real network dynamics using two concurrent flows:
    - **a heavy UDP flow** to capture time-varying maximum capacity;
    - **a light ICMP flow** to measure base RTT and packet loss.
- The recorded traces are then extracted and replayed to **precisely emulate bandwidth, delay, and loss variations** observed in real Starlink networks.

The system consists of two major parts:
1. **Recorder scripts** for collecting real-world measurements.
2. **Extended Replayer Environment**. Our replayer extends [Mahimahi](https://github.com/ravinet/mahimahi) by **introducing the ability to reproduce time-varying RTTs and bandwidth conditions**, while still reusing Mahimahi’s original features. This enables accurate replay of dynamic link behaviors observed in real-world satellite networks.  

### Trace
We collect a large-scale dataset from real-world Starlink experiments. **They are organized into 8 directories, each containing 600 individual traces plus a per-directory statistics file.** These traces serve as the foundation for reproducing realistic network dynamics in *LeoReplayer*.

## Citation
If you find our contributions helpful in your project or research, please cite with the following BibTeX entry:
```bibtex
@inproceedings{lai2025leocc,
  title={LeoCC: Making Internet Congestion Control Robust to LEO Satellite Dynamics},
  author={Lai, Zeqi and Li, Zonglun and Wu, Qian and Li, Hewu and Li, Jihao and Xie, Xin and Li, Yuanjie and Liu, Jun and Wu, Jianping},
  booktitle={Proceedings of the ACM SIGCOMM 2025 Conference},
  pages={129--146},
  year={2025}
}
```

## Contact Us
For any questions or feedback related to this project, please feel free to get in touch with us. (Email:[zeqilai@tsinghua.edu.cn](mailto:zeqilai@tsinghua.edu.cn), [lzl24@mails.tsinghua.edu.cn](mailto:lzl24@mails.tsinghua.edu.cn)).
