#!/usr/bin/env python3
"""Построение графиков по результатам экспериментов (data/raw_results.csv, data/summary.csv).

Строит 4 графика и сохраняет PNG в data/plots/ (флаг --show — ещё и показать на экране):
  1. plot1_total_time_vs_clients.png  — среднее время запроса (total_ms) от числа клиентов, 1 КБ и 64 КБ, усы = разброс между запусками;
  2. plot2_throughput_vs_clients.png  — пропускная способность (Мбит/с) от числа клиентов, 1 КБ и 64 КБ;
  3. plot3_total_ms_boxplot.png       — распределение total_ms отдельных запросов по конфигурациям (boxplot);
  4. plot4_time_breakdown.png         — разбиение среднего времени запроса: connect / ожидание ответа (ttfb) / приём остального.

Режим проверки самого скрипта:  python scripts/plot_results.py --sample
  берёт data/sample_data_for_plot_check.csv (ВЫДУМАННЫЕ данные, НЕ результаты эксперимента!) и пишет
  картинки в data/plots_sample/ с пометкой «ДЕМО-ДАННЫЕ» на каждом графике.
Требуется: pip install matplotlib
"""
import argparse
import csv
import os
import statistics
import sys
from collections import OrderedDict, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_csv_skip_comments(path):
    with open(path, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.startswith("#")]
    return list(csv.DictReader(lines))


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return 0.0


def load_real(data_dir):
    sp, rp = os.path.join(data_dir, "summary.csv"), os.path.join(data_dir, "raw_results.csv")
    if not (os.path.exists(sp) and os.path.exists(rp)):
        sys.exit("Нет %s или %s. Сначала выполните scripts/run_experiments.py." % (sp, rp))
    runs = [{"config": r["config"], "label": r["label"], "clients": int(r["clients"]), "resource": r["resource"],
             "run_id": int(r["run_id"]), "avg_total_ms": num(r["avg_total_ms"]),
             "throughput_mbit_s": num(r["throughput_mbit_s"])} for r in read_csv_skip_comments(sp)]
    reqs = [{"config": r["config"], "total_ms": num(r["total_ms"]), "connect_ms": num(r["connect_ms"]),
             "ttfb_ms": num(r["ttfb_ms"]), "ok": r["error"] == "" and r["status"] == "200"} for r in read_csv_skip_comments(rp)]
    return runs, reqs


def load_sample(data_dir):
    rows = read_csv_skip_comments(os.path.join(data_dir, "sample_data_for_plot_check.csv"))
    groups = defaultdict(list)
    for r in rows:
        groups[(r["config"], r["run_id"])].append(r)
    runs = []
    for (cfg, run), rs in groups.items():
        wall = num(rs[0]["wall_ms"])
        clients = int(rs[0]["clients"])
        runs.append({"config": cfg, "label": "%s: %d кл., %s" % (cfg, clients, rs[0]["resource"]), "clients": clients,
                     "resource": rs[0]["resource"], "run_id": int(run),
                     "avg_total_ms": statistics.mean(num(r["total_ms"]) for r in rs),
                     "throughput_mbit_s": sum(num(r["bytes_recv"]) for r in rs) * 8 / (wall / 1000.0) / 1e6})
    reqs = [{"config": r["config"], "total_ms": num(r["total_ms"]), "connect_ms": num(r["connect_ms"]),
             "ttfb_ms": num(r["ttfb_ms"]), "ok": True} for r in rows]
    return runs, reqs


