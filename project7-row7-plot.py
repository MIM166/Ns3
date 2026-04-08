#!/usr/bin/env python3
import csv
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parent
OUTDIR = ROOT / "project7-row7-plots"


def load_rows(csv_path: pathlib.Path):
    with csv_path.open(newline="") as f:
        return list(csv.DictReader(f))


def sort_key(value: str):
    try:
        return float(value)
    except ValueError:
        return value


def write_dat(rows, x_col: str, y_col: str, dat_path: pathlib.Path):
    points = []
    for row in rows:
        if not row.get(x_col) or not row.get(y_col):
            continue
        points.append((float(row[x_col]), float(row[y_col])))
    points.sort(key=lambda pair: pair[0])

    with dat_path.open("w", newline="") as f:
        for x_val, y_val in points:
            f.write(f"{x_val} {y_val}\n")


def write_plot(dat_path: pathlib.Path,
               pdf_path: pathlib.Path,
               title: str,
               xlabel: str,
               ylabel: str,
               color: str,
               plt_path: pathlib.Path):
    script = f"""
set terminal pdfcairo enhanced color font "Helvetica,11" size 6in,4in
set output "{pdf_path}"
set title "{title}"
set xlabel "{xlabel}"
set ylabel "{ylabel}"
set grid
set key off
set border linewidth 1.2
set tics out
plot "{dat_path}" using 1:2 with linespoints lw 2.2 pt 7 ps 1.1 lc rgb "{color}"
"""
    plt_path.write_text(script.lstrip())
    subprocess.run(["gnuplot", str(plt_path)], check=True)


def write_comparison_plot(wired_dat_path: pathlib.Path,
                          wireless_dat_path: pathlib.Path,
                          pdf_path: pathlib.Path,
                          title: str,
                          xlabel: str,
                          ylabel: str,
                          plt_path: pathlib.Path):
    script = f"""
set terminal pdfcairo enhanced color font "Helvetica,11" size 6in,4in
set output "{pdf_path}"
set title "{title}"
set xlabel "{xlabel}"
set ylabel "{ylabel}"
set grid
set key top right
set border linewidth 1.2
set tics out
plot "{wired_dat_path}" using 1:2 with linespoints lw 2.2 pt 7 ps 1.1 lc rgb "#1f77b4" title "Wired Bolt", \\
     "{wireless_dat_path}" using 1:2 with linespoints lw 2.2 pt 5 ps 1.1 lc rgb "#d62728" title "Static 802.15.4 Bolt"
"""
    plt_path.write_text(script.lstrip())
    subprocess.run(["gnuplot", str(plt_path)], check=True)


def generate_metric_plots(rows, spec, sweep_specs, metrics, prefix):
    for metric_key, metric_label, color in metrics:
        for sweep_name, x_col, xlabel in sweep_specs:
            filtered = [row for row in rows if row["sweep"] == sweep_name]
            if not filtered:
                continue

            dat_path = OUTDIR / f"{prefix}-{metric_key}-{sweep_name}.dat"
            plt_path = OUTDIR / f"{prefix}-{metric_key}-{sweep_name}.plt"
            pdf_path = OUTDIR / f"{prefix}-{metric_key}-{sweep_name}.pdf"

            write_dat(filtered, x_col, metric_key, dat_path)
            title = f"{spec} {metric_label} vs {xlabel}"
            write_plot(dat_path, pdf_path, title, xlabel, metric_label, color, plt_path)


def generate_comparison_plots(wired_rows, wireless_rows, sweep_specs, metrics):
    for metric_key, metric_label in metrics:
        for sweep_name, x_col, xlabel in sweep_specs:
            wired_filtered = [row for row in wired_rows if row["sweep"] == sweep_name]
            wireless_filtered = [row for row in wireless_rows if row["sweep"] == sweep_name]
            if not wired_filtered or not wireless_filtered:
                continue

            wired_dat_path = OUTDIR / f"compare-wired-{metric_key}-{sweep_name}.dat"
            wireless_dat_path = OUTDIR / f"compare-lrwpan-static-{metric_key}-{sweep_name}.dat"
            plt_path = OUTDIR / f"compare-{metric_key}-{sweep_name}.plt"
            pdf_path = OUTDIR / f"compare-{metric_key}-{sweep_name}.pdf"

            write_dat(wired_filtered, x_col, metric_key, wired_dat_path)
            write_dat(wireless_filtered, x_col, metric_key, wireless_dat_path)

            title = f"Wired vs Static 802.15.4 {metric_label} vs {xlabel}"
            write_comparison_plot(
                wired_dat_path,
                wireless_dat_path,
                pdf_path,
                title,
                xlabel,
                metric_label,
                plt_path,
            )


def main():
    OUTDIR.mkdir(exist_ok=True)

    wired_csv = ROOT / "project7-row7-wired.csv"
    wireless_csv = ROOT / "project7-row7-lrwpan-static.csv"

    if not wired_csv.exists() or not wireless_csv.exists():
        missing = [str(path.name) for path in (wired_csv, wireless_csv) if not path.exists()]
        raise SystemExit(f"Missing CSV input(s): {', '.join(missing)}")

    wired_rows = load_rows(wired_csv)
    wireless_rows = load_rows(wireless_csv)

    wired_sweeps = [
        ("nodes", "num_nodes", "Nodes"),
        ("flows", "num_flows", "Flows"),
        ("pps", "packets_per_second", "Packets/s"),
    ]
    wireless_sweeps = [
        ("nodes", "num_nodes", "Nodes"),
        ("flows", "num_flows", "Flows"),
        ("pps", "packets_per_second", "Packets/s"),
        ("area", "coverage_multiplier", "Coverage Multiplier (x Tx_range)"),
    ]

    wired_metrics = [
        ("throughput_mbps", "Throughput (Mbps)", "#1b9e77"),
        ("avg_delay_ms", "End-to-End Delay (ms)", "#d95f02"),
        ("pdr", "Packet Delivery Ratio", "#7570b3"),
        ("drop_ratio", "Packet Drop Ratio", "#e7298a"),
    ]
    wireless_metrics = [
        ("throughput_mbps", "Throughput (Mbps)", "#1b9e77"),
        ("avg_delay_ms", "End-to-End Delay (ms)", "#d95f02"),
        ("pdr", "Packet Delivery Ratio", "#7570b3"),
        ("drop_ratio", "Packet Drop Ratio", "#e7298a"),
        ("energy_consumption_j", "Energy Consumption (J)", "#66a61e"),
    ]
    comparison_metrics = [
        ("throughput_mbps", "Throughput (Mbps)"),
        ("avg_delay_ms", "End-to-End Delay (ms)"),
        ("pdr", "Packet Delivery Ratio"),
        ("drop_ratio", "Packet Drop Ratio"),
    ]
    comparison_sweeps = [
        ("nodes", "num_nodes", "Nodes"),
        ("flows", "num_flows", "Flows"),
        ("pps", "packets_per_second", "Packets/s"),
    ]

    generate_metric_plots(wired_rows, "Wired Bolt", wired_sweeps, wired_metrics, "wired")
    generate_metric_plots(
        wireless_rows,
        "Static 802.15.4 Bolt",
        wireless_sweeps,
        wireless_metrics,
        "lrwpan-static",
    )
    generate_comparison_plots(wired_rows, wireless_rows, comparison_sweeps, comparison_metrics)

    print(f"Plots written to {OUTDIR}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"gnuplot failed: {exc}", file=sys.stderr)
        raise
