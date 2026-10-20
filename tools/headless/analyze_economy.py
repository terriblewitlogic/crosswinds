#!/usr/bin/env python3
"""Summarize the actual-engine Monte Carlo CSV; requires numpy/matplotlib.

No missing milestone is imputed as success. Reach rates use all companies;
milestone time quantiles are conditional on reaching that milestone by 6 hours.
The 10–90% bands describe variation between simulated companies, not confidence
intervals for a population of human players.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

LABELS = {
    "first_contracts": "First fitting contract",
    "best_contracts": "Best contract return",
    "wants_trader": "Makes → Wants",
    "prudent_mixed": "Prudent mixed",
    "eager_fleet": "Eager fleet",
    "casual_mixed": "Mixed, every 2 min",
    "margin_trader": "Margin trader",
}
COLORS = ["#7b817e", "#175b8a", "#b3583c", "#267c64", "#a06b08", "#8362aa", "#bd668d"]


def distribution(values):
    a = np.asarray(values, dtype=float)
    if not len(a):
        return None
    q = np.quantile(a, [.1, .5, .9])
    return {"n": len(a), "p10": float(q[0]), "median": float(q[1]),
            "p90": float(q[2]), "mean": float(np.mean(a)),
            "min": float(np.min(a)), "max": float(np.max(a))}


def rate(flags):
    n = len(flags)
    k = sum(bool(x) for x in flags)
    p = k / n
    z = 1.95996398454
    divisor = 1 + z*z/n
    mid = (p + z*z/(2*n)) / divisor
    half = z * np.sqrt(p*(1-p)/n + z*z/(4*n*n)) / divisor
    return {"count": k, "n": n, "fraction": p,
            "wilson95": [float(max(0, mid-half)), float(min(1, mid+half))]}


def load(path):
    with path.open(newline="") as source:
        rows = [{k: v if k == "policy" else int(v) for k, v in row.items()}
                for row in csv.DictReader(source)]
    seen = set()
    campaign_checkpoints = {}
    for row in rows:
        key = row["policy"], row["seed"], row["real_minutes"]
        if key in seen:
            raise ValueError(f"Duplicate checkpoint {key}")
        seen.add(key)
        campaign_checkpoints.setdefault((row["policy"], row["seed"]), []).append(row["real_minutes"])
        row["net_treasury"] = row["cash"] - row["debt"]
        # A condition-adjusted asset proxy, not a liquidation value: there is no
        # ship resale API. Office/chart/charter values are intentionally excluded.
        row["asset_proxy"] = row["net_treasury"] + row["cargo_paid_value"] + row["fleet_condition_value"]
        row["market_cash_flow"] = row["market_net_receipts"] - row["market_spend"]
    for (policy, seed), checkpoints in campaign_checkpoints.items():
        if sorted(checkpoints) != [30, 60, 120, 240, 360]:
            raise ValueError(f"Incomplete campaign {policy}/{seed}: {checkpoints}")
    return rows


def summarize(rows):
    result = {}
    for policy in dict.fromkeys(r["policy"] for r in rows):
        cohort = [r for r in rows if r["policy"] == policy]
        final = [r for r in cohort if r["real_minutes"] == 360]
        summary = {"n": len(final), "checkpoints": {}}
        for name in ["cash", "debt", "net_treasury", "asset_proxy", "ships", "regions", "delivered_units",
                     "contract_revenue", "operating_costs", "capital_spending", "repair_spend", "incident_net",
                     "market_profit", "market_cash_flow", "market_spend", "market_gross_receipts",
                     "max_debt", "relief_claims", "dispatches", "bought_units", "sold_units"]:
            summary[name] = distribution([r[name] for r in final])
        summary["ever_relief"] = rate([r["relief_claims"] > 0 for r in final])
        summary["ever_30_min_stall"] = rate([r["max_no_progress_world_minutes"] >= 18000 for r in final])
        summary["debt_at_end"] = rate([r["debt"] > 0 for r in final])
        summary["net_treasury_below_start"] = rate([r["net_treasury"] < 500 for r in final])
        summary["invariant_failures"] = sum(r["invariant_failures"] for r in final)
        summary["cash_reconciliation_errors"] = sum(r.get("cash_reconciliation_error", 0) != 0 for r in cohort)
        summary["max_cash_reconciliation_error"] = max(abs(r.get("max_cash_reconciliation_error", 0)) for r in cohort)
        summary["milestones"] = {}
        for name in ["first_delivery", "second_ship", "stage3", "first_charter"]:
            vals = [r[name + "_world_minute"] for r in final]
            summary["milestones"][name] = {"reached": rate([v > 0 for v in vals]),
                                            "real_minutes_if_reached": distribution([v/600 for v in vals if v > 0])}
        for minute in [30, 60, 120, 240, 360]:
            checkpoint = [r for r in cohort if r["real_minutes"] == minute]
            summary["checkpoints"][str(minute)] = {
                name: distribution([r[name] for r in checkpoint])
                for name in ["cash", "debt", "net_treasury", "asset_proxy", "delivered_units", "ships"]}
        result[policy] = summary
    return result


def make_plot(rows, summary, destination):
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False})
    fig, axes = plt.subplots(2, 2, figsize=(15, 10))
    fig.patch.set_facecolor("#faf9f5")
    policies = list(summary)
    for policy, color in zip(policies, COLORS):
        s = summary[policy]
        minutes = [30, 60, 120, 240, 360]
        values = [s["checkpoints"][str(m)]["net_treasury"] for m in minutes]
        axes[0, 0].plot(minutes, [v["median"] for v in values], color=color, label=LABELS[policy], lw=2)
        axes[0, 0].fill_between(minutes, [v["p10"] for v in values], [v["p90"] for v in values], color=color, alpha=.08)
        for axis, milestone in [(axes[0, 1], "second_ship"), (axes[1, 0], "first_charter")]:
            final = [r for r in rows if r["policy"] == policy and r["real_minutes"] == 360]
            times = np.array([r[milestone + "_world_minute"]/600 for r in final])
            grid = np.linspace(0, 360, 361)
            percent = [100*np.sum((times > 0) & (times <= t))/len(times) for t in grid]
            axis.plot(grid, percent, color=color, lw=2)
    axes[0, 0].axhline(500, color="#444444", linestyle=":", lw=1)
    axes[0, 0].set(title="Treasury after debt: median and 10–90% range", ylabel="Coins minus relief debt", xlabel="Real minutes at 1×")
    axes[0, 0].legend(fontsize=8, loc="upper left")
    axes[0, 1].set(title="Companies reaching a second ship", ylabel="% of all companies", xlabel="Real minutes at 1×", ylim=(-2, 103))
    axes[1, 0].set(title="Companies opening their first new region", ylabel="% of all companies", xlabel="Real minutes at 1×", ylim=(-2, 103))
    y = np.arange(len(policies))
    axes[1, 1].barh(y-.18, [100*summary[p]["ever_relief"]["fraction"] for p in policies], height=.34,
                    color="#bc6650", label="Claimed relief")
    axes[1, 1].barh(y+.18, [100*summary[p]["ever_30_min_stall"]["fraction"] for p in policies], height=.34,
                    color="#8a8992", label="Stalled ≥30 min")
    axes[1, 1].set(yticks=y, yticklabels=[LABELS[p] for p in policies], xlabel="% of all companies", xlim=(0, 105), title="Economic difficulty within 6 hours")
    axes[1, 1].invert_yaxis()
    axes[1, 1].legend(loc="lower right", fontsize=8)
    for axis in axes.flat:
        axis.grid(axis="y", alpha=.15)
        axis.set_axisbelow(True)
    companies = sum(s["n"] for s in summary.values())
    fig.suptitle(f"CROSSWINDS economy audit · {companies:,} simulated companies · 6 hours each", fontsize=19, x=.06, ha="left")
    fig.text(.06, .02, "Actual game engine; legal unlocks; fresh 500-coin starts. Bots describe strategies, not human-player success rates.\n"
             "Cash can be invested in ships and charters. Stalls include deliberate waiting for a qualifying trade; they are not all softlocks.", fontsize=9, color="#555555")
    fig.tight_layout(rect=(.03, .065, 1, .94), h_pad=2, w_pad=3)
    fig.savefig(destination, dpi=140, facecolor=fig.get_facecolor())
    plt.close(fig)


def markdown(summary):
    lines = ["# Monte Carlo numerical summary", "", "Each policy starts with the same set of seeds and 500 coins. Horizon: 360 real minutes at 1×.", "",
             "| Strategy | Companies | Final cash, median (P10–P90) | Final debt, median | Relief | ≥30m stall | Second ship | First new region |", 
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for p, s in summary.items():
        c = s["cash"]
        pct = lambda r: f'{100*r["fraction"]:.1f}%'
        lines.append(f'| {LABELS[p]} | {s["n"]:,} | {c["median"]:,.0f} ({c["p10"]:,.0f}–{c["p90"]:,.0f}) | '
                     f'{s["debt"]["median"]:,.0f} | {pct(s["ever_relief"])} | {pct(s["ever_30_min_stall"])} | '
                     f'{pct(s["milestones"]["second_ship"]["reached"])} | {pct(s["milestones"]["first_charter"]["reached"])} |')
    lines += ["", "Time quantiles below include only companies reaching the milestone; the preceding reach rates retain failures/censoring.", "",
              "| Strategy | First delivery, median min | Second ship, median min | First new region, median min | Delivered units, median | Final ships, median |", 
              "|---|---:|---:|---:|---:|---:|"]
    for p, s in summary.items():
        def time(name):
            d = s["milestones"][name]["real_minutes_if_reached"]
            return f'{d["median"]:.1f}' if d else "—"
        lines.append(f'| {LABELS[p]} | {time("first_delivery")} | {time("second_ship")} | {time("first_charter")} | '
                     f'{s["delivered_units"]["median"]:.0f} | {s["ships"]["median"]:.0f} |')
    lines += ["", "The JSON includes quantiles, observed minima/maxima and Wilson 95% intervals for event rates. These intervals apply to the simulator's seed distribution, not to human players.",
              "", f'Invariant failures: **{sum(s["invariant_failures"] for s in summary.values())}**.', ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    rows = load(args.csv)
    summary = summarize(rows)
    args.out.mkdir(parents=True, exist_ok=True)
    payload = {"csv_sha256": hashlib.sha256(args.csv.read_bytes()).hexdigest(), "policies": summary}
    (args.out / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")
    (args.out / "summary.md").write_text(markdown(summary))
    make_plot(rows, summary, args.out / "economy-overview.png")
    print(markdown(summary))


if __name__ == "__main__":
    main()
