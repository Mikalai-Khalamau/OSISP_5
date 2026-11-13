#!/usr/bin/env python3
"""Автоматические функциональные тесты HTTP-сервера (лабораторная №5, вариант 27).

Скрипт сам запускает server.exe, прогоняет проверки на «сырых» сокетах и в конце корректно
останавливает сервер (CTRL_BREAK_EVENT). Только стандартная библиотека Python 3.8+.

Запуск:   python tests/run_tests.py --bin bin\\x64\\Release
Параметры: --port 20113  --www <каталог>  --only <подстрока имени теста>
Код возврата: 0 — все тесты PASS, 1 — есть FAIL.
"""
import argparse
import os
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IS_WIN = os.name == "nt"
TEST_TIMEOUT_MS = 3000       # --timeout сервера на время тестов
DEFAULT_MAX_HEADER = 8192

# ----------------------------------------------------------------------------------------------
# Запуск/остановка сервера
# ----------------------------------------------------------------------------------------------


class ServerProc:
    def __init__(self, exe, port, www, extra=()):
        self.exe, self.port = exe, port
        self.out = []
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if IS_WIN else 0
        self.p = subprocess.Popen([exe, "--host", "127.0.0.1", "--port", str(port), "--www", www,
                                   "--timeout", str(TEST_TIMEOUT_MS), "--quiet", "--id", "99"] + list(extra),
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  creationflags=flags)
        self.t = threading.Thread(target=self._drain, daemon=True)
        self.t.start()

    def _drain(self):  # читаем вывод в фоне, чтобы заполнившийся канал не завис сервер
        for line in self.p.stdout:
            self.out.append(line.decode("utf-8", "replace"))

    def text(self):
        return "".join(self.out)

    def wait_ready(self, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if self.p.poll() is not None:
                return False
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def stop(self):
        """Штатная остановка. Возвращает код завершения процесса."""
        if self.p.poll() is None:
            try:
                if IS_WIN:
                    import signal
                    self.p.send_signal(signal.CTRL_BREAK_EVENT)
                else:
                    self.p.terminate()
                self.p.wait(timeout=15)
            except Exception:
                self.p.kill()
                self.p.wait()
        self.t.join(timeout=2)
        return self.p.returncode


# ----------------------------------------------------------------------------------------------
# HTTP на сырых сокетах
# ----------------------------------------------------------------------------------------------

def raw_exchange(port, payload=b"", chunk=0, delay=0.0, timeout=10.0):
    """Отправляет payload (целиком или кусками по chunk байт с паузой delay) и читает ответ.
    Возвращает (данные, закрыто_ли_сервером)."""
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.settimeout(timeout)
    try:
        try:
            if chunk:
                for i in range(0, len(payload), chunk):
                    s.sendall(payload[i:i + chunk])
                    time.sleep(delay)
            elif payload:
                s.sendall(payload)
        except (ConnectionError, socket.timeout):
            pass  # сервер мог закрыть раньше — всё равно попробуем прочитать, что он успел ответить
        data = b""
        closed = False
        while True:
            try:
                d = s.recv(65536)
            except socket.timeout:
                break
            except ConnectionError:
                closed = True
                break
            if not d:
                closed = True
                break
            data += d
        return data, closed
    finally:
        s.close()


def parse_response(data):
    """-> (status:int, headers:dict(lower), body:bytes)"""
    sep = data.find(b"\r\n\r\n")
    if sep < 0:
        raise AssertionError("в ответе нет конца заголовков: %r" % data[:80])
    lines = data[:sep].decode("latin-1").split("\r\n")
    parts = lines[0].split(" ", 2)
    if len(parts) < 3 or not parts[0].startswith("HTTP/1.1"):
        raise AssertionError("плохая статусная строка: %r" % lines[0])
    headers = {}
    for ln in lines[1:]:
        k, v = ln.split(":", 1)
        headers[k.strip().lower()] = v.strip()
    return int(parts[1]), headers, data[sep + 4:]


def build_request(path="/", method="GET", version="HTTP/1.1", host="localhost", extra=""):
    h = "Host: %s\r\n" % host if host else ""
    return ("%s %s %s\r\n%s%sConnection: close\r\n\r\n" % (method, path, version, h, extra)).encode("latin-1")


def http(port, path="/", method="GET", **kw):
    data, closed = raw_exchange(port, build_request(path, method, **kw))
    st, hd, body = parse_response(data)
    return st, hd, body, closed


def read_file(name):
    with open(os.path.join(ctx["www"], name), "rb") as f:
        return f.read()


# ----------------------------------------------------------------------------------------------
# Реестр тестов
# ----------------------------------------------------------------------------------------------
TESTS = []
ctx = {}


def test(title):
    def deco(fn):
        TESTS.append((title, fn))
        return fn
    return deco


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


CTYPES = {".txt": "text/plain; charset=utf-8", ".html": "text/html; charset=utf-8"}


@test("1. GET существующего файла: 200, Content-Length, Content-Type, тело, Connection: close, закрытие")
def t_get():
    for name in ["hello.txt", "1kb.txt", "64kb.txt", "1kb.html", "64kb.html", "index.html", "sub/page.html"]:
        st, hd, body, closed = http(ctx["port"], "/" + name)
        exp = read_file(name)
        expect(st == 200, "%s: статус %d" % (name, st))
        expect(hd.get("content-length") == str(len(exp)), "%s: Content-Length=%s, ожидалось %d" % (name, hd.get("content-length"), len(exp)))
        expect(hd.get("content-type") == CTYPES[os.path.splitext(name)[1]], "%s: Content-Type=%s" % (name, hd.get("content-type")))
        expect(hd.get("connection", "").lower() == "close", "%s: нет Connection: close" % name)
        expect(body == exp, "%s: тело не совпадает с файлом (%d vs %d байт)" % (name, len(body), len(exp)))
        expect(closed, "%s: сервер не закрыл соединение" % name)
        expect("date" in hd and hd["date"].endswith("GMT"), "%s: нет корректного Date" % name)


@test("2. HEAD: те же заголовки, что у GET, но без тела")
def t_head():
    for name in ["hello.txt", "1kb.txt", "64kb.html"]:
        st, hd, body, closed = http(ctx["port"], "/" + name, "HEAD")
        st2, hd2, body2, _ = http(ctx["port"], "/" + name, "GET")
        expect(st == 200 and st2 == 200, "статусы %d/%d" % (st, st2))
        expect(body == b"", "%s: у HEAD есть тело (%d байт)" % (name, len(body)))
        expect(hd["content-length"] == hd2["content-length"] == str(len(body2)), "%s: Content-Length HEAD != GET" % name)
        expect(hd["content-type"] == hd2["content-type"], "%s: Content-Type отличается" % name)
        expect(closed, "%s: соединение не закрыто" % name)
    st, hd, body, _ = http(ctx["port"], "/nope.txt", "HEAD")
    expect(st == 404 and body == b"" and int(hd["content-length"]) > 0, "HEAD для 404: статус/тело/Content-Length")


@test("3. GET несуществующего файла/каталога -> 404")
def t_404():
    for p in ["/nope.txt", "/sub", "/sub/", "/sub/nope.html", "/index.html.bak"]:
        st, hd, body, closed = http(ctx["port"], p)
        expect(st == 404, "%s: статус %d" % (p, st))
        expect(hd["content-length"] == str(len(body)) and len(body) > 0, "%s: Content-Length не равен длине тела" % p)
        expect(closed, "%s: не закрыто" % p)


@test("4. Неподдерживаемые методы (POST, PUT, DELETE, FOO, ...) -> 405 + Allow")
def t_405():
    for m in ["POST", "PUT", "DELETE", "FOO", "OPTIONS", "PATCH", "TRACE", "CONNECT"]:
        st, hd, body, closed = http(ctx["port"], "/hello.txt", m)
        expect(st == 405, "%s: статус %d" % (m, st))
        expect(hd.get("allow") == "GET, HEAD", "%s: Allow=%r" % (m, hd.get("allow")))
        expect(hd["content-length"] == str(len(body)), "%s: Content-Length" % m)
        expect(closed, "%s: не закрыто" % m)


BAD_REQUESTS = [
    ("мусор", b"garbage\r\n\r\n"),
    ("нет версии", b"GET /\r\nHost: x\r\n\r\n"),
    ("неверная версия HTTP/9.9", b"GET / HTTP/9.9\r\nHost: x\r\n\r\n"),
    ("версия-мусор", b"GET / HTTP/abc\r\nHost: x\r\n\r\n"),
    ("нет Host в HTTP/1.1", b"GET / HTTP/1.1\r\n\r\n"),
    ("заголовок без ':'", b"GET / HTTP/1.1\r\nHost x\r\n\r\n"),
    ("пробел перед ':'", b"GET / HTTP/1.1\r\nHost : x\r\n\r\n"),
    ("слишком длинная строка запроса", b"GET /" + b"a" * 9000 + b" HTTP/1.1\r\nHost: x\r\n\r\n"),
    ("NUL-байт в запросе", b"GET /\x00 HTTP/1.1\r\nHost: x\r\n\r\n"),
    ("NUL-байт в заголовке", b"GET / HTTP/1.1\r\nHost: x\x00y\r\n\r\n"),
    ("метод не token", b"G@T / HTTP/1.1\r\nHost: x\r\n\r\n"),
    ("два пробела", b"GET  / HTTP/1.1\r\nHost: x\r\n\r\n"),
    ("target не origin-form", b"GET http://x/ HTTP/1.1\r\nHost: x\r\n\r\n"),
    ("голый LF", b"GET / HTTP/1.1\r\nHost: x\nFoo: y\r\n\r\n"),
    ("Content-Length не число", b"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n"),
    ("два Host", b"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n"),
    ("пустая строка запроса", b"\r\n\r\n"),
    ("кривое %-кодирование в пути", b"GET /%zz HTTP/1.1\r\nHost: x\r\n\r\n"),
]


@test("5. Некорректные запросы -> 400")
def t_400():
    for title, payload in BAD_REQUESTS:
        data, closed = raw_exchange(ctx["port"], payload)
        st, hd, body = parse_response(data)
        expect(st == 400, "%s: статус %d" % (title, st))
        expect(hd["content-length"] == str(len(body)), "%s: Content-Length" % title)
        expect(closed, "%s: соединение не закрыто" % title)
    # HTTP/1.0 без Host допустим -> 200
    data, _ = raw_exchange(ctx["port"], b"GET /hello.txt HTTP/1.0\r\n\r\n")
    expect(parse_response(data)[0] == 200, "HTTP/1.0 без Host должен давать 200")


def secret_marker():
    with open(os.path.join(ROOT, "secret.txt"), "rb") as f:
        return f.readline().strip()


TRAVERSAL = [
    ("/../secret.txt", {404}), ("/%2e%2e/secret.txt", {404}), ("/%2E%2E/secret.txt", {404}),
    ("/..%2fsecret.txt", {404}), ("/%2e%2e%2fsecret.txt", {404}), ("/%2f..%2fsecret.txt", {404}),
    ("/%252e%252e/secret.txt", {404}), ("/..\\secret.txt", {404}), ("/..%5csecret.txt", {404}),
    ("/sub/../../secret.txt", {404}), ("/a/../../secret.txt", {404}), ("/./secret.txt", {404}),
    ("//secret.txt", {404}), ("/%2E%2E/", {404}), ("/sub/%2e%2e/%2e%2e/secret.txt", {404}),
    ("/C:/Windows/win.ini", {404}), ("/C:\\Windows\\win.ini", {404}), ("/hello.txt::$DATA", {404}),
    ("/hello.txt:stream", {404}), ("/con", {404}), ("/nul", {404}), ("/CON.txt", {404}), ("/aux", {404}),
    ("/com1", {404}), ("/lpt9.txt", {404}), ("/hello.txt.", {404}), ("/hello.txt%20", {404}),
    ("/\\\\server\\share\\x", {404}), ("/%5c%5cserver%5cshare", {404}), ("/" + "a/" * 600 + "x", {404}),
    ("/%00", {400}), ("/hello.txt%00.jpg", {400}), ("/%zz", {400}), ("/%", {400}), ("/%2", {400}),
    ("/%0d%0a", {400}),
]


@test("6. Обход каталога (относительные/закодированные/двойное кодирование/диски/устройства/NUL)")
def t_traversal():
    marker = secret_marker()
    expect(len(marker) > 10, "не найден secret.txt в корне проекта")
    for path, allowed in TRAVERSAL:
        data, _ = raw_exchange(ctx["port"], build_request(path))
        expect(marker not in data, "УТЕЧКА secret.txt для %r" % path)
        st, hd, body = parse_response(data)
        expect(st in allowed, "%r: статус %d, ожидалось %s" % (path, st, sorted(allowed)))
    # Позитивный контроль: сервер по-прежнему отдаёт обычные файлы.
    expect(http(ctx["port"], "/hello.txt")[0] == 200, "после атак обычный файл недоступен")


@test("7. Заголовки больше лимита (16 КБ) -> 400 и закрытие; граница 8192/8193; сервер жив")
def t_header_limit():
    payload = b"GET / HTTP/1.1\r\nHost: x\r\nX-Big: " + b"a" * 16384 + b"\r\n\r\n"
    data, closed = raw_exchange(ctx["port"], payload)
    st, _, _ = parse_response(data)
    expect(st == 400 and closed, "16 КБ: статус %d, closed=%s" % (st, closed))
    base = b"GET / HTTP/1.1\r\nHost: x\r\nX-Pad: "
    for total, want in [(DEFAULT_MAX_HEADER, 200), (DEFAULT_MAX_HEADER + 1, 400)]:
        pad = total - len(base) - 4
        data, _ = raw_exchange(ctx["port"], base + b"p" * pad + b"\r\n\r\n")
        expect(parse_response(data)[0] == want, "блок заголовков %d байт: ожидался %d" % (total, want))
    # Лимит работает и когда терминатора \r\n\r\n нет вовсе (бесконечный поток).
    data, closed = raw_exchange(ctx["port"], b"GET /" + b"z" * 20000)
    expect(parse_response(data)[0] == 400, "поток без \\r\\n\\r\\n должен давать 400")
    expect(http(ctx["port"], "/hello.txt")[0] == 200, "сервер перестал отвечать")


@test("8. Запрос по частям (по 1 байту с паузами) -> корректный ответ")
def t_partial():
    payload = build_request("/hello.txt")
    data, closed = raw_exchange(ctx["port"], payload, chunk=1, delay=0.02)
    st, hd, body = parse_response(data)
    expect(st == 200 and body == read_file("hello.txt"), "статус %d" % st)
    # И разрыв посреди терминатора \r\n | \r\n
    s = socket.create_connection(("127.0.0.1", ctx["port"]), timeout=5)
    s.sendall(b"GET /hello.txt HTTP/1.1\r\nHost: x\r\n\r")
    time.sleep(0.2)
    s.sendall(b"\n")
    buf = b""
    while True:
        d = s.recv(65536)
        if not d:
            break
        buf += d
    s.close()
    expect(parse_response(buf)[0] == 200, "разорванный \\r\\n\\r\\n не распознан")


@test("9. Молчащий клиент закрывается по тайм-ауту, остальные обслуживаются")
def t_silent():
    silent = socket.create_connection(("127.0.0.1", ctx["port"]), timeout=15)
    t0 = time.time()
    expect(http(ctx["port"], "/hello.txt")[0] == 200, "другой клиент не обслужен, пока молчит первый")
    silent.settimeout(TEST_TIMEOUT_MS / 1000.0 + 5)
    try:
        d = silent.recv(100)
    except ConnectionError:
        d = b""
    waited = time.time() - t0
    silent.close()
    expect(d == b"", "молчащему клиенту пришли данные: %r" % d[:40])
    expect(waited < TEST_TIMEOUT_MS / 1000.0 + 3, "сервер закрыл молчуна слишком поздно (%.1f с)" % waited)


@test("10. Обрыв соединения посреди запроса (FIN и RST) — сервер не падает")
def t_abort():
    s = socket.create_connection(("127.0.0.1", ctx["port"]), timeout=5)
    s.sendall(b"GET /hello.txt HTT")
    s.close()
    import struct
    s = socket.create_connection(("127.0.0.1", ctx["port"]), timeout=5)
    s.sendall(b"GET /64kb.txt HTTP/1.1\r\nHo")
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))  # close -> RST
    s.close()
    # клиент отправил полный запрос и сразу оборвал, не читая 64 КБ ответа
    s = socket.create_connection(("127.0.0.1", ctx["port"]), timeout=5)
    s.sendall(build_request("/64kb.txt"))
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    s.close()
    time.sleep(0.5)
    expect(http(ctx["port"], "/1kb.txt")[0] == 200, "сервер не отвечает после обрывов")


