#!/usr/bin/env python3
"""Summarize measured booked-voyage policies; zeros in milestone fields mean not reached."""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path
import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('csv', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows = list(csv.DictReader(args.csv.open()))
    groups = defaultdict(list)
    for row in rows:
        groups[(row['policy'], int(row['fleet_cap']), int(row['attention_minutes']), int(row['novice']))].append(row)
    summary = []
    for key, members in sorted(groups.items()):
        policy, cap, attention, novice = key
        record = dict(policy=policy, fleet_cap=cap, attention_minutes=attention, novice=bool(novice), companies=len(members))
        for metric in ['cash','delivered','ships','regions','min_cash','dispatches','partials','refused','relief','pending']:
            values = np.array([float(r[metric]) for r in members])
            record[metric] = dict(zip(['p10','median','p90'], map(float, np.quantile(values, [.1,.5,.9]))))
        for metric in ['first_arrival_minutes','second_ship_minutes','first_region_minutes','all_regions_minutes']:
            values = np.array([float(r[metric]) for r in members])
            reached = values[values > 0]
            record[metric] = dict(reached=len(reached), fraction=len(reached)/len(values),
                median=float(np.median(reached)) if len(reached) else None,
                p10=float(np.quantile(reached,.1)) if len(reached) else None,
                p90=float(np.quantile(reached,.9)) if len(reached) else None)
        for metric in ['failures','reconciliation','stall']:
            record[metric] = sum(int(r[metric]) for r in members)
        summary.append(record)
    result = dict(companies=len(rows), simulated_hours=sum(int(r['horizon_minutes'])/60 for r in rows),
        csv_sha256=hashlib.sha256(args.csv.read_bytes()).hexdigest(),
        failures=sum(int(r['failures']) for r in rows), reconciliation=sum(int(r['reconciliation']) for r in rows),
        cohorts=summary)
    (args.out/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    lines = ['# Revised voyage economy: simulation results', '',
        f"{result['companies']:,} companies; {result['simulated_hours']:,.0f} simulated hours. "
        f"{result['failures']} state/quote failures; {result['reconciliation']} cash reconciliation failures.", '',
        'Milestone times are medians among companies that reached them. Reach counts remain explicit; an unreached milestone is never averaged as zero.', '',
        '| Policy | Fleet cap | Check every | Partial-load policy | Cash p10 / median / p90 | Second ship, min (reached) | First region, min (reached) | Regions, median |',
        '|---|---:|---:|---|---:|---:|---:|---:|']
    def milestone(value, total):
        return f"{value['median']:.1f} ({value['reached']}/{total})" if value['median'] is not None else f"— (0/{total})"
    for r in summary:
        cash = r['cash']
        lines.append(f"| {r['policy']} | {r['fleet_cap']} | {r['attention_minutes']} min | {'Sometimes' if r['novice'] else 'Full offers'} | "
            f"{cash['p10']:,.0f} / {cash['median']:,.0f} / {cash['p90']:,.0f} | "
            f"{milestone(r['second_ship_minutes'],r['companies'])} | {milestone(r['first_region_minutes'],r['companies'])} | {r['regions']['median']:.0f} |")
    (args.out/'summary.md').write_text('\n'.join(lines)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.size':10, 'axes.spines.top':False, 'axes.spines.right':False})
    policies = ['freight','trade','mixed']
    colors = ['#27485d','#a9652c']
    fig, axes = plt.subplots(3, 2, figsize=(12, 10), constrained_layout=True)
    for row_index, interval in enumerate([2,10,30]):
        for col, metric in enumerate(['cash','delivered']):
            ax = axes[row_index,col]
            for offset, (cap, color) in enumerate(zip([3,6], colors)):
                for novice, style in [(False,'o'),(True,'x')]:
                    selected = [next(r for r in summary if r['policy']==p and r['fleet_cap']==cap and
                        r['attention_minutes']==interval and r['novice']==novice) for p in policies]
                    x = np.arange(3)+(offset-.5)*.22 + (.04 if novice else -.04)
                    med = np.array([r[metric]['median'] for r in selected])
                    low = med - np.array([r[metric]['p10'] for r in selected])
                    high = np.array([r[metric]['p90'] for r in selected])-med
                    label = f"Cap {cap}, {'partial loads' if novice else 'full offers'}"
                    ax.errorbar(x,med,yerr=[low,high],fmt=style,color=color,capsize=3,label=label,alpha=.8)
            ax.set_xticks(range(3), ['Freight','Booked trade','Mixed'])
            ax.set_title(f"Check every {interval} min · {'Treasury' if metric=='cash' else 'Cargo delivered'}")
            ax.set_ylim(bottom=0)
            ax.grid(axis='y', alpha=.18)
    axes[0,0].legend(fontsize=8, loc='best')
    fig.suptitle('Crosswinds · six-hour voyage policies\nMedians and 10–90% ranges; scripted decisions, not predicted player outcomes', fontsize=15)
    fig.savefig(args.out/'voyage-economy.png',dpi=170)
    plt.close(fig)
    print(json.dumps({k:v for k,v in result.items() if k!='cohorts'},indent=2))


if __name__ == '__main__':
    main()
