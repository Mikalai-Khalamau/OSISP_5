@echo off
rem Сборка lab05.sln из командной строки (Release, x64).
rem Запускайте из "Developer Command Prompt for VS 2022" либо просто двойным щелчком:
rem если msbuild не найден в PATH, скрипт попробует найти его через vswhere.
setlocal
cd /d "%~dp0.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="

where msbuild >nul 2>nul
if not errorlevel 1 set "MSBUILD=msbuild"

if not defined MSBUILD (
    if not exist "%VSWHERE%" (
        echo [ОШИБКА] msbuild не найден. Откройте "Developer Command Prompt for VS 2022".
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [ОШИБКА] Не удалось найти MSBuild. Установите VS 2022 с компонентом "Разработка классических приложений на C++".
    exit /b 1
)

"%MSBUILD%" lab05.sln /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
if errorlevel 1 (
    echo [ОШИБКА] Сборка не удалась.
    exit /b 1
)
echo.
echo Готово: bin\x64\Release\server.exe и bin\x64\Release\client.exe
exit /b 0
