#pragma once

#include <cstdint>
#include <string>

#include "install.h"

// Экраны программы установки в стиле текстового режима Windows XP.
namespace ui {

// Чёрный экран «Программа установки проверяет конфигурацию...».
void checkingHardware();
// Синий экран загрузки: в строке состояния — загружаемый файл.
void loading(const std::string &title, const std::string &file);

// Возвращают false, если пользователь вышел из программы установки.
bool welcome(const std::string &name);
bool license(const std::string &name, const std::string &text);
bool chooseDirectory(const SetupProject &p, std::string &dir, uint64_t need);
// Найдена прежняя установка: true — обновить (переустановить).
bool existing(const SetupProject &p, const ExistingInstall &e);

// Экран хода выполнения с жёлтым индикатором.
class ProgressScreen : public InstallProgress
{
public:
    ProgressScreen(const std::string &title, const std::string &message, const std::string &label,
                   bool askRetryOnError = false);

    void onStatus(const std::string &text) override;
    void onPercent(int percent) override;
    bool askRetry(const std::string &err) override;

private:
    void draw();

    std::string title, message, label, statusText;
    int percent = 0;
    bool retry;
    unsigned long long lastDraw = 0;
};

// Завершение с красным индикатором обратного отсчёта.
void finish(const std::string &title, const std::string &text, const std::string &countdown,
            const std::string &key);

// Сообщение об ошибке; выход по F3 (или ВВОД).
void error(const std::string &title, const std::string &text);

// Удаление: подтверждение (L) и итог.
bool confirmUninstall(const InstallLog &log);

std::string seconds(int n);     // «1 секунду», «3 секунды», «15 секунд»

} // namespace ui
