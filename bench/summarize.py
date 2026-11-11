#!/usr/bin/env python3
"""Summarize results/raw/*.csv (median of the measured repetitions).

Writes results/resumo_<experimento>.csv, results/tabelas.md and the PNG graphs
in docs/img/. Requires matplotlib.
"""
import csv
import os
import statistics
from collections import OrderedDict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "results", "raw")
OUT = os.path.join(ROOT, "results")
IMG = os.path.join(ROOT, "docs", "img")
ALGO_NAME = {"hash": "Hash Join", "nested_loop": "Nested Loop Join"}
COLORS = {"hash": "#1f77b4", "nested_loop": "#d62728"}
MIB = 1024.0 * 1024.0
TIME_FIELDS = ["scan_f_ms", "scan_d_ms", "filter_ms", "build_time_ms", "probe_time_ms",
               "join_total_ms", "project_ms", "group_by_ms", "operators_ms", "wall_ms"]


def load(experiment):
    path = os.path.join(RAW, f"{experiment}.csv")
    if not os.path.exists(path):
        return []
    with open(path) as file:
        return list(csv.DictReader(file))


def summarize(rows):
    groups = OrderedDict()
    for row in rows:
        groups.setdefault((row["label"], row["algorithm"]), []).append(row)
    summary = []
    for (label, algorithm), runs in groups.items():
        first = runs[0]
        for field in ("rows_out", "filtered_rows", "groups", "avg_checksum", "key_comparisons"):
            values = {r[field] for r in runs}
            if len(values) != 1:
                raise SystemExit(f"{label}/{algorithm}: {field} differs between runs: {values}")
        item = OrderedDict()
        for key in ("experiment", "label", "model", "algorithm", "employees", "departments",
                    "threshold", "filtered_rows", "selectivity", "build_rows", "probe_rows",
                    "rows_out", "groups", "key_comparisons", "hash_table_entries",
                    "hash_table_bytes", "join_memory_bytes", "base_bytes", "scan_bytes",
                    "filter_bytes", "join_output_bytes", "project_bytes", "group_by_bytes",
                    "intermediate_rows", "intermediate_bytes", "peak_live_bytes", "avg_checksum"):
            item[key] = first[key]
        item["runs"] = len(runs)
        for field in TIME_FIELDS:
            item[field] = statistics.median(float(r[field]) for r in runs)
        walls = [float(r["wall_ms"]) for r in runs]
        item["wall_min_ms"] = min(walls)
        item["wall_max_ms"] = max(walls)
        item["peak_rss_kb"] = max(int(r["peak_rss_kb"]) for r in runs)
        summary.append(item)
    return summary


def write_csv(experiment, summary):
    if not summary:
        return
    with open(os.path.join(OUT, f"resumo_{experiment}.csv"), "w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        for item in summary:
            writer.writerow({k: (f"{v:.3f}" if isinstance(v, float) else v) for k, v in item.items()})


def check_equivalence(summary):
    """Both algorithms must return the same rows and the same groups."""
    by_label = OrderedDict()
    for item in summary:
        by_label.setdefault(item["label"], {})[item["algorithm"]] = item
    problems = []
    for label, algos in by_label.items():
        if len(algos) == 2:
            a, b = algos["hash"], algos["nested_loop"]
            if a["rows_out"] != b["rows_out"] or abs(float(a["avg_checksum"]) - float(b["avg_checksum"])) > 1e-3:
                problems.append(label)
    return problems


def ms(value):
    return f"{value:,.1f}".replace(",", "X").replace(".", ",").replace("X", ".")


def num(value):
    return f"{int(value):,}".replace(",", ".")


def ms3(value):
    return f"{value:,.3f}".replace(",", "X").replace(".", ",").replace("X", ".")


def kib(value):
    return f"{float(value) / 1024.0:,.1f}".replace(",", "X").replace(".", ",").replace("X", ".")


def mib(value):
    return f"{float(value) / MIB:,.1f}".replace(",", "X").replace(".", ",").replace("X", ".")


def table_main(summary, first_col, first_value):
    lines = [f"| {first_col} | Algoritmo | Linhas no JOIN (probe) | BUILD | rows_out | Comparações de chave | "
             "BUILD (ms) | PROBE (ms) | JOIN total (ms) | Consulta, mediana (ms) | min–max (ms) | "
             "Memória do JOIN (MiB) | Pico de intermediários (MiB) | Pico RSS (MiB) |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for item in summary:
        lines.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | **{}** | {}–{} | {} | {} | {} |".format(
            first_value(item), ALGO_NAME[item["algorithm"]], num(item["probe_rows"]),
            num(item["build_rows"]), num(item["rows_out"]), num(item["key_comparisons"]),
            ms3(item["build_time_ms"]), ms(item["probe_time_ms"]), ms(item["join_total_ms"]),
            ms(item["wall_ms"]), ms(item["wall_min_ms"]), ms(item["wall_max_ms"]),
            mib(item["join_memory_bytes"]), mib(item["peak_live_bytes"]),
            mib(item["peak_rss_kb"] * 1024)))
    return "\n".join(lines)


