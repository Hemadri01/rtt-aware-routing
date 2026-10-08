# RTT comparison notebooks

Open [the illustrated results](rtt_cdf_10gbps_5s_results.ipynb) to see every
figure with its explanation. The notebook is saved with the images embedded,
and the matching PNG/SVG export is included under
[results/ent-10gbps/rtt-full](../results/ent-10gbps/rtt-full/README.md).
Run all cells to reload the included ENT export. The notebook also supports
DM after a matching DM export is added.

The [export notebook](compare_rtt_readme_cdf_10gbps_5s.ipynb) and its
[validation helper](compare_checked.py) are included as analysis source.
Generating a new export requires the input traces, simulator outputs, and
sidecars from the full ns-3 checkout. It does not launch simulations.
