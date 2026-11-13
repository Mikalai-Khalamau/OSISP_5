#!/usr/bin/env python3
"""Генерация тестовых ресурсов лабораторной работы №5 (вариант 27).

Создаёт (детерминированно, SEED = 1000 + STUDENT_ID * 97 = 11961):
  www/index.html, www/hello.txt, www/sub/page.html, www/sub/a b.txt
  www/1kb.txt, www/64kb.txt, www/1kb.html, www/64kb.html  (РОВНО 1024 и 65536 байт, только ASCII)
  secret.txt           — файл ВНЕ www (должен быть недоступен через сервер)
  www_evil/decoy.txt   — «соседний» каталог с похожим именем (для проверки префиксной проверки пути)

Запуск: python scripts/gen_www.py [--seed 11961] [--root <корень проекта>]
"""
import argparse
import os
import random

STUDENT_ID = 113
DEFAULT_SEED = 1000 + STUDENT_ID * 97  # 11961
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:-_()"  # без < > & (безопасно для HTML)
SECRET_MARKER = "TOP-SECRET-LAB05-MUST-NEVER-BE-SERVED"


def filler(rng, size):
    """Ровно size ASCII-символов: строки по 63 символа + '\\n' (последняя может быть короче)."""
    out = []
    left = size
    while left > 0:
        n = min(64, left)
        if n == 64:
            out.append("".join(rng.choice(ALPHABET) for _ in range(63)) + "\n")
        else:
            out.append("".join(rng.choice(ALPHABET) for _ in range(n)))
        left -= n
    text = "".join(out)
    assert len(text) == size
    return text


def make_html(rng, size, title):
    head = '<!DOCTYPE html>\n<html><head><meta charset="utf-8"><title>%s</title></head><body><pre>\n' % title
    tail = "</pre></body></html>\n"
    body = filler(rng, size - len(head) - len(tail))
    data = head + body + tail
    assert len(data) == size
    return data


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:  # бинарно: никаких подмен \n -> \r\n
        f.write(text.encode("ascii"))
    print("%-28s %6d байт" % (os.path.relpath(path), os.path.getsize(path)))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED, help="SEED генератора (по умолчанию %d)" % DEFAULT_SEED)
    ap.add_argument("--root", default=os.path.dirname(here), help="корень проекта")
    a = ap.parse_args()
    www = os.path.join(a.root, "www")

    write(os.path.join(www, "index.html"),
          '<!DOCTYPE html>\n<html><head><meta charset="utf-8"><title>Lab 5</title></head>\n'
          "<body><h1>Lab 5 HTTP server</h1><p>Variant 27, STUDENT_ID=113.</p>"
          '<p><a href="/hello.txt">hello.txt</a> | <a href="/sub/page.html">sub/page.html</a></p></body></html>\n')
    write(os.path.join(www, "hello.txt"), "Hello from the lab 5 HTTP server!\n")
    write(os.path.join(www, "sub", "page.html"),
          '<!DOCTYPE html>\n<html><head><meta charset="utf-8"><title>Nested page</title></head>\n'
          "<body><h2>Nested page in www/sub</h2></body></html>\n")
    write(os.path.join(www, "sub", "a b.txt"), "File name with a space (tests %20 decoding).\n")

    # Для каждого файла — собственный генератор от seed, чтобы файлы не зависели друг от друга.
    write(os.path.join(www, "1kb.txt"), filler(random.Random(a.seed * 10 + 1), 1024))
    write(os.path.join(www, "64kb.txt"), filler(random.Random(a.seed * 10 + 2), 65536))
    write(os.path.join(www, "1kb.html"), make_html(random.Random(a.seed * 10 + 3), 1024, "1kb"))
    write(os.path.join(www, "64kb.html"), make_html(random.Random(a.seed * 10 + 4), 65536, "64kb"))

    write(os.path.join(a.root, "secret.txt"), SECRET_MARKER + "\nThis file is OUTSIDE the www directory.\n")
    write(os.path.join(a.root, "www_evil", "decoy.txt"), "Decoy: sibling directory whose name starts with 'www'.\n")


if __name__ == "__main__":
    main()