@test("11. 5 одновременных клиентов × 20 запросов (1 КБ и 64 КБ), проверка содержимого")
def t_concurrent():
    errors = []
    files = ["1kb.txt", "64kb.txt", "1kb.html", "64kb.html", "hello.txt"]
    expected = {n: read_file(n) for n in files}

    def worker(k):
        try:
            for i in range(20):
                name = files[(k + i) % len(files)]
                st, hd, body, _ = http(ctx["port"], "/" + name)
                if st != 200 or body != expected[name] or hd["content-length"] != str(len(body)):
                    errors.append("клиент %d, %s: статус %d, %d байт" % (k, name, st, len(body)))
                    return
        except Exception as e:  # noqa
            errors.append("клиент %d: %r" % (k, e))

    ths = [threading.Thread(target=worker, args=(k,)) for k in range(5)]
    [t.start() for t in ths]
    [t.join() for t in ths]
    expect(not errors, "; ".join(errors[:3]))


@test("12. «/» -> index.html; query, fragment, %-кодирование имени")
def t_root_query():
    st, hd, body, _ = http(ctx["port"], "/")
    expect(st == 200 and body == read_file("index.html"), "/ -> index.html")
    expect(hd["content-type"] == "text/html; charset=utf-8", "Content-Type у /")
    for p in ["/hello.txt?x=1", "/hello.txt?", "/hello.txt#frag", "/hello.txt?a=b#c", "/%68ello.txt", "/?x=1"]:
        st, _, body, _ = http(ctx["port"], p)
        expect(st == 200, "%s: статус %d" % (p, st))
    st, _, body, _ = http(ctx["port"], "/sub/a%20b.txt")
    expect(st == 200 and body == read_file("sub/a b.txt"), "/sub/a%20b.txt")


