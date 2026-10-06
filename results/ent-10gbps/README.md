# ENT results at 10 Gb/s

These files accompany the ENT results in the [project README](../../README.md#results). The CONGA–ECMP baseline covers symmetric loads 0.1–0.9 and asymmetric loads 0.1–0.7. The [initial RTT comparison](rtt-preliminary/) covers the available loads 0.1–0.3 in both topologies.

The CONGA–ECMP baseline figures and CSV summaries are unchanged copies of exports from `8_hosts_v9(prob1)/compare_conga_ecmp_cdf_10gbps_5s.ipynb` in the full simulator repository, with `WORKLOAD = "ENT"`. Their source directory is:

```text
8_hosts_v9(prob1)/readme_figures/conga_ecmp_cdf_10gbps_5s/ENT/
```

| Included files | Contents |
| --- | --- |
| `sym/` and `asym/` | Overall mean FCT relative to CONGA, mean and p99 FCT by size, mean FCT by size relative to ECMP, root queue-drop figures, and queue/utilization summaries |
| [delivery_and_loss_by_topology.png](delivery_and_loss_by_topology.png) | Exact-size flow completion and forward TCP packet loss for both topologies |
| [run_summary.csv](run_summary.csv) | One row per algorithm, topology, and load: 32 runs in total |
| [issues.csv](issues.csv) | Exported validation issues; header only because none were reported |
| [manifests/](manifests/) | Input generation settings, seeds, flow counts, requested load, and realized offered traffic |
| [rtt-preliminary/](rtt-preliminary/) | Initial RTT versus CONGA/ECMP mean-FCT figures and exported summaries for available loads |

## Measurement definitions

- **Application FCT:** time from the scheduled flow start until the full requested payload has arrived. Completion during the drain period is included.
- **Overall normalized mean FCT:** mean algorithm application FCT divided by mean CONGA application FCT over the same input flows at each load, combining all sizes. CONGA is the reference at 1. Each flow contributes equally to the mean; this is not an average of the three size-class means.
- **Normalized FCT by size:** mean CONGA application FCT divided by mean ECMP application FCT over the same input flows, separately for each load and size class. This is a ratio of means, with ECMP as the reference.
- **Size classes:** small below 102,400 bytes; medium from 102,400 to below 1,048,576 bytes; large at least 1,048,576 bytes.
- **p99:** the notebook's NumPy 0.99 quantile of completed application FCTs within each size class. It is a sample percentile, not an uncertainty interval.
- **Exact delivery:** a flow's received payload equals its input size, with completion recorded. Input traces, per-flow delivery records, and independent sink audits agree for all 237,224 flow records across the 32 runs. This total counts each algorithm/topology run separately; it is not a count of distinct input transfers.
- **Forward packet loss:** FlowMonitor lost packets divided by transmitted packets, multiplied by 100, for the forward TCP connections matched to the input flows. It includes connection-control packets on those connections; it is not a fraction of application payload bytes.
- **Root queue drops:** packets dropped by the root FqCoDel queue discs across the topology, including both directions. The plotted value is the drop count per delivered MiB. These are separate counters from FlowMonitor loss.
- **Uplink utilization:** transmitted bytes during 0–5 s divided by that individual directed link's capacity over five seconds. Percentages for different links do not sum to 100%. Traffic during the drain period is excluded from utilization.

## Inputs and configuration

The full simulator repository holds the raw reports under `conga_sym_cdf_10gbps_5s/` and `conga_asym_cdf_10gbps_5s/`. Each `ENT_load_<n>_<Conga|ECMP>.csv` has a `.flows.csv` delivery file and a `.flows.csv.audit.csv` sink audit. Inputs are in `cdf_traffic_10gbps_5s/`. The per-flow FCT values remain in those raw files and the notebook's `sym/summary/flow_validation.csv` and `asym/summary/flow_validation.csv` exports.

The five-second runners configure two leaves, two spines, four servers per leaf, 10 Gb/s healthy links, a 500 us CONGA flowlet timeout, SACK, and a 3 s TCP connection timeout. The asymmetric runner reduces Leaf 0–Spine 0 to 5 Gb/s and enables capacity-aware CONGA congestion accounting. They set a 30 s drain period after the final flow start and a 0–5 s utilization window. The default routing seed is 1. These settings are recorded here from the runner configuration; the output CSVs do not contain a complete command-line metadata record.

The load labels refer to target payload arrival rates relative to the healthy fabric's aggregate 40 Gb/s capacity. They do not represent measured link utilization or a load renormalized to the reduced asymmetric capacity. Each load has one traffic seed, preserved in its manifest, and the same trace is reused across algorithms and topologies. In this five-second sample, the realized loads for targets 0.1–0.9 are approximately 0.123, 0.144, 0.404, 0.495, 0.528, 0.660, 0.767, 0.872, and 0.946.

These are single-run comparisons per load. No repeated-seed confidence intervals are claimed.
