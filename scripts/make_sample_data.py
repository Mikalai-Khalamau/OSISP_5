#!/usr/bin/env python3
"""Создаёт data/sample_data_for_plot_check.csv — ВЫДУМАННЫЕ данные, только чтобы проверить, что plot_results.py
рисует графики. Это НЕ результаты эксперимента и их нельзя использовать в отчёте.
Реальные данные (raw_results.csv, summary.csv, table2.csv) создаёт только scripts/run_experiments.py."""
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rng = random.Random(1)
rows = []
for cfg, clients, res, size in [("A", 1, "1 КБ", 1250), ("B", 1, "64 КБ", 65780), ("C", 3, "1 КБ", 1250), ("D", 3, "64 КБ", 65780)]:
    for run in (1, 2, 3):
        base = (0.9 if size < 5000 else 1.6) * (1.0 if clients == 1 else 1.7)
        wall = 0.0
        for c in range(1, clients + 1):
            tot = 0.0
            for i in range(1, 51):
                connect = abs(rng.gauss(0.25, 0.05))
                ttfb = abs(rng.gauss(0.2, 0.05))
                total = connect + ttfb + abs(rng.gauss(base, 0.15))
                tot += total
                rows.append([cfg, clients, res, run, 0, c, i, 200, size, round(connect, 3), round(ttfb, 3), round(total, 3)])
            wall = max(wall, tot)
        for r in rows:
            if r[0] == cfg and r[3] == run:
                r[4] = round(wall * 1.02, 3)
path = os.path.join(ROOT, "data", "sample_data_for_plot_check.csv")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w", encoding="utf-8", newline="") as f:
    f.write("# ВНИМАНИЕ: ЭТО ВЫДУМАННЫЕ ДЕМО-ДАННЫЕ ДЛЯ ПРОВЕРКИ plot_results.py --sample. НЕ РЕЗУЛЬТАТЫ ЭКСПЕРИМЕНТА!\n")
    f.write("config,clients,resource,run_id,wall_ms,client_id,req_no,status,bytes_recv,connect_ms,ttfb_ms,total_ms\n")
    for r in rows:
        f.write(",".join(str(x) for x in r) + "\n")
print("записано", path, len(rows), "строк (ДЕМО-данные)")
