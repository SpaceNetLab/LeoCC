# Traces
Due to the large size of the trace files, we provide a download link via cloud storage: https://cloud.tsinghua.edu.cn/d/9fc6fd096e764f57bd25/.
## Directory Structure
The dataset is organized into 8 top-level directories, each containing 600 subdirectories named 1 through 600. **Each subdirectory corresponds to an individual trace**.
In addition, each directory includes a **statistics** file, which summarizes the trace characteristics within that directory.
```
Structure Layout
├── A/
│ ├── 1/
│ ├── 2/
│ ├── 3/
│ ├── ... (total 600 traces)
│ ├── 600/
│ └── A_statistics.txt
├── B/
│ ├── 1/
│ ├── 2/
│ ├── ...
│ ├── 600/
│ └── B_statistics.txt
├── .../
```
## Individual Trace Description
### bw_{trace_no}.txt
This file can be of arbitrary length and must follow the pattern below:
```
0
1
2
3
4
5...
```
**Each line represents a timestamp in milliseconds**, e.g., 0 denotes the 0th millisecond from the start of the trace. 

When the same timestamp appears consecutively, the number of repetitions indicates the bandwidth at that moment. Specifically, each repetition corresponds to 12 Mbps, and the total bandwidth is calculated as 12 Mbps × (number of consecutive lines).
For example:
```
1
1
1
```
means that at 1 ms, the bandwidth is 36 Mbps (since 3 × 12 Mbps).

### delay_{trace_no}.txt
Each line number *i* corresponds to the *i-th* 10-millisecond interval of the trace.
The value on line *i* specifies the one-way delay (in milliseconds) for that interval.

For Example:
```
Line 1: 24   → 0–10 ms  delay = 24 ms
Line 2: 18   → 10–20 ms delay = 18 ms
```
## Trace Statistics File
Since the bandwidth and delay trace files (in `.txt` format) are **the inputs to *LeoReplayer***, they are not designed for readability. To provide practitioners with a quick overview of each trace, we include a `statistics.txt` file in every directory.

The `statistics.txt` file contains information such as **different percentiles of bandwidth and delay, as well as their variance values**, helping practitioners rapidly understand the basic characteristics of each trace.

## Reconfiguration Extraction Script
We provide a helper script that scans the delay trace files and extracts **the timestamp of the first periodic reconfiguration event for each trace**. Please note that this script may not work reliably for every trace, as extreme or irregular cases can exist where periodic reconfiguration is not clearly identifiable.

This timestamp can be directly used by algorithms such as *SaTCP/SatPipe/LeoCC*—in our implementation, it corresponds to the module parameter `offset`—which rely on reconfiguration timing to operate effectively.