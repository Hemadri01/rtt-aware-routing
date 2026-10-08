#!/usr/bin/env python3
"""Compare CONGA/ECMP/RTT results with independent application-payload checks.

Only numpy and matplotlib are required. The original notebooks are untouched.
Matching and the default FlowMonitor FCT metric follow those notebooks; byte
validation is an independent audit and never chooses a different match.
"""

import argparse
import csv
import io
import json
import math
import os
import re
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path

TRACE_DIR = Path(__file__).resolve().parent
# Keep Matplotlib's cache with the project when this command-line tool is run
# in a restricted environment where the user cache and /tmp are unsuitable.
os.environ.setdefault("MPLCONFIGDIR", str(TRACE_DIR / ".matplotlib"))

import matplotlib

matplotlib.use("Agg")  # Save figures on a server as well as from Jupyter.
import matplotlib.pyplot as plt
import numpy as np
BENCHMARKS = ["private_enterprise", "social_media_cloud", "commercial_cloud", "university"]
ALGORITHMS = {
    "conga": ("conga", "Conga", "CONGA", "#0072B2"),
    "ecmp": ("conga", "ECMP", "ECMP", "#555555"),
    "weighted_true": ("rtt", "WEIGHTED_ECMP_timeout_true_RTT", "Weighted RTT / fixed timeout", "#009E73"),
    "weighted_false": ("rtt", "WEIGHTED_ECMP_timeout_false_RTT", "Weighted RTT / adaptive timeout", "#009E73"),
    "random_true": ("rtt", "POWER_OF_2_RANDOM_timeout_true_RTT", "Random two / fixed timeout", "#D55E00"),
    "random_false": ("rtt", "POWER_OF_2_RANDOM_timeout_false_RTT", "Random two / adaptive timeout", "#D55E00"),
    "top2_true": ("rtt", "POWER_OF_2_TOP2_timeout_true_RTT", "Best two / fixed timeout", "#CC79A7"),
    "top2_false": ("rtt", "POWER_OF_2_TOP2_timeout_false_RTT", "Best two / adaptive timeout", "#CC79A7"),
    "lrtt_true": ("rtt", "LOWEST_RTT_timeout_true_RTT", "Lowest RTT / fixed timeout", "#E69F00"),
    "lrtt_false": ("rtt", "LOWEST_RTT_timeout_false_RTT", "Lowest RTT / adaptive timeout", "#E69F00"),
}
# Fixed 500 us Random-Two results are retained as a diagnostic option, but
# excluded from normal comparisons because they are incompatible with the
# current RTT flowlet design and can leave TCP flows incomplete.
DEFAULT_ALGORITHMS = ["conga", "ecmp", "weighted_true", "weighted_false", "random_false"]
STATUSES = ["exact", "undersized", "oversized", "missing", "unverified", "invalid"]
STATUS_COLORS = ["#009E73", "#E69F00", "#D55E00", "#CC79A7", "#999999", "#882255"]
SMALL, LARGE = 100 * 1024, 1024 * 1024


def integer(value):
    """Read integer bytes exactly, including trace strings such as '300.0'."""
    number = Decimal(str(value))
    if not number.is_finite() or number != number.to_integral_value():
        raise ValueError(f"Expected an integer, got {value!r}")
    return int(number)


def seconds(value):
    """Parse ns-3 time strings with units, or a plain number of seconds."""
    match = re.fullmatch(r"\s*([+-]?[\d.]+(?:[eE][+-]?\d+)?)\s*(ns|us|ms|s)?\s*", str(value))
    if not match:
        raise ValueError(f"Invalid time: {value!r}")
    result = float(match[1]) * {None: 1, "s": 1, "ms": 1e-3, "us": 1e-6, "ns": 1e-9}[match[2]]
    if not math.isfinite(result):
        raise ValueError(f"Non-finite time: {value!r}")
    return result


def expected_port(index):
    """Reproduce the driver's uint16_t assignment and single port wrap."""
    port = (10000 + index) % 65536
    return port - 40000 if port > 50000 else port


