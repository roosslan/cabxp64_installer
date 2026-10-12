#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/project.h"

// Журнал установки: всё, что нужно удалить при деинсталляции. Хранится в
// директории установки в файле uninstall.sif рядом с uninst.exe.
struct InstallLog
{
    std::string name, version, publisher, productCode, installDir;
    bool perMachine = false;
    std::vector<std::string> files;
    std::vector<std::string> dirs;          // созданные директории
    std::vector<RegistryValue> values;      // записанные значения реестра
    std::vector<std::string> keys;          // созданные разделы: "HKCU\путь"
    std::vector<std::string> shortcuts;
    std::vector<std::string> killProcesses;

    bool load(const std::string &path, std::string &err);
    bool save(const std::string &path, std::string &err) const;
};

const char *const kLogName = "uninstall.sif";
const char *const kUninstallerName = "uninst.exe";
// Копия журнала программы установки в директории установки.
const char *const kSetupLogName = "setup.log";

// Установленная ранее версия продукта (по разделу Uninstall реестра).
struct ExistingInstall
{
    bool found = false;
    std::string version;
    std::string dir;
};

ExistingInstall findExisting(const SetupProject &p);

// Обратная связь для экранов хода выполнения.
class InstallProgress
{
public:
    virtual ~InstallProgress() = default;
    virtual void onStatus(const std::string &) {}   // правая часть строки состояния
    virtual void onPercent(int) {}
    // Ошибка при копировании: true — повторить, false — прервать установку.
    virtual bool askRetry(const std::string &) { return false; }
};

struct InstallJob
{
    SetupProject project;
    std::string distDir;        // где лежат setup.sif и CAB-архивы
    std::string installDir;
    std::string selfPath;       // программа установки (для uninst.exe)
    uint64_t selfStubSize = 0;  // размер программы без дописанных данных
    ExistingInstall existing;

    InstallLog log;             // заполняется по ходу установки
};

// Этап 1: остановка процессов, удаление прежней версии, копирование файлов.
// При ошибке скопированное удаляется.
bool installFiles(InstallJob &job, InstallProgress *progress, std::string &err);
// Этап 2: реестр, ярлыки, программа удаления, регистрация в системе.
bool installConfig(InstallJob &job, InstallProgress *progress, std::string &err);
// Запуск программ из [RunAfterInstall].
void runAfterInstall(const InstallJob &job);

// Удаление по журналу. keepRegistration: не трогать раздел Uninstall и
// uninst.exe (используется при обновлении поверх прежней версии).
bool uninstall(const InstallLog &log, InstallProgress *progress, bool keepRegistration,
               std::string &err);

void killProcesses(const std::vector<std::string> &names, InstallProgress *progress);
std::string uninstallKeyPath(const std::string &productCode);
bool isElevated();
// Сумма размеров файлов во всех CAB-архивах дистрибутива.
uint64_t payloadSize(const SetupProject &p, const std::string &distDir);
