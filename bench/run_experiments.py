#!/usr/bin/env python3
"""Run the stage 3 experiments (Group A, materialized model).

Generates the deterministic datasets (if missing) under data/generated/,
records their size and SHA-256 in results/datasets.csv, runs miniquery_bench
for every configuration and writes every measured repetition to
results/raw/<experiment>.csv. The machine configuration goes to
results/ambiente.txt. Use --only to run a subset (e.g. --only cardinalidade).
"""
import argparse
import csv
import hashlib
import os
import platform
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data", "generated")
RESULTS = os.path.join(ROOT, "results")
RAW = os.path.join(RESULTS, "raw")
BENCH = os.path.join(ROOT, "miniquery_bench")
SEED = 2026
REPEATS = 5
REFERENCE_THRESHOLD = 5000.0  # WHERE f.salary > 5000 (keeps ~60%)
SELECTIVITY_THRESHOLDS = {0.01: 10900.0, 0.10: 10000.0, 0.50: 6000.0, 0.90: 2000.0}
CARDINALITIES = [10_000, 100_000, 1_000_000, 5_000_000, 10_000_000]
BUILD_SIZES = [100, 1_000, 10_000, 100_000]
DEFAULT_DEPARTMENTS = 1_000
SELECTIVITY_EMPLOYEES = 1_000_000
BUILD_EMPLOYEES = 1_000_000
# Above this many key comparisons a Nested Loop run takes minutes: no warm-up run.
NLJ_NO_WARMUP_COMPARISONS = 5e9


def human(n):
    for unit, size in (("m", 1_000_000), ("k", 1_000)):
        if n >= size and n % size == 0:
            return f"{n // size}{unit}"
    return str(n)


def employee_file(employees, departments):
    return os.path.join(DATA, f"funcionarios_{human(employees)}_d{human(departments)}.csv")


def department_file(departments):
    return os.path.join(DATA, f"departamentos_{human(departments)}.csv")


def generate(employees, departments):
    emp, dep = employee_file(employees, departments), department_file(departments)
    if not os.path.exists(dep):
        subprocess.run([sys.executable, os.path.join(ROOT, "bench", "generate_data.py"),
                        "--employees", "0", "--departments", str(departments),
                        "--employee-file", os.devnull, "--department-file", dep], check=True)
    if not os.path.exists(emp):
        subprocess.run([sys.executable, os.path.join(ROOT, "bench", "generate_data.py"),
                        "--employees", str(employees), "--departments", str(departments),
                        "--seed", str(SEED), "--employee-file", emp], check=True)
    return emp, dep


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return "indisponível"


def write_environment():
    lscpu = command_output(["lscpu"])
    model = next((l.split(":", 1)[1].strip() for l in lscpu.splitlines() if l.startswith("Model name")), "?")
    cpus = next((l.split(":", 1)[1].strip() for l in lscpu.splitlines() if l.startswith("CPU(s)")), "?")
    caches = [l.strip() for l in lscpu.splitlines() if "cache" in l.lower()]
    with open("/proc/meminfo") as file:
        meminfo = dict(line.split(":", 1) for line in file)
    os_release = {}
    if os.path.exists("/etc/os-release"):
        with open("/etc/os-release") as file:
            for line in file:
                if "=" in line:
                    key, value = line.rstrip().split("=", 1)
                    os_release[key] = value.strip('"')
    lines = [
        f"data_execucao={time.strftime('%Y-%m-%d %H:%M %z')}",
        f"cpu={model}",
        f"cpus_logicas={cpus}",
        *[f"cache: {c}" for c in caches],
        f"memoria_total={meminfo['MemTotal'].strip()}",
        f"memoria_disponivel_no_inicio={meminfo['MemAvailable'].strip()}",
        f"so={os_release.get('PRETTY_NAME', platform.platform())}",
        f"kernel={platform.release()}",
        f"virtualizacao={command_output(['systemd-detect-virt']) or 'desconhecida'}",
        f"compilador={command_output(['gcc', '--version']).splitlines()[0]}",
        "flags=-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 (Makefile, alvo miniquery_bench)",
        f"python={platform.python_version()}",
        f"repeticoes_medidas={REPEATS} (mais 1 de aquecimento descartada, exceto Nested Loop com > {NLJ_NO_WARMUP_COMPARISONS:.0e} comparações)",
        f"semente_gerador={SEED}",
        "execucao=sequencial, 1 thread, uma configuração por processo",
    ]
    with open(os.path.join(RESULTS, "ambiente.txt"), "w") as file:
        file.write("\n".join(lines) + "\n")


def experiments():
    for n in CARDINALITIES:
        yield "cardinalidade", human(n), n, DEFAULT_DEPARTMENTS, REFERENCE_THRESHOLD
    for sel, threshold in SELECTIVITY_THRESHOLDS.items():
        yield "seletividade", f"{int(sel * 100)}pct", SELECTIVITY_EMPLOYEES, DEFAULT_DEPARTMENTS, threshold
    for d in BUILD_SIZES:
        yield "build", human(d), BUILD_EMPLOYEES, d, REFERENCE_THRESHOLD


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", choices=["cardinalidade", "seletividade", "build"], action="append")
    parser.add_argument("--algorithms", default="hash,nested_loop")
    args = parser.parse_args()
    os.makedirs(DATA, exist_ok=True)
    os.makedirs(RAW, exist_ok=True)
    subprocess.run(["make", "-C", ROOT, "miniquery_bench"], check=True, stdout=subprocess.DEVNULL)
    write_environment()

    plan = [e for e in experiments() if not args.only or e[0] in args.only]
    datasets = {}
    for _, _, n, d, _ in plan:
        emp, dep = generate(n, d)
        datasets[emp] = (n, "funcionarios", d)
        datasets[dep] = (d, "departamentos", d)
    with open(os.path.join(RESULTS, "datasets.csv"), "w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["arquivo", "tabela", "linhas", "departamentos", "bytes", "sha256", "semente"])
        for path, (rows, table, d) in sorted(datasets.items()):
            writer.writerow([os.path.relpath(path, ROOT), table, rows, d, os.path.getsize(path),
                             sha256(path), SEED if table == "funcionarios" else "-"])

    algorithms = args.algorithms.split(",")
    for experiment in dict.fromkeys(e[0] for e in plan):
        out_path = os.path.join(RAW, f"{experiment}.csv")
        header_written = False
        with open(out_path, "w") as out:
            for exp, label, n, d, threshold in plan:
                if exp != experiment:
                    continue
                emp, dep = employee_file(n, d), department_file(d)
                for algorithm in algorithms:
                    est = n * (SALARY_KEEP(threshold)) * d
                    warmup = 0 if algorithm == "nested_loop" and est > NLJ_NO_WARMUP_COMPARISONS else 1
                    cmd = [BENCH, experiment, label, emp, dep, algorithm, str(threshold),
                           str(REPEATS), str(warmup)]
                    if not header_written:
                        cmd.append("header")
                        header_written = True
                    started = time.time()
                    print(f"[{time.strftime('%H:%M:%S')}] {experiment} {label} {algorithm} ...", flush=True)
                    proc = subprocess.run(cmd, capture_output=True, text=True)
                    if proc.returncode != 0:
                        print(f"  FALHOU ({proc.returncode}): {proc.stderr.strip()}", flush=True)
                        continue
                    out.write(proc.stdout)
                    out.flush()
                    print(f"  ok em {time.time() - started:.1f}s", flush=True)


def SALARY_KEEP(threshold):
    return max(0.0, min(1.0, (11000.0 - threshold) / 10000.0))


if __name__ == "__main__":
    main()
