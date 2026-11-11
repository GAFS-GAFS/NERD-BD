#!/usr/bin/env python3
"""Generate deterministic stage-3 CSV inputs without versioning large files."""
import argparse
import csv
import random

parser = argparse.ArgumentParser()
parser.add_argument("--employees", type=int, default=10_000)
parser.add_argument("--departments", type=int, default=100)
parser.add_argument("--seed", type=int, default=2026)
parser.add_argument("--selectivity", type=float, default=0.5,
                    help="fraction of salaries generated above 5000")
parser.add_argument("--matched-ratio", type=float, default=1.0,
                    help="fraction of employees with a department key present in departments")
parser.add_argument("--employee-file", default="funcionarios_generated.csv")
parser.add_argument("--department-file", default="departamentos_generated.csv")
args = parser.parse_args()
if args.employees < 0 or args.departments <= 0 or not 0 <= args.selectivity <= 1 or not 0 <= args.matched_ratio <= 1:
    raise SystemExit("invalid cardinality, selectivity, or matched ratio")

rng = random.Random(args.seed)
with open(args.department_file, "w", newline="", encoding="utf-8") as file:
    writer = csv.writer(file)
    writer.writerow(["id", "name", "region"])
    for department_id in range(1, args.departments + 1):
        writer.writerow([department_id, f"department_{department_id}", f"region_{department_id % 5}"])
with open(args.employee_file, "w", newline="", encoding="utf-8") as file:
    writer = csv.writer(file)
    writer.writerow(["id", "department_id", "salary", "age"])
    for employee_id in range(1, args.employees + 1):
        department_id = (rng.randint(1, args.departments) if rng.random() < args.matched_ratio
                         else args.departments + rng.randint(1, args.departments))
        salary = rng.uniform(5000.01, 12000) if rng.random() < args.selectivity else rng.uniform(2500, 5000)
        writer.writerow([employee_id, department_id, f"{salary:.2f}", rng.randint(18, 70)])