def ip_to_node(address, servers_per_leaf=4):
    """Map the current driver's 10.1.<leaf+1>.<2*(server+1)> host IPs."""
    parts = [int(p) for p in address.split(".")]
    if (len(parts) != 4 or parts[:2] != [10, 1] or not 1 <= parts[2] <= 255
            or not 2 <= parts[3] <= min(254, 2 * servers_per_leaf) or parts[3] % 2):
        raise ValueError(f"Not a server address in this topology: {address}")
    return (parts[2] - 1) * servers_per_leaf + parts[3] // 2 - 1


def read_trace(path):
    """Read all trace rows; never silently discard an invalid requested size."""
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"flow_id", "sn", "dn", "flow_size", "event_time"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("Input trace lacks flow_id/sn/dn/flow_size/event_time columns")
        first = reader.fieldnames[0]
        flows = []
        seen = set()
        for row in reader:
            index = integer(row[first])  # C++ consumes the first CSV column.
            if "index" in row and integer(row["index"]) != index:
                raise ValueError(f"First column and index disagree at trace row {index}")
            size = integer(row["flow_size"])
            if index < 0 or index in seen or size <= 0:
                raise ValueError(f"Duplicate/negative index or nonpositive size at {index}")
            seen.add(index)
            flow = dict(index=index, flow_id=row["flow_id"], sn=integer(row["sn"]),
                        dn=integer(row["dn"]), size=size, start=seconds(row["event_time"]))
            if min(flow["sn"], flow["dn"], flow["start"]) < 0:
                raise ValueError(f"Negative endpoint/start at {index}")
            flows.append(flow)
    if not flows:
        raise ValueError("Input trace contains no flows to compare")
    return sorted(flows, key=lambda f: f["start"])


