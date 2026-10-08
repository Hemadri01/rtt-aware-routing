# Complete ENT comparison

The [results notebook](../../../analysis/rtt_cdf_10gbps_5s_results.ipynb) presents
all 65 figures with explanations. Its saved outputs include the images, and it
can be rerun using the bundled export. The [export notebook](../../../analysis/compare_rtt_readme_cdf_10gbps_5s.ipynb)
shows how the full simulator outputs were validated and compared.

The [dated ENT export](ENT/20261008T074640_547349Z/INDEX.md) contains PNG and
SVG versions of every figure, along with aggregate CSV summaries and the
figure manifest. [latest.json](ENT/latest.json) identifies this snapshot for
the results notebook. The exact-flow validation covered 80 runs: all requested
payloads were delivered and all FCT comparisons include every input flow.

The large per-flow validation tables and raw simulator reports remain in the
full ns-3 checkout. The aggregate summaries in this folder preserve the
comparison results unchanged. Report paths in the run summary are relative
to the full simulator root. The 21 entries in
`issues.csv` concern the known nominal-capacity utilization report for the
asymmetric 5 Gb/s link. The figures recalculate utilization from transmitted
bytes and that link's physical capacity.

The experiments use one generated trace and one routing run per topology,
load, and policy. The figures do not give uncertainty across repeated runs.
