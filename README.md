# Лабораторная №5. Вариант 27 — упрощённый HTTP/1.1-сервер (TCP, Winsock2)

STUDENT_ID = 113, SEED = 11961, порт по умолчанию 20113, адрес 127.0.0.1.

## Сборка
Откройте `lab05.sln` в Visual Studio 2022 → конфигурация **Release | x64** → *Сборка → Собрать решение*
(или `scripts\build.bat`). Результат: `bin\x64\Release\server.exe` и `client.exe`.
Требуется компонент «Разработка классических приложений на C++» (Windows 10/11 SDK, toolset v143).

## Запуск (из корня проекта)
```
bin\x64\Release\server.exe --host 127.0.0.1 --port 20113 --www www
bin\x64\Release\client.exe --port 20113 --path /1kb.txt --print-body
bin\x64\Release\client.exe --port 20113 --method HEAD --path /64kb.txt
bin\x64\Release\client.exe --port 20113 --raw "GET /../secret.txt HTTP/1.1\r\nHost: x\r\n\r\n"
```
Остановка сервера: Ctrl+C или `q` + Enter. Справка: `--help` у обеих программ.

## Тесты, эксперименты, графики (Python 3.8+)
```
pip install -r requirements.txt
python tests\run_tests.py --bin bin\x64\Release
python scripts\run_experiments.py --bin bin\x64\Release --count 200 --runs 3
python scripts\plot_results.py
```
Дополнительный эксперимент на защите:
`python scripts\run_experiments.py --only-custom --custom-clients 5 --custom-path /64kb.html --custom-count 100`

Тестовые файлы можно пересоздать: `python scripts\gen_www.py`.
Диаграммы (исходники): `diagrams\sequence.puml`, `architecture.drawio`, `sequence.mermaid`.
Диаграмма последовательности уже отрендерена: `diagrams\sequence.png` и `sequence.svg`.
Подробная инструкция по сборке, запуску, файлам и графике — `ИНСТРУКЦИЯ.md`.

`data\plots_sample\` и `data\sample_data_for_plot_check.csv` — ДЕМО-данные для проверки скрипта графиков, не результаты эксперимента.

## Git: подготовка и этапы работы

### 1. Подготовка

Установите Git for Windows, затем задайте данные автора (подставьте свой
актуальный e-mail):

```powershell
git config --global user.name "Mikalai Khalamau"
git config --global user.email "your-email@example.com"
git init
git branch -M main
```

Создайте **приватный** репозиторий на GitHub или GitLab без автоматического
README, скопируйте его URL и подключите remote:

```powershell
git remote add origin https://github.com/<login>/lr5-http-server.git
git remote -v
```

Чтобы добавить преподавателя на GitHub: откройте репозиторий → **Settings →
Collaborators → Add people**, найдите аккаунт преподавателя и подтвердите
доступ. Для GitLab аналогичный пункт находится в **Project information →
Members**.

`.gitignore` оставляет в репозитории исходники, `www/`, `data/*.csv`,
диаграммы, скрипты и PDF в `report/`, но исключает `bin/`, `obj/`, `.vs/`,
исполняемые файлы, отладочные базы и `secret.txt`.

### 2. История по этапам

История этого проекта разбита на небольшие логически завершённые коммиты:

1. **Проектирование** — структура, README, описание протокола и исходники
   диаграмм (`diagrams/sequence.puml`, `architecture.drawio`).
2. **Базовое сетевое взаимодействие** — `lab05.sln`, проекты Visual Studio,
   `src/common/`, каркасы сервера и клиента, создание сокетов, подключение и
   базовый обмен.
3. **Основная функциональность и тестирование** — HTTP-разбор, GET/HEAD,
   ответы 200/400/404/405, защита путей, лимит заголовков, многопоточность,
   журналирование, `www/` и `tests/`.
4. **Эксперименты и отчёт** — `scripts/`, `data/`, графики и PDF в `report/`.

Пример сообщений коммитов:

```text
chore: configure repository and ignore generated files
docs: add project design and protocol diagrams
feat: add Winsock common library and project skeleton
feat: implement basic TCP server and client exchange
feat: implement HTTP parsing and static file responses
test: add protocol, security, and concurrency checks
feat: add experiment scripts and web fixtures
data: add experiment results and generated plots
docs: add laboratory report materials
```

Для каждого завершённого этапа отправляйте историю:

```powershell
git add <файлы-этапа>
git commit -m "<сообщение из списка>"
git push -u origin main
```

### 3. Проверка перед сдачей

```powershell
git log --oneline --decorate --graph
git status
git remote -v
git rev-parse HEAD
```

На странице приватного репозитория GitHub проверьте вкладки **Code** и
**Commits**: должны быть видны все файлы и созданная история. Команда
`git rev-parse HEAD` выводит хеш финального коммита, который можно передать
преподавателю.

### 4. Шпаргалка к защите

```powershell
git log --oneline --decorate --graph
git show --stat <commit>
git diff <старый-коммит>..<новый-коммит>
# например, изменить тайм-аут в исходнике, затем:
git add src\common\net_io.cpp
git commit -m "fix: adjust socket timeout"
git push
```

На защите удобно показать `git show` для одного этапа, `git diff` между двумя
версиями и финальный хеш из `git rev-parse HEAD`.