def table_operators(summary, first_col, first_value):
    lines = [f"| {first_col} | Algoritmo | SCAN f | SCAN d | FILTER | JOIN | PROJECT | GROUP BY | Soma dos operadores | Parcela do JOIN |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for item in summary:
        share = 100.0 * item["join_total_ms"] / item["operators_ms"] if item["operators_ms"] else 0.0
        lines.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {}% |".format(
            first_value(item), ALGO_NAME[item["algorithm"]], ms(item["scan_f_ms"]), ms(item["scan_d_ms"]),
            ms(item["filter_ms"]), ms(item["join_total_ms"]), ms(item["project_ms"]),
            ms(item["group_by_ms"]), ms(item["operators_ms"]), f"{share:.0f}"))
    return "\n".join(lines)


def table_intermediate(summary, first_col, first_value):
    lines = [f"| {first_col} | SCAN f+d (MiB) | FILTER (linhas / MiB) | JOIN saída (linhas / MiB) | Tabela hash (KiB) | PROJECT (MiB) | GROUP BY (grupos) | Total materializado (MiB) |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for item in summary:
        if item["algorithm"] != "hash":
            continue
        lines.append("| {} | {} | {} / {} | {} / {} | {} | {} | {} | {} |".format(
            first_value(item), mib(item["scan_bytes"]), num(item["filtered_rows"]), mib(item["filter_bytes"]),
            num(item["rows_out"]), mib(item["join_output_bytes"]), kib(item["hash_table_bytes"]),
            mib(item["project_bytes"]), num(item["groups"]), mib(item["intermediate_bytes"])))
    return "\n".join(lines)


def plot_lines(summary, xkey, xlabel, title, path, logx=True, logy=True, xfmt=None):
    fig, ax = plt.subplots(figsize=(7, 4.2))
    for algorithm in ("nested_loop", "hash"):
        items = [i for i in summary if i["algorithm"] == algorithm]
        if not items:
            continue
        xs = [float(i[xkey]) for i in items]
        ys = [i["wall_ms"] for i in items]
        ax.plot(xs, ys, marker="o", color=COLORS[algorithm], label=ALGO_NAME[algorithm])
        for x, y in zip(xs, ys):
            ax.annotate(ms(y), (x, y), textcoords="offset points", xytext=(4, 6), fontsize=7)
    if logx:
        ax.set_xscale("log")
    if logy:
        ax.set_yscale("log")
    if xfmt:
        ax.set_xticks([float(i[xkey]) for i in summary if i["algorithm"] == "hash"])
        ax.set_xticklabels([xfmt(i) for i in summary if i["algorithm"] == "hash"])
    ax.set_xlabel(xlabel)
    ax.set_ylabel("tempo da consulta, mediana (ms)")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_operators(summary, label_fn, title, path):
    fields = [("scan_f_ms", "SCAN f"), ("scan_d_ms", "SCAN d"), ("filter_ms", "FILTER"),
              ("build_time_ms", "JOIN: BUILD"), ("probe_time_ms", "JOIN: PROBE"),
              ("project_ms", "PROJECT"), ("group_by_ms", "GROUP BY")]
    labels = [f"{label_fn(i)}\n{'HJ' if i['algorithm'] == 'hash' else 'NLJ'}" for i in summary]
    fig, ax = plt.subplots(figsize=(max(7, 0.9 * len(summary)), 4.5))
    bottom = [0.0] * len(summary)
    for field, name in fields:
        values = [i[field] for i in summary]
        ax.bar(labels, values, bottom=bottom, label=name)
        bottom = [b + v for b, v in zip(bottom, values)]
    ax.set_yscale("log")
    ax.set_ylabel("tempo de CPU por operador, mediana (ms, escala log)")
    ax.set_title(title)
    ax.legend(fontsize=7, ncol=4)
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_operator_share(summary, label_fn, title, path):
    fields = [("scan_f_ms", "SCAN f"), ("scan_d_ms", "SCAN d"), ("filter_ms", "FILTER"),
              ("build_time_ms", "JOIN: BUILD"), ("probe_time_ms", "JOIN: PROBE"),
              ("project_ms", "PROJECT"), ("group_by_ms", "GROUP BY")]
    items = [i for i in summary if i["algorithm"] == "hash"]
    labels = [label_fn(i) for i in items]
    fig, ax = plt.subplots(figsize=(7, 4.2))
    bottom = [0.0] * len(items)
    for field, name in fields:
        values = [100.0 * i[field] / sum(i[f] for f, _ in fields) for i in items]
        ax.bar(labels, values, bottom=bottom, label=name)
        bottom = [b + v for b, v in zip(bottom, values)]
    ax.set_ylabel("% do tempo de CPU dos operadores")
    ax.set_ylim(0, 100)
    ax.set_title(title)
    ax.legend(fontsize=7, ncol=4, loc="lower center", bbox_to_anchor=(0.5, -0.32))
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_memory(summary, xkey, xlabel, title, path, xfmt):
    items = [i for i in summary if i["algorithm"] == "hash"]
    fields = [("scan_bytes", "SCAN f+d"), ("filter_bytes", "FILTER"), ("join_output_bytes", "JOIN (saída)"),
              ("hash_table_bytes", "tabela hash"), ("project_bytes", "PROJECT"), ("group_by_bytes", "GROUP BY")]
    labels = [xfmt(i) for i in items]
    width = 0.12
    fig, ax = plt.subplots(figsize=(7.5, 4.4))
    positions = list(range(len(items)))
    for k, (field, name) in enumerate(fields):
        values = [max(float(i[field]) / MIB, 1e-3) for i in items]
        ax.bar([p + (k - 2.5) * width for p in positions], values, width=width, label=name)
    ax.plot(positions, [float(i["peak_live_bytes"]) / MIB for i in items], "k--o",
            label="pico de intermediários vivos")
    ax.plot(positions, [i["peak_rss_kb"] / 1024.0 for i in items], "m:s", label="pico RSS do processo")
    ax.set_xticks(positions)
    ax.set_xticklabels(labels)
    ax.set_yscale("log")
    ax.set_xlabel(xlabel)
    ax.set_ylabel("MiB (escala log)")
    ax.set_title(title)
    ax.legend(fontsize=7, ncol=2)
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_build(summary, path):
    items = [i for i in summary if i["algorithm"] == "hash"]
    xs = [int(i["build_rows"]) for i in items]
    fig, ax1 = plt.subplots(figsize=(7, 4.2))
    ax1.plot(xs, [i["build_time_ms"] for i in items], "o-", label="BUILD (ms)")
    ax1.plot(xs, [i["probe_time_ms"] for i in items], "s-", label="PROBE (ms)")
    nlj = [i for i in summary if i["algorithm"] == "nested_loop"]
    if nlj:
        ax1.plot([int(i["build_rows"]) for i in nlj], [i["join_total_ms"] for i in nlj], "^-",
                 color=COLORS["nested_loop"], label="Nested Loop JOIN (ms)")
    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.set_xlabel("linhas da relação de BUILD (departamentos)")
    ax1.set_ylabel("tempo de CPU (ms, escala log)")
    ax2 = ax1.twinx()
    ax2.bar(xs, [float(i["hash_table_bytes"]) / MIB for i in items], width=[x * 0.35 for x in xs],
            alpha=0.25, color="gray", label="tabela hash (MiB)")
    ax2.set_yscale("log")
    ax2.set_ylabel("tamanho da tabela hash (MiB, log)")
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, fontsize=8, loc="upper left")
    ax1.set_title("Tamanho da relação de BUILD (1M funcionários, salary > 5000)")
    ax1.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def main():
    os.makedirs(IMG, exist_ok=True)
    sections = []
    all_problems = []
    card = summarize(load("cardinalidade"))
    sel = summarize(load("seletividade"))
    build = summarize(load("build"))
    for name, summary in (("cardinalidade", card), ("seletividade", sel), ("build", build)):
        write_csv(name, summary)
        all_problems += [f"{name}:{p}" for p in check_equivalence(summary)]

    def card_label(i):
        return num(i["employees"])

    def sel_label(i):
        return f"{float(i['selectivity']) * 100:.1f}% (> {float(i['threshold']):.0f})"

    def build_label(i):
        return num(i["build_rows"])

    if card:
        sections.append("### Cardinalidade (|departamentos| = 1.000, `salary > 5000`)\n\n" +
                        table_main(card, "funcionarios", card_label) + "\n\n#### Tempo por operador\n\n" +
                        table_operators(card, "funcionarios", card_label) + "\n\n#### Dados intermediários (Hash Join)\n\n" +
                        table_intermediate(card, "funcionarios", card_label))
        plot_lines(card, "employees", "linhas de funcionarios (escala log)",
                   "Cardinalidade: tempo da consulta completa", os.path.join(IMG, "cardinalidade_tempo.png"))
        plot_memory(card, "employees", "linhas de funcionarios", "Cardinalidade: memória materializada por operador",
                    os.path.join(IMG, "cardinalidade_memoria.png"), lambda i: human_label(i["employees"]))
        plot_operator_share(card, lambda i: human_label(i["employees"]),
                            "Cardinalidade: parcela de cada operador (Hash Join)",
                            os.path.join(IMG, "cardinalidade_operadores.png"))
    if sel:
        sections.append("### Seletividade (1.000.000 funcionários, |departamentos| = 1.000)\n\n" +
                        table_main(sel, "seletividade", sel_label) + "\n\n#### Tempo por operador\n\n" +
                        table_operators(sel, "seletividade", sel_label) + "\n\n#### Dados intermediários (Hash Join)\n\n" +
                        table_intermediate(sel, "seletividade", sel_label))
        for i in sel:
            i["sel_pct"] = float(i["selectivity"]) * 100
        plot_lines(sel, "sel_pct", "fração de funcionários que passa no FILTER (%)",
                   "Seletividade: tempo da consulta completa", os.path.join(IMG, "seletividade_tempo.png"),
                   logx=False)
        plot_operators(sel, lambda i: f"{i['sel_pct']:.0f}%", "Seletividade: tempo por operador",
                       os.path.join(IMG, "seletividade_operadores.png"))
        plot_memory(sel, "sel_pct", "seletividade do FILTER", "Seletividade: memória materializada por operador",
                    os.path.join(IMG, "seletividade_memoria.png"), lambda i: f"{i['sel_pct']:.0f}%")
    if build:
        sections.append("### Tamanho da relação de BUILD (1.000.000 funcionários, `salary > 5000`)\n\n" +
                        table_main(build, "linhas no BUILD", build_label) + "\n\n#### Tempo por operador\n\n" +
                        table_operators(build, "linhas no BUILD", build_label))
        plot_build(build, os.path.join(IMG, "build_tamanho.png"))
        plot_lines(build, "build_rows", "linhas de departamentos (escala log)",
                   "Tamanho do BUILD: tempo da consulta completa", os.path.join(IMG, "build_tempo.png"))
    header = ("<!-- gerado por bench/summarize.py a partir de results/raw/*.csv; mediana de "
              "5 repetições -->\n\n")
    if all_problems:
        header += f"**ATENÇÃO: resultados divergentes entre algoritmos em {all_problems}**\n\n"
    with open(os.path.join(OUT, "tabelas.md"), "w") as file:
        file.write(header + "\n\n".join(sections) + "\n")
    print("divergências:", all_problems or "nenhuma")


def human_label(value):
    value = int(value)
    if value >= 1_000_000:
        return f"{value // 1_000_000}M"
    if value >= 1_000:
        return f"{value // 1_000}k"
    return str(value)


if __name__ == "__main__":
    main()
