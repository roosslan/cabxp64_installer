#pragma once

#include <string>

// Журнал работы: текст в UTF-8, строка на событие с отметкой времени.
// Файл дописывается; каждая запись сразу сбрасывается на диск.
namespace xlog {

// Открытие журнала и запись заголовка сеанса (программа, параметры, Windows).
bool open(const std::string &path, const std::string &program);
void close();
const std::string &path();

void write(const std::string &msg);
void error(const std::string &msg);         // строка с пометкой «ОШИБКА»
// Сообщение с текстом последней ошибки Win32.
void lastError(const std::string &msg);
// Копия записей текущего сеанса в отдельный файл.
bool saveSession(const std::string &to);

} // namespace xlog
