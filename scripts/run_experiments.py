#!/usr/bin/env python3
"""Автоматизация экспериментов лабораторной №5 (вариант 27). Только стандартная библиотека, Python 3.8+.

Конфигурации по заданию:  A = 1 клиент × 1 КБ,  B = 1 клиент × 64 КБ,  C = 3 клиента × 1 КБ,  D = 3 клиента × 64 КБ.
Каждая конфигурация выполняется --runs раз (RUN_ID 1..N); каждый клиент делает --count запросов.
Клиенты — ОТДЕЛЬНЫЕ процессы (client.exe), стартуют один за другим без пауз (т.е. практически одновременно).

Результаты (в --out, по умолчанию data/):
  raw_results.csv   — все запросы всех клиентов (+ колонка config)
  summary.csv       — по каждому запуску: время запуска (мс), средние метрики, ошибки, байты, пропускная способность
  table2.csv        — «Таблица 2»: Конфигурация | Запуск 1, мс | ... | Среднее, мс
  table3.csv        — успешные запросы, ошибки, переданные байты, пропускная способность
  experiment_meta.json — STUDENT_ID, SEED, порт, параметры
Время запуска (wall_ms) = от старта первого клиента до завершения последнего (включает запуск процессов).
Пропускная способность = все принятые байты (заголовки + тело) всех клиентов / wall_ms.

Дополнительный эксперимент «от преподавателя»:
  python scripts/run_experiments.py --only-custom --custom-clients 5 --custom-path /64kb.html --custom-count 100
(результаты пишутся в custom_*.csv, основные файлы не затираются)
"""
import argparse
import csv
import json
import os
import subprocess
import statistics
import sys
import time
import socket
import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IS_WIN = os.name == "nt"
STUDENT_ID = 113
SEED = 1000 + STUDENT_ID * 97          # 11961
BASE_PORT = 20000 + STUDENT_ID         # 20113

# (имя, число клиентов, путь, подпись ресурса)
CONFIGS = [("A", 1, "/1kb.txt", "1 КБ"), ("B", 1, "/64kb.txt", "64 КБ"),
           ("C", 3, "/1kb.txt", "1 КБ"), ("D", 3, "/64kb.txt", "64 КБ")]


def config_label(name, clients, resource):
    word = "клиент" if clients == 1 else "клиента"
    return "%s: %d %s, %s" % (name, clients, word, resource)