@test("13. Клиент: подключение к неправильному порту -> WSAECONNREFUSED и ненулевой код")
def t_client_refused():
    exe = os.path.join(ctx["bin"], "client.exe")
    expect(os.path.exists(exe), "нет %s" % exe)
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    free = s.getsockname()[1]
    s.close()  # порт только что свободен — на нём никто не слушает
    r = subprocess.run([exe, "--host", "127.0.0.1", "--port", str(free), "--timeout", "3000"],
                       capture_output=True, timeout=30)
    out = (r.stdout + r.stderr).decode("utf-8", "replace")
    expect(r.returncode != 0, "код возврата 0, ожидался ненулевой")
    expect("10061" in out and "WSAECONNREFUSED" in out, "в выводе нет WSAECONNREFUSED/10061:\n" + out)
    # положительный контроль клиента
    r = subprocess.run([exe, "--host", "127.0.0.1", "--port", str(ctx["port"]), "--path", "/1kb.txt", "--count", "5",
                        "--expect", "200", "--quiet"], capture_output=True, timeout=30)
    expect(r.returncode == 0, "клиент на рабочем сервере вернул %d: %s" % (r.returncode, r.stdout.decode("utf-8", "replace")))


@test("14. Занятый порт: второй сервер на том же порту -> понятная ошибка bind и ненулевой код")
def t_port_busy():
    second = ServerProc(ctx["server_exe"], ctx["port"], ctx["www"])
    try:
        second.p.wait(timeout=15)
    except subprocess.TimeoutExpired:
        second.stop()
        raise AssertionError("второй сервер не завершился — порт не считается занятым")
    second.t.join(timeout=2)
    out = second.text()
    expect(second.p.returncode not in (0, None), "код возврата %r" % second.p.returncode)
    expect("bind" in out and "10048" in out, "нет понятного сообщения о bind/10048:\n" + out)


