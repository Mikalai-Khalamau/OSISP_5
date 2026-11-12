// text_utils.h — мелкие функции для работы с текстом и кодировками.
// Все «узкие» строки в программе — UTF-8 (в проектах включён ключ компилятора /utf-8,
// а консоль переключается на кодовую страницу UTF-8 функцией EnableUtf8Console).
#pragma once
#include "platform.h"
#include <string>

// Строгое преобразование UTF-8 -> UTF-16. false, если в строке некорректный UTF-8.
bool Utf8ToWide(const std::string& s, std::wstring& out);
// Преобразование из системной ANSI-кодировки (так приходят аргументы argv) в UTF-16.
bool AcpToWide(const std::string& s, std::wstring& out);
// UTF-16 -> UTF-8.
std::string WideToUtf8(const std::wstring& w);
// Переключает вывод консоли на UTF-8, чтобы русский текст отображался корректно.
void EnableUtf8Console();
// Подготовка произвольной строки (из сети!) для записи в журнал: непечатаемые байты -> \xHH,
// длинные строки обрезаются. Защищает журнал от «инъекции» переводов строк.
std::string SanitizeForLog(const std::string& s, size_t maxLen = 120);
// Перевод ASCII-символов строки в нижний регистр.
std::string ToLowerAscii(std::string s);