class Server:
    def __init__(self, exe, host, port, www):
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if IS_WIN else 0
        self.p = subprocess.Popen([exe, "--host", host, "--port", str(port), "--www", www, "--quiet", "--id", "0"],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  creationflags=flags)
        self.host, self.port = host, port

    def wait_ready(self, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if self.p.poll() is not None:
                return False
            try:
                socket.create_connection((self.host, self.port), timeout=0.5).close()
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def stop(self):
        """CTRL_BREAK_EVENT -> штатное завершение; если не успел — terminate/kill. Висящих процессов не остаётся."""
        if self.p.poll() is None:
            try:
                if IS_WIN:
                    import signal
                    self.p.send_signal(signal.CTRL_BREAK_EVENT)
                else:
                    self.p.terminate()
                self.p.wait(timeout=15)
            except Exception:
                try:
                    self.p.terminate()
                    self.p.wait(timeout=5)
                except Exception:
                    self.p.kill()
                    self.p.wait()
        return self.p.returncode


def run_config(a, name, clients, path, run_id, tmp_dir):
    """Один запуск конфигурации. Возвращает (wall_ms, список строк-словарей запросов)."""
    procs, csvs = [], []
    for k in range(clients):
        f = os.path.join(tmp_dir, "%s_run%d_client%d.csv" % (name, run_id, k + 1))
        if os.path.exists(f):
            os.remove(f)
        csvs.append(f)
    t0 = time.perf_counter()
    for k in range(clients):
        procs.append(subprocess.Popen(
            [a.client_exe, "--host", a.host, "--port", str(a.port), "--id", str(k + 1), "--path", path,
             "--count", str(a.count_for(name)), "--timeout", str(a.timeout), "--run-id", str(run_id),
             "--csv", csvs[k], "--quiet", "--expect", "200"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
    outs = [p.communicate()[0].decode("utf-8", "replace") for p in procs]
    wall_ms = (time.perf_counter() - t0) * 1000.0
    rows = []
    for k, f in enumerate(csvs):
        if procs[k].returncode != 0:
            print("  ! клиент %d вернул код %d: %s" % (k + 1, procs[k].returncode, outs[k].strip()[-300:]))
        if not os.path.exists(f):
            print("  ! нет CSV клиента %d" % (k + 1))
            continue
        with open(f, newline="", encoding="utf-8") as fh:
            rows.extend(csv.DictReader(fh))
    return wall_ms, rows


def fnum(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0


def summarize(name, clients, resource, run_id, wall_ms, rows):
    ok = [r for r in rows if r["error"] == "" and r["status"] == "200"]
    errors = [r for r in rows if r["error"] != "" or r["status"] != "200"]
    timeouts = [r for r in rows if "WSAETIMEDOUT" in r["error"]]
    recv = sum(int(r["bytes_recv"]) for r in rows)
    sent = sum(int(r["bytes_sent"]) for r in rows)
    mean = lambda key: statistics.mean([fnum(r[key]) for r in rows]) if rows else 0.0
    sec = wall_ms / 1000.0
    return {
        "config": name, "label": config_label(name, clients, resource), "clients": clients, "resource": resource,
        "run_id": run_id, "wall_ms": round(wall_ms, 3), "requests": len(rows), "ok": len(ok), "errors": len(errors),
        "timeouts": len(timeouts), "bytes_sent": sent, "bytes_recv": recv,
        "avg_total_ms": round(mean("total_ms"), 4), "avg_connect_ms": round(mean("connect_ms"), 4),
        "avg_ttfb_ms": round(mean("ttfb_ms"), 4),
        "throughput_mbit_s": round(recv * 8 / sec / 1e6, 4) if sec > 0 else 0.0,
        "throughput_MB_s": round(recv / sec / 1e6, 4) if sec > 0 else 0.0,
    }


def write_csv(path, header, rows):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def print_table(header, rows):
    cells = [[str(c) for c in header]] + [[str(c) for c in r] for r in rows]
    widths = [max(len(r[i]) for r in cells) for i in range(len(header))]
    for n, r in enumerate(cells):
        print("  ".join(c.ljust(widths[i]) for i, c in enumerate(r)))
        if n == 0:
            print("  ".join("-" * w for w in widths))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(ROOT, "bin", "x64", "Release"), help="папка с server.exe и client.exe")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=BASE_PORT, help="порт сервера (по умолчанию BASE_PORT=20000+STUDENT_ID)")
    ap.add_argument("--count", type=int, default=200, help="запросов на каждого клиента в запуске (по умолчанию 200)")
    ap.add_argument("--runs", type=int, default=3, help="число запусков каждой конфигурации (минимум 3 по заданию)")
    ap.add_argument("--timeout", type=int, default=5000, help="таймаут клиента, мс")
    ap.add_argument("--warmup", type=int, default=20, help="прогревочных запросов перед измерениями (не записываются)")
    ap.add_argument("--student-id", type=int, default=STUDENT_ID)
    ap.add_argument("--seed", type=int, default=SEED, help="SEED (записывается в experiment_meta.json)")
    ap.add_argument("--www", default=os.path.join(ROOT, "www"))
    ap.add_argument("--out", default=os.path.join(ROOT, "data"))
    ap.add_argument("--custom-clients", type=int, default=0, help="дополнительная конфигурация X: число клиентов")
    ap.add_argument("--custom-path", default="/1kb.txt")
    ap.add_argument("--custom-count", type=int, default=0, help="запросов на клиента для X (по умолчанию = --count)")
    ap.add_argument("--custom-name", default="X")
    ap.add_argument("--only-custom", action="store_true", help="выполнить только X; писать в custom_*.csv")
    a = ap.parse_args()

    a.server_exe = os.path.join(a.bin, "server.exe")
    a.client_exe = os.path.join(a.bin, "client.exe")
    for exe in (a.server_exe, a.client_exe):
        if not os.path.exists(exe):
            print("Не найден %s — сначала соберите проект (scripts\\build.bat)." % exe)
            return 2
    if a.only_custom and a.custom_clients < 1:
        print("--only-custom требует --custom-clients N")
        return 2
    a.count_for = lambda name: (a.custom_count or a.count) if name == a.custom_name and a.custom_clients else a.count

    configs = [] if a.only_custom else list(CONFIGS)
    if a.custom_clients > 0:
        res = "1 КБ" if "1kb" in a.custom_path else "64 КБ" if "64kb" in a.custom_path else a.custom_path
        configs.append((a.custom_name, a.custom_clients, a.custom_path, res))

    os.makedirs(a.out, exist_ok=True)
    tmp_dir = os.path.join(a.out, "runs")
    os.makedirs(tmp_dir, exist_ok=True)

    server = Server(a.server_exe, a.host, a.port, a.www)
    try:
        if not server.wait_ready():
            print("Сервер не запустился (порт %d занят?). Закройте другие копии server.exe." % a.port)
            return 3
        print("Сервер запущен на %s:%d. STUDENT_ID=%d SEED=%d. count=%d runs=%d\n" %
              (a.host, a.port, a.student_id, a.seed, a.count, a.runs))
        if a.warmup > 0:  # прогрев: файловый кэш ОС, первые потоки
            subprocess.run([a.client_exe, "--host", a.host, "--port", str(a.port), "--path", "/64kb.txt",
                            "--count", str(a.warmup), "--quiet"], stdout=subprocess.DEVNULL)

        raw_rows, summary = [], []
        for name, clients, path, res in configs:
            for run_id in range(1, a.runs + 1):
                wall, rows = run_config(a, name, clients, path, run_id, tmp_dir)
                s = summarize(name, clients, res, run_id, wall, rows)
                summary.append(s)
                for r in rows:
                    raw_rows.append([name] + [r[k] for k in ("run_id", "client_id", "req_no", "method", "path", "status",
                                                             "bytes_sent", "bytes_recv", "body_bytes", "connect_ms",
                                                             "ttfb_ms", "total_ms", "error")])
                print("%-22s запуск %d: %9.1f мс, запросов %d, ошибок %d, %.2f Мбит/с" %
                      (s["label"], run_id, wall, s["requests"], s["errors"], s["throughput_mbit_s"]))
    finally:
        code = server.stop()
        print("\nСервер остановлен, код возврата %r." % code)

    prefix = "custom_" if a.only_custom else ""
    write_csv(os.path.join(a.out, prefix + "raw_results.csv"),
              ["config", "run_id", "client_id", "req_no", "method", "path", "status", "bytes_sent", "bytes_recv",
               "body_bytes", "connect_ms", "ttfb_ms", "total_ms", "error"], raw_rows)
    cols = ["config", "label", "clients", "resource", "run_id", "wall_ms", "requests", "ok", "errors", "timeouts",
            "bytes_sent", "bytes_recv", "avg_total_ms", "avg_connect_ms", "avg_ttfb_ms", "throughput_mbit_s", "throughput_MB_s"]
    write_csv(os.path.join(a.out, prefix + "summary.csv"), cols, [[s[c] for c in cols] for s in summary])

    # Таблица 2: время выполнения запуска (мс) по запускам и среднее.
    t2h = ["Конфигурация"] + ["Запуск %d, мс" % i for i in range(1, a.runs + 1)] + ["Среднее, мс"]
    t3h = ["Конфигурация", "Успешных запросов (ср. за запуск)", "Ошибок (всего)", "Передано байт (ср. за запуск)",
           "Пропускная способность, Мбит/с (ср.)", "Пропускная способность, МБ/с (ср.)"]
    t2, t3 = [], []
    for name, clients, path, res in configs:
        ss = [s for s in summary if s["config"] == name]
        t2.append([ss[0]["label"]] + ["%.1f" % s["wall_ms"] for s in ss] + ["%.1f" % statistics.mean(s["wall_ms"] for s in ss)])
        t3.append([ss[0]["label"], "%.1f" % statistics.mean(s["ok"] for s in ss), sum(s["errors"] for s in ss),
                   "%.0f" % statistics.mean(s["bytes_sent"] + s["bytes_recv"] for s in ss),
                   "%.3f" % statistics.mean(s["throughput_mbit_s"] for s in ss),
                   "%.3f" % statistics.mean(s["throughput_MB_s"] for s in ss)])
    write_csv(os.path.join(a.out, prefix + "table2.csv"), t2h, t2)
    write_csv(os.path.join(a.out, prefix + "table3.csv"), t3h, t3)
    with open(os.path.join(a.out, prefix + "experiment_meta.json"), "w", encoding="utf-8") as f:
        json.dump({"student_id": a.student_id, "seed": a.seed, "port": a.port, "host": a.host, "count": a.count,
                   "runs": a.runs, "timeout_ms": a.timeout, "warmup": a.warmup, "timestamp": datetime.datetime.now().isoformat(timespec="seconds"),
                   "configs": [list(c) for c in configs]}, f, ensure_ascii=False, indent=2)

    print("\nТаблица 2. Время выполнения запуска, мс\n")
    print_table(t2h, t2)
    print("\nТаблица 3. Успешные запросы, ошибки, байты, пропускная способность\n")
    print_table(t3h, t3)
    print("\nФайлы записаны в %s" % a.out)
    return 1 if any(s["errors"] for s in summary) else 0


if __name__ == "__main__":
    sys.exit(main())
