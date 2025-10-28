#!/usr/bin/env python3
import argparse
import subprocess
#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path
from typing import Dict, List, Tuple


def main() -> int:
    ap = argparse.ArgumentParser(description="Plot threads vs ops/s from a CSV produced by lock_test --csv-file.")
    ap.add_argument("--csv-in", required=True, help="Path to CSV produced by lock_test")
    ap.add_argument("--output", default=None, help="Path to save figure (png). If omitted, will show interactively")

    args = ap.parse_args()

    path = Path(args.csv_in)
    if not path.exists():
        print(f"CSV not found: {path}", file=sys.stderr)
        return 2

    # Read CSV
    results: Dict[str, List[Tuple[int, float]]] = {}
    with path.open('r') as f:
        header = f.readline().strip().split(',')
        try:
            idx_lock = header.index('lock')
            idx_threads = header.index('threads')
            idx_ops_s = header.index('ops_s')
            idx_task = header.index('task') if 'task' in header else None
        except ValueError:
            print("CSV missing required columns: lock,threads,ops_s", file=sys.stderr)
            return 2
        task_name = None
        for line in f:
            if not line.strip():
                continue
            cols = [c.strip() for c in line.strip().split(',')]
            try:
                lk = cols[idx_lock]
                t = int(float(cols[idx_threads]))
                y = float(cols[idx_ops_s])
                if idx_task is not None and task_name is None:
                    task_name = cols[idx_task]
            except Exception:
                continue
            results.setdefault(lk, []).append((t, y))

    if not results:
        print("No data found in CSV", file=sys.stderr)
        return 2

    # Plot
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed. Install with 'pip install matplotlib'", file=sys.stderr)
        return 3

    plt.figure(figsize=(8, 5))
    all_threads = set()
    for lk, series in results.items():
        series = sorted(series, key=lambda p: p[0])
        xs = [t for (t, _) in series]
        ys = [y for (_, y) in series]
        all_threads.update(xs)
        plt.plot(xs, ys, marker='o', label=lk)
    plt.xlabel('Threads')
    plt.ylabel('Ops/s')
    title = f"threads vs ops/s"
    if task_name:
        title = f"{task_name} {title}"
    plt.title(title)
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.xticks(sorted(all_threads))

    if args.output:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        plt.savefig(out_path, dpi=150, bbox_inches='tight')
        print(f"Saved figure to {out_path}")
    else:
        plt.show()

    return 0


if __name__ == "__main__":
    sys.exit(main())
    sys.exit(main())