@test("15. Штатное завершение (Ctrl+Break): код 0, итоговая статистика")
def t_shutdown():
    code = ctx["server"].stop()
    out = ctx["server"].text()
    expect(code == 0, "код возврата %r\n%s" % (code, out))
    expect("STATS" in out and "STOP" in out, "нет статистики/STOP в выводе:\n" + out)
    ctx["server_stopped"] = True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(ROOT, "bin", "x64", "Release"), help="папка с server.exe и client.exe")
    ap.add_argument("--port", type=int, default=20113)
    ap.add_argument("--www", default=os.path.join(ROOT, "www"))
    ap.add_argument("--only", default="", help="выполнить только тесты, в названии которых есть эта подстрока")
    a = ap.parse_args()

    server_exe = os.path.join(a.bin, "server.exe")
    if not os.path.exists(server_exe):
        print("Не найден %s — сначала соберите проект (scripts\\build.bat)." % server_exe)
        return 2
    ctx.update(bin=a.bin, port=a.port, www=a.www, server_exe=server_exe)

    srv = ServerProc(server_exe, a.port, a.www)
    ctx["server"] = srv
    if not srv.wait_ready():
        print("Сервер не запустился:\n" + srv.text())
        srv.stop()
        return 2
    print("Сервер запущен (порт %d). Тесты:\n" % a.port)

    failed = 0
    run = 0
    try:
        for title, fn in TESTS:
            if a.only and a.only not in title and not title.startswith("15."):
                continue
            if title.startswith("15.") and ctx.get("server_stopped"):
                continue
            run += 1
            t0 = time.time()
            try:
                fn()
                print("PASS  %s  (%.2f с)" % (title, time.time() - t0))
            except AssertionError as e:
                failed += 1
                print("FAIL  %s\n      %s" % (title, str(e).replace("\n", "\n      ")))
            except Exception as e:  # noqa
                failed += 1
                print("FAIL  %s\n      исключение: %r" % (title, e))
    finally:
        if not ctx.get("server_stopped"):
            srv.stop()
    print("\nИтого: %d тестов, прошло %d, провалено %d" % (run, run - failed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