def mean_std(vals):
    return statistics.mean(vals), (statistics.stdev(vals) if len(vals) > 1 else 0.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=os.path.join(ROOT, "data"))
    ap.add_argument("--out", default="")
    ap.add_argument("--show", action="store_true", help="показать графики на экране")
    ap.add_argument("--sample", action="store_true", help="проверка скрипта на ДЕМО-данных (не результаты эксперимента)")
    a = ap.parse_args()

    import matplotlib
    if not a.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.size": 12, "axes.titlesize": 14, "axes.labelsize": 12, "legend.fontsize": 11,
                         "axes.grid": True, "grid.alpha": 0.35})

    runs, reqs = load_sample(a.data) if a.sample else load_real(a.data)
    out = a.out or os.path.join(a.data, "plots_sample" if a.sample else "plots")
    os.makedirs(out, exist_ok=True)
    demo = "\n[ДЕМО-ДАННЫЕ, НЕ РЕЗУЛЬТАТ ЭКСПЕРИМЕНТА]" if a.sample else ""

    # Группировка: ресурс -> число клиентов -> значения по запускам
    resources = list(OrderedDict.fromkeys(r["resource"] for r in runs))
    clients_list = sorted(set(r["clients"] for r in runs))
    by = defaultdict(list)
    for r in runs:
        by[(r["resource"], r["clients"])].append(r)

    # ---------- График 1: среднее время запроса vs число клиентов ----------
    fig, ax = plt.subplots(figsize=(8, 5.5))
    width = 0.8 / max(1, len(resources))
    for i, res in enumerate(resources):
        xs, ms, ss = [], [], []
        for j, c in enumerate(clients_list):
            if (res, c) in by:
                m, s = mean_std([r["avg_total_ms"] for r in by[(res, c)]])
                xs.append(j + (i - (len(resources) - 1) / 2) * width)
                ms.append(m)
                ss.append(s)
        ax.bar(xs, ms, width * 0.95, yerr=ss, capsize=5, label="Ресурс " + res)
    ax.set_xticks(range(len(clients_list)))
    ax.set_xticklabels([str(c) for c in clients_list])
    ax.set_xlabel("Число одновременных клиентов")
    ax.set_ylabel("Среднее время запроса total_ms, мс")
    ax.set_title("Время выполнения запроса от числа клиентов" + demo, fontsize=12)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, "plot1_total_time_vs_clients.png"), dpi=150)

    # ---------- График 2: пропускная способность vs число клиентов ----------
    fig, ax = plt.subplots(figsize=(8, 5.5))
    for res in resources:
        cs, ms, ss = [], [], []
        for c in clients_list:
            if (res, c) in by:
                m, s = mean_std([r["throughput_mbit_s"] for r in by[(res, c)]])
                cs.append(c)
                ms.append(m)
                ss.append(s)
        ax.errorbar(cs, ms, yerr=ss, marker="o", capsize=5, linewidth=2, label="Ресурс " + res)
    ax.set_xticks(clients_list)
    ax.set_xlabel("Число одновременных клиентов")
    ax.set_ylabel("Пропускная способность, Мбит/с")
    ax.set_title("Пропускная способность от числа клиентов" + demo, fontsize=12)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, "plot2_throughput_vs_clients.png"), dpi=150)

    # ---------- График 3: распределение total_ms по конфигурациям ----------
    cfgs = list(OrderedDict.fromkeys(r["config"] for r in runs))
    label_of = {r["config"]: r["label"] for r in runs}
    data = [[q["total_ms"] for q in reqs if q["config"] == c and q["ok"]] for c in cfgs]
    fig, ax = plt.subplots(figsize=(9, 5.5))
    ax.boxplot(data, showfliers=True, flierprops={"markersize": 3, "alpha": 0.4})
    ax.set_xticks(range(1, len(cfgs) + 1))
    ax.set_xticklabels([label_of[c].replace(": ", ":\n") for c in cfgs])
    ax.set_xlabel("Конфигурация эксперимента")
    ax.set_ylabel("Время отдельного запроса total_ms, мс")
    ax.set_yscale("log")
    ax.set_title("Распределение времени запросов (boxplot, лог. шкала)" + demo, fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "plot3_total_ms_boxplot.png"), dpi=150)

    # ---------- График 4: разбиение времени запроса ----------
    conn, wait, rest = [], [], []
    for c in cfgs:
        qs = [q for q in reqs if q["config"] == c and q["ok"]]
        mc = statistics.mean(q["connect_ms"] for q in qs)
        mt = statistics.mean(q["ttfb_ms"] for q in qs)
        tot = statistics.mean(q["total_ms"] for q in qs)
        conn.append(mc)
        wait.append(mt)
        rest.append(max(0.0, tot - mc - mt))
    fig, ax = plt.subplots(figsize=(9, 5.5))
    xs = range(len(cfgs))
    ax.bar(xs, conn, label="connect_ms (установка TCP)")
    ax.bar(xs, wait, bottom=conn, label="ttfb_ms (ожидание первого байта ответа)")
    ax.bar(xs, rest, bottom=[c + w for c, w in zip(conn, wait)], label="приём остального ответа + закрытие")
    ax.set_xticks(list(xs))
    ax.set_xticklabels([label_of[c].replace(": ", ":\n") for c in cfgs])
    ax.set_xlabel("Конфигурация эксперимента")
    ax.set_ylabel("Среднее время, мс")
    ax.set_title("Из чего складывается время запроса" + demo, fontsize=12)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, "plot4_time_breakdown.png"), dpi=150)

    print("Графики сохранены в", out)
    for f in sorted(os.listdir(out)):
        print("  ", f)
    if a.show:
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