def read_flowmonitor(path, servers_per_leaf):
    """Read only the first CSV table, without the old 12,000-row truncation.

    Later per-node/link sections have a different schema and are not IP flows.
    Keep invalid rows visible as issues rather than quietly losing a match.
    """
    lines = []
    with path.open() as stream:
        for line in stream:
            if not line.strip() or line.startswith("==="):
                break
            lines.append(line)
    reader = csv.DictReader(io.StringIO("".join(lines)))
    required = {"FlowID", "Src", "Dest", "SrcPort", "DestPort", "TimeFirstTxPacket", "FCT(s)"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError(f"Missing FlowMonitor columns: {sorted(required - set(reader.fieldnames or []))}")
    records, problems = [], []
    for line, row in enumerate(reader, start=2):
        try:
            source = ip_to_node(row["Src"], servers_per_leaf)
            destination = ip_to_node(row["Dest"], servers_per_leaf)
            records.append(dict(raw=row, key=(source, destination, integer(row["DestPort"])),
                                start=seconds(row["TimeFirstTxPacket"]),
                                fct=float(row["FCT(s)"]), ordinal=line))
        except (ValueError, TypeError, ArithmeticError) as error:
            problems.append(f"FlowMonitor line {line}: {error}")
    return records, problems


def match_flows(flows, records):
    """Original tuple + nearest-start rule; size NEVER influences this decision.

    As in the originals, this does not consume a candidate after matching it.
    Candidate ambiguity and reuse are reported separately for the researcher.
    """
    groups = defaultdict(list)
    for record in records:
        groups[record["key"]].append(record)
    matches = {}
    for flow in flows:
        candidates = groups[(flow["sn"], flow["dn"], expected_port(flow["index"]))]
        best = min(candidates, key=lambda r: abs(r["start"] - flow["start"]), default=None)
        matches[flow["index"]] = (best, len(candidates))
    return matches


def indexed_csv(path):
    """Retain duplicate indices so a duplicate can never be mistaken for a pass."""
    grouped = defaultdict(list)
    problems = []
    with path.open(newline="") as stream:
        for line, row in enumerate(csv.DictReader(stream), start=2):
            try:
                grouped[integer(row["TraceIndex"])].append(row)
            except (KeyError, ValueError, TypeError, ArithmeticError) as error:
                problems.append(f"{path.name} line {line}: invalid TraceIndex ({error})")
    return grouped, problems


def validate_payload(flow, entries, sidecar_exists):
    """Check received application bytes against the INPUT, not ExpectedBytes alone.

    Identity, start time, expected size, completion flag, and completion times
    must agree too. Missing old sidecars are explicitly unverified.
    """
    result = dict(size_status="unverified", received_bytes="", byte_error="",
                  application_fct_s="", issues=[])
    if not sidecar_exists:
        result["issues"].append("No application payload sidecar; IP bytes cannot verify size")
        return result
    if not entries:
        result.update(size_status="missing", issues=["Missing input flow in payload sidecar"])
        return result
    if len(entries) != 1:
        result.update(size_status="invalid", issues=["Duplicate TraceIndex in payload sidecar"])
        return result
    row = entries[0]
    try:
        actual_identity = (row["FlowID"], integer(row["Src"]), integer(row["Dst"]), integer(row["DestPort"]))
        identity = (flow["flow_id"], flow["sn"], flow["dn"], expected_port(flow["index"]))
        if actual_identity != identity:
            raise ValueError("Sidecar identity differs from input trace")
        if integer(row["ExpectedBytes"]) != flow["size"]:
            raise ValueError("Sidecar ExpectedBytes differs from input flow_size")
        if not math.isclose(seconds(row["StartTime(s)"]), flow["start"], abs_tol=1e-9, rel_tol=0):
            raise ValueError("Sidecar start time differs from input")
        received = integer(row["ReceivedBytes"])
        if received < 0:
            raise ValueError("Negative ReceivedBytes")
        error = received - flow["size"]
        result.update(received_bytes=received, byte_error=error,
                      size_status="exact" if error == 0 else "undersized" if error < 0 else "oversized")
        if error:
            result["issues"].append(f"Payload differs from input by {error:+d} bytes")
        if integer(row["Complete"]) != int(error == 0):
            raise ValueError("Complete flag disagrees with payload bytes")
        if error == 0:
            fct = seconds(row["FCT(s)"])
            completion = seconds(row["CompletionTime(s)"])
            if fct < 0 or not math.isclose(completion - flow["start"], fct, abs_tol=2e-9, rel_tol=0):
                raise ValueError("Invalid application completion time/FCT")
            result["application_fct_s"] = fct
        elif row["FCT(s)"] or row["CompletionTime(s)"]:
            raise ValueError("Incomplete/oversized flow has a completion time")
    except (KeyError, ValueError, TypeError, ArithmeticError) as error:
        result["issues"].append(str(error))
        # Keep a known size mismatch visible even if metadata is also wrong.
        if result["size_status"] not in ("undersized", "oversized"):
            result["size_status"] = "invalid"
    return result


def check_sink_audit(flow, payload, entries):
    """Cross-check the optional independent PacketSink audit when it is present."""
    if len(entries) != 1:
        return ["Missing/duplicate TraceIndex in sink audit"]
    try:
        row = entries[0]
        expected = integer(row["ExpectedBytes"])
        accepted = integer(row["SenderAcceptedBytes"])
        callback = integer(row["CallbackBytes"])
        sink = integer(row["SinkTotalBytes"])
        if expected != flow["size"] or not 0 <= accepted <= expected:
            return ["Sink audit expected/sender byte count disagrees with input"]
        if sink != callback or callback != payload["received_bytes"] or min(sink, callback) < 0:
            return ["Independent sink, callback and sidecar byte counts disagree"]
        if payload["size_status"] == "exact" and accepted != expected:
            return ["Exact receive count but sender accepted fewer than expected bytes"]
    except (KeyError, ValueError, TypeError, ArithmeticError) as error:
        return [f"Invalid sink audit: {error}"]
    return []


def inspect_run(flows, path, servers_per_leaf=4):
    """Return one diagnostic row for EVERY input flow, including unmatched rows."""
    records, issues = read_flowmonitor(path, servers_per_leaf)
    matches = match_flows(flows, records)
    sidecar = Path(str(path) + ".flows.csv")
    audit_path = Path(str(sidecar) + ".audit.csv")
    sidecar_exists, audit_exists = sidecar.exists(), audit_path.exists()
    payloads, extra = indexed_csv(sidecar) if sidecar_exists else ({}, [])
    issues.extend(extra)
    audits, extra = indexed_csv(audit_path) if audit_exists else ({}, [])
    issues.extend(extra)
    indices = {f["index"] for f in flows}
    for label, rows in [("payload sidecar", payloads), ("sink audit", audits)]:
        for index in sorted(set(rows) - indices):
            issues.append(f"Unexpected TraceIndex {index} in {label}")
    used = Counter(best["ordinal"] for best, _ in matches.values() if best is not None)
    results = []
    for flow in flows:
        index = flow["index"]
        best, candidates = matches[index]
        payload = validate_payload(flow, payloads.get(index, []), sidecar_exists)
        notes = payload.pop("issues")
        if audit_exists:
            notes.extend(check_sink_audit(flow, payload, audits.get(index, [])))
        row = dict(trace_index=index, flow_id=flow["flow_id"], sn=flow["sn"], dn=flow["dn"],
                   input_start_s=flow["start"], expected_bytes=flow["size"],
                   expected_dest_port=expected_port(index), matched=best is not None,
                   candidates=candidates, flowmonitor_id="", flowmonitor_fct_s="",
                   start_difference_s="", flowmonitor_tx_bytes="", flowmonitor_rx_bytes="",
                   **payload)
        if best is None:
            notes.append("No FlowMonitor match")
        else:
            row.update(flowmonitor_id=best["raw"]["FlowID"], flowmonitor_fct_s=best["fct"],
                       start_difference_s=abs(best["start"] - flow["start"]),
                       flowmonitor_tx_bytes=best["raw"].get("TxBytes", ""),
                       flowmonitor_rx_bytes=best["raw"].get("RxBytes", ""))
            if candidates > 1:
                notes.append("Multiple candidates; original nearest-start rule used")
            if used[best["ordinal"]] > 1:
                notes.append("FlowMonitor row reused by multiple input flows")
            if not math.isfinite(best["fct"]) or best["fct"] <= 0:
                notes.append("Nonpositive/non-finite FlowMonitor FCT")
        row["verified"] = payload["size_status"] == "exact" and not notes
        row["issues"] = "; ".join(notes)
        results.append(row)
    return results, issues


def write_csv(path, rows, fields=None):
    """Write a table even when empty so downstream work never reuses a stale file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = list(rows)
    columns = fields or list(rows[0] if rows else {"message": None})
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def summarize_comparison(flows, runs, algorithms, metric, cohort):
    """Use the same intersection of matched input flows for every algorithm.

    Default: retain size errors in comparisons, as requested, and report them.
    Optional verified cohort: explicitly restrict to verified rows in ALL runs.
    Normalization remains mean(algorithm FCT) / mean(ECMP FCT).
    """
    indexed = {a: {r["trace_index"]: r for r in runs[a]} for a in algorithms}
    selected = []
    for flow in flows:
        rows = [indexed[a][flow["index"]] for a in algorithms]
        if not all(row["matched"] for row in rows):
            continue
        if cohort == "verified" and not all(row["verified"] for row in rows):
            continue
        # A common finite, positive FCT population prevents unequal denominators.
        if not all(isinstance(row[metric], (float, int)) and math.isfinite(row[metric])
                   and row[metric] > 0 for row in rows):
            continue
        selected.append(flow)
    summaries, series, wide = [], {}, []
    for flow in selected:
        row = dict(trace_index=flow["index"], flow_id=flow["flow_id"], sn=flow["sn"],
                   dn=flow["dn"], input_time=flow["start"], flow_size=flow["size"])
        for algorithm in algorithms:
            source = indexed[algorithm][flow["index"]]
            for name in ("flowmonitor_fct_s", "application_fct_s", "size_status", "verified", "byte_error"):
                row[f"{name}_{algorithm}"] = source[name]
        wide.append(row)
    for group, select in [("all", lambda size: True), ("small", lambda size: size < SMALL),
                          ("large", lambda size: size > LARGE)]:
        subset = [f for f in selected if select(f["size"])]
        for algorithm in algorithms:
            values = np.array([indexed[algorithm][f["index"]][metric] for f in subset], dtype=float)
            series[algorithm, group] = values
            baseline = np.array([indexed["ecmp"][f["index"]][metric] for f in subset]) if "ecmp" in indexed else np.array([])
            summaries.append(dict(algorithm=algorithm, size_group=group, n=len(values),
                                  mean_fct_ms=float(values.mean() * 1000) if len(values) else "",
                                  p50_fct_ms=float(np.quantile(values, .50) * 1000) if len(values) else "",
                                  p75_fct_ms=float(np.quantile(values, .75) * 1000) if len(values) else "",
                                  p90_fct_ms=float(np.quantile(values, .90) * 1000) if len(values) else "",
                                  p95_fct_ms=float(np.quantile(values, .95) * 1000) if len(values) else "",
                                  p99_fct_ms=float(np.quantile(values, .99) * 1000) if len(values) else "",
                                  normalized_mean=float(values.mean() / baseline.mean()) if len(baseline) else ""))
    return summaries, series, wide


def style(algorithm):
    """Consistent colors identify policies; line styles identify timeout variants."""
    return dict(color=ALGORITHMS[algorithm][3], label=ALGORITHMS[algorithm][2],
                linestyle="--" if algorithm.endswith("_false") else "-",
                marker="s" if algorithm.endswith("_false") else "o", linewidth=2)


def save_figure(fig, directory, name):
    """Save both a crisp raster preview and a vector figure for a paper."""
    directory.mkdir(parents=True, exist_ok=True)
    fig.savefig(directory / (name + ".png"), dpi=180, bbox_inches="tight")
    fig.savefig(directory / (name + ".pdf"), bbox_inches="tight")
    plt.close(fig)


def plot_delivery(runs, directory, title):
    """Expose size failures before interpreting any performance advantage."""
    fig, axes = plt.subplots(1, 2, figsize=(15, 5), layout="constrained")
    labels = list(runs)
    left = np.zeros(len(labels))
    for status, color in zip(STATUSES, STATUS_COLORS):
        percentages = [100 * sum(r["size_status"] == status for r in runs[a]) / max(1, len(runs[a])) for a in labels]
        axes[0].barh(range(len(labels)), percentages, left=left, color=color, label=status)
        left += percentages
    axes[0].set(yticks=range(len(labels)), yticklabels=labels, xlim=(0, 100),
                xlabel="% of ALL input flows", title="Delivered payload vs requested bytes")
    # A single bad flow is too thin to see in a 6,000-flow stacked bar. Print
    # exact counts so small errors cannot disappear into a nearly green plot.
    for i, algorithm in enumerate(labels):
        rows = runs[algorithm]
        exact = sum(r["size_status"] == "exact" for r in rows)
        problems = sum(bool(r["issues"]) for r in rows)
        axes[0].text(2, i, f"Exact {exact:,}/{len(rows):,} | flagged rows {problems:,}",
                     va="center", fontsize=9,
                     bbox=dict(facecolor="white", edgecolor="none", alpha=.85, pad=2))
    axes[0].legend(loc="upper center", bbox_to_anchor=(.5, -.17), ncol=3, fontsize=9)
    for a in labels:
        errors = [r for r in runs[a] if r["byte_error"] != "" and r["byte_error"] != 0]
        if errors:
            axes[1].scatter([r["trace_index"] for r in errors],
                            [100 * r["byte_error"] / r["expected_bytes"] for r in errors],
                            s=20, alpha=.65, color=ALGORITHMS[a][3], label=a,
                            marker="x" if a.endswith("_false") else "o")
    axes[1].axhline(0, color="#555555", linewidth=1)
    axes[1].set(xlabel="Input trace index", ylabel="Payload error (%)",
                title="Negative = short; positive = excess")
    axes[1].set_yscale("symlog", linthresh=1)
    if axes[1].collections:
        axes[1].legend(fontsize=8)
    else:
        axes[1].text(.5, .5, "No observed nonzero byte errors\nMissing/unverified rows are shown at left",
                     ha="center", va="center", transform=axes[1].transAxes)
    fig.suptitle(title + " | payload audit", fontsize=14, weight="bold")
    save_figure(fig, directory, "payload_audit")


def plot_cdf(series, algorithms, directory, title, note):
    """Show the entire FCT distribution, including its tail, with a log time axis."""
    fig, axes = plt.subplots(1, 3, figsize=(16, 5), layout="constrained")
    for ax, group in zip(axes, ["all", "small", "large"]):
        count = 0
        for algorithm in algorithms:
            values = np.sort(series[algorithm, group]) * 1000
            count = len(values)
            if count:
                options = style(algorithm)
                options.pop("marker")
                ax.step(values, np.arange(1, count + 1) / count, where="post", **options)
        ax.set(title=f"{group.title()} flows | common n={count:,}", xlabel="FCT (ms)", ylabel="Cumulative fraction", ylim=(0, 1.02))
        ax.set_xscale("log")
    handles, labels = axes[0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(.5, -.02), ncol=3, fontsize=9)
    fig.suptitle(title + "\n" + note, fontsize=12)
    save_figure(fig, directory, "fct_cdf")


def plot_loads(metrics, algorithms, directory, title, note):
    """Mean, tail, and ECMP-normalized FCT without hard-coded y-axis clipping."""
    fig, axes = plt.subplots(3, 3, figsize=(17, 12), layout="constrained")
    for column, group in enumerate(["all", "small", "large"]):
        for row, (metric, ylabel) in enumerate([("mean_fct_ms", "Mean FCT (ms)"),
                                              ("p99_fct_ms", "99th percentile FCT (ms)"),
                                              ("normalized_mean", "Mean FCT / ECMP mean")]):
            ax = axes[row, column]
            for algorithm in algorithms:
                points = sorted([m for m in metrics if m["algorithm"] == algorithm and m["size_group"] == group], key=lambda m: m["load"])
                if points:
                    ax.plot([m["load"] / 10 for m in points],
                            [m[metric] if m[metric] != "" else np.nan for m in points], **style(algorithm))
            if metric == "normalized_mean":
                ax.axhline(1, color="#555555", linewidth=1, linestyle=":")
                ax.set_ylim(bottom=0)
            elif any(m[metric] != "" for m in metrics):
                ax.set_yscale("log")
            ax.set(xlabel="Offered load", ylabel=ylabel,
                   title={"all": "All flows", "small": "Small: <100 KiB", "large": "Large: >1 MiB"}[group])
    handles, labels = axes[0, 0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(.5, -.02), ncol=3, fontsize=10)
    fig.suptitle(title + "\n" + note, fontsize=14)
    save_figure(fig, directory, "fct_vs_load")


def run_analysis(args):
    """Audit each available run independently; compare only complete run sets."""
    plt.rcParams.update({"axes.spines.top": False, "axes.spines.right": False,
                         "axes.grid": True, "grid.alpha": .18, "font.size": 10,
                         "axes.axisbelow": True, "figure.facecolor": "white"})
    args.output.mkdir(parents=True, exist_ok=True)
    manifest, summary, metrics, global_issues = [], [], [], []
    metric = "flowmonitor_fct_s" if args.metric == "flowmonitor" else "application_fct_s"
    note = f"{args.metric} FCT; {args.cohort} common cohort; size checks do not change matching"
    for benchmark in args.benchmarks:
        # Remove only this tool's old comparison figures for selections being
        # regenerated. A newly missing/invalid run must not leave a stale plot.
        for extension in ("png", "pdf"):
            (args.output / benchmark / f"fct_vs_load.{extension}").unlink(missing_ok=True)
        for load in args.loads:
            stem = f"{benchmark}_load_{load}"
            directory = args.output / stem
            for filename in ("comparison_matched.csv", "fct_cdf.png", "fct_cdf.pdf",
                             "payload_audit.png", "payload_audit.pdf"):
                (directory / filename).unlink(missing_ok=True)
            for algorithm in args.algorithms:
                for suffix in ("_flow_checks.csv", "_issues.csv"):
                    (directory / (algorithm + suffix)).unlink(missing_ok=True)
            runs = {}
            problems = []
            try:
                flows = read_trace(args.input_dir / (stem + ".csv"))
            except (OSError, ValueError, KeyError, ArithmeticError) as error:
                global_issues.append(dict(workload=stem, algorithm="input", issue=str(error)))
                continue
            for algorithm in args.algorithms:
                family, suffix, _, _ = ALGORITHMS[algorithm]
                path = (args.conga_dir if family == "conga" else args.rtt_dir) / (stem + "_" + suffix + ".csv")
                entry = dict(workload=stem, algorithm=algorithm, path=str(path), status="missing")
                try:
                    runs[algorithm], issues = inspect_run(flows, path, args.servers_per_leaf)
                    entry["status"] = "read"
                    problems.extend(dict(workload=stem, algorithm=algorithm, issue=x) for x in issues)
                    rows = runs[algorithm]
                    write_csv(directory / (algorithm + "_flow_checks.csv"), rows)
                    write_csv(directory / (algorithm + "_issues.csv"), [r for r in rows if r["issues"]], list(rows[0]) if rows else None)
                    counts = Counter(r["size_status"] for r in rows)
                    summary.append(dict(workload=stem, benchmark=benchmark, load=load, algorithm=algorithm,
                                        input_flows=len(flows), matched=sum(r["matched"] for r in rows),
                                        verified=sum(r["verified"] for r in rows),
                                        issue_flows=sum(bool(r["issues"]) for r in rows), file_issues=len(issues),
                                        **{s: counts[s] for s in STATUSES}))
                except (OSError, ValueError, KeyError, ArithmeticError) as error:
                    entry["status"] = "missing" if not path.exists() else "invalid"
                    problems.append(dict(workload=stem, algorithm=algorithm, issue=str(error)))
                manifest.append(entry)
            global_issues.extend(problems)
            if runs:
                plot_delivery(runs, directory, f"{args.scenario.upper()} / {benchmark} / load {load / 10:g}")
            warning = bool(problems) or any(not r["verified"] for rows in runs.values() for r in rows)
            if set(runs) != set(args.algorithms):
                print(f"{stem}: audited {len(runs)}/{len(args.algorithms)} runs; comparison withheld (missing/invalid run)")
                continue
            stats, series, wide = summarize_comparison(flows, runs, args.algorithms, metric, args.cohort)
            for row in stats:
                row.update(benchmark=benchmark, load=load, validation_warning=warning)
            metrics.extend(stats)
            write_csv(directory / "comparison_matched.csv", wide)
            banner = "VALIDATION ISSUES: inspect flow_checks before drawing conclusions" if warning else "All input payloads verified"
            plot_cdf(series, args.algorithms, directory, f"{benchmark} / load {load / 10:g} / {banner}", note)
            print(f"{stem}: {len(wide):,}/{len(flows):,} flows in comparison; {banner}")
        points = [m for m in metrics if m["benchmark"] == benchmark]
        if points:
            banner = "VALIDATION ISSUES in one or more runs" if any(m["validation_warning"] for m in points) else "All plotted runs passed validation"
            plot_loads(points, args.algorithms, args.output / benchmark,
                       f"{args.scenario.upper()} / {benchmark} / {banner}", note)
    write_csv(args.output / "validation_summary.csv", summary)
    write_csv(args.output / "run_manifest.csv", manifest)
    write_csv(args.output / "file_issues.csv", global_issues)
    write_csv(args.output / "metrics.csv", metrics)
    config = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
    (args.output / "configuration.json").write_text(json.dumps(config, indent=2) + "\n")
    failed = bool(global_issues) or not summary or any(s["issue_flows"] or s["file_issues"] for s in summary)
    print(f"Reports: {args.output}\nValidation: {'ISSUES FOUND' if failed else 'PASS'}")
    return 2 if failed and args.strict else 0


def main(argv=None):
    """Command-line interface, also callable from the companion notebook."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["sym", "asym"], default="sym")
    parser.add_argument("--input-dir", type=Path, default=TRACE_DIR)
    parser.add_argument("--conga-dir", type=Path)
    parser.add_argument("--rtt-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--benchmarks", nargs="+", default=BENCHMARKS)
    parser.add_argument("--loads", type=int, nargs="+")
    parser.add_argument("--algorithms", choices=list(ALGORITHMS), nargs="+", default=DEFAULT_ALGORITHMS)
    parser.add_argument("--servers-per-leaf", type=int, default=4)
    parser.add_argument("--metric", choices=["flowmonitor", "application"], default="flowmonitor")
    parser.add_argument("--cohort", choices=["matched", "verified"], default="matched")
    parser.add_argument("--strict", action="store_true", help="Exit 2 after writing reports if any run is missing, invalid, incomplete or unverified")
    args = parser.parse_args(argv)
    args.loads = args.loads or list(range(1, 10 if args.scenario == "sym" else 8))
    if args.servers_per_leaf <= 0 or len(set(args.algorithms)) != len(args.algorithms):
        parser.error("Positive servers-per-leaf and unique algorithms are required")
    args.conga_dir = args.conga_dir or TRACE_DIR.parent / f"conga_{args.scenario}_8_hosts_v9(prob1)"
    args.rtt_dir = args.rtt_dir or TRACE_DIR.parent / f"rtt_{args.scenario}_8_hosts_v9(prob1)"
    args.output = args.output or TRACE_DIR / f"analysis_checked_{args.scenario}_{args.metric}_{args.cohort}"
    return run_analysis(args)


if __name__ == "__main__":
    raise SystemExit(main())
