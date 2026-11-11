#!/usr/bin/env python3
"""Generate the deterministic stage 3 datasets (funcionarios and departamentos).

The same seed and parameters always produce byte-identical CSV files, so large
files do not need to be versioned: anyone can regenerate them and check the
SHA-256 listed in results/datasets.csv.

funcionarios: id, department_id, salary, age
  - department_id uniform in [1, departments] (or outside it, see --matched-ratio)
  - salary uniform in [1000.00, 11000.00), so "salary > t" keeps about
    (11000 - t) / 10000 of the rows (t = 5000 keeps ~60%)
departamentos: id, name, region
"""
import argparse
import csv
import random

SALARY_MIN = 1000.0
SALARY_MAX = 11000.0


def threshold_for(selectivity):
    """Salary threshold that keeps approximately `selectivity` of the rows."""
    return SALARY_MAX - selectivity * (SALARY_MAX - SALARY_MIN)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--employees", type=int, default=10_000)
    parser.add_argument("--departments", type=int, default=1_000)
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--matched-ratio", type=float, default=1.0,
                        help="fraction of employees whose department_id exists in departamentos")
    parser.add_argument("--employee-file", default="funcionarios_generated.csv")
    parser.add_argument("--department-file", default=None,
                        help="also write departamentos with ids 1..departments")
    args = parser.parse_args()
    if args.employees < 0 or args.departments <= 0 or not 0 <= args.matched_ratio <= 1:
        raise SystemExit("invalid cardinality or matched ratio")

    if args.department_file:
        with open(args.department_file, "w", newline="", encoding="utf-8") as file:
            writer = csv.writer(file, lineterminator="\n")
            writer.writerow(["id", "name", "region"])
            for department_id in range(1, args.departments + 1):
                writer.writerow([department_id, f"department_{department_id}",
                                 f"region_{department_id % 5}"])

    rng = random.Random(args.seed)
    randint = rng.randint
    rand = rng.random
    span = SALARY_MAX - SALARY_MIN
    with open(args.employee_file, "w", encoding="utf-8", buffering=1 << 20) as file:
        file.write("id,department_id,salary,age\n")
        lines = []
        for employee_id in range(1, args.employees + 1):
            if args.matched_ratio >= 1.0 or rand() < args.matched_ratio:
                department_id = randint(1, args.departments)
            else:
                department_id = args.departments + randint(1, args.departments)
            salary = SALARY_MIN + rand() * span
            lines.append(f"{employee_id},{department_id},{salary:.2f},{randint(18, 70)}\n")
            if len(lines) == 100_000:
                file.writelines(lines)
                lines.clear()
        file.writelines(lines)


if __name__ == "__main__":
    main()
