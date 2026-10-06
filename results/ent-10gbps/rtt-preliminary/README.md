# Initial RTT comparison

These figures and summaries come from `8_hosts_v9(prob1)/compare_rtt_available_cdf_10gbps_5s.ipynb` in the full simulator repository, run with `WORKLOAD = "ENT"`. The source snapshot was saved under `8_hosts_v9(prob1)/analysis_rtt_available_cdf_10gbps_5s/ENT/20261006T070407_722954Z/`. The files are copied unchanged except that `run_summary.csv` uses paths relative to the full simulator root instead of machine-specific absolute paths.

The two figures show overall mean application FCT at target loads 0.1–0.3, normalized to CONGA at each load. [Symmetric](sym/overall_mean_fct_vs_load.png) and [asymmetric](asym/overall_mean_fct_vs_load.png) topologies are kept separate. Each point uses the flows shared by the policies available at that load. A policy absent from a load has no point.

- [fct_summary.csv](fct_summary.csv): mean and p99 application FCT for all flows and for the small, medium, and large size classes. Ratios to both CONGA and ECMP use the same completed-flow cohort.
- [cohorts.csv](cohorts.csv): available policies, input flows, shared flows, and exclusions for each topology/load case. All six available cohorts have zero exclusions.
- [run_summary.csv](run_summary.csv): payload completion, forward packet loss, and queue-drop counts for each of the 28 analyzed runs, including 16 RTT runs and their 12 matching baselines.
- [issues.csv](issues.csv): eight utilization reporting discrepancies on the asymmetric 5 Gb/s link. RTT's CSV divides by the nominal 10 Gb/s rate; the notebook recalculates the heatmaps from the transmitted bytes and physical capacity. These entries do not indicate incomplete transfers.

These results cover one input trace and one simulation per case. Weighted RTT with an RTT-derived timeout is still missing at load 0.3; the higher loads have not been included. No uncertainty intervals or general performance ranking are claimed.
