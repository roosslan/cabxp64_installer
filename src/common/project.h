#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cabfile.h"

// Описание установки. Один формат используется для файла проекта сборщика
// (*.xsp) и для setup.sif в дистрибутиве; поля, нужные только сборщику
// (источники файлов, параметры сборки), в setup.sif не записываются.

struct Component
{
    std::string id;             // имя секции [Component.<id>]
    std::string description;
    std::string cabinet;        // имя CAB в дистрибутиве
    int directory = 1;          // номер из [Directories]
    // Только для сборщика.
    std::string source;         // директория с файлами (относительно проекта)
    std::string include = "*.*";
    std::string exclude;        // маски через ';', пути — относительно source
    bool recurse = true;
};

enum class ShortcutPlace { Desktop, Programs, Startup };

struct Shortcut
{
    ShortcutPlace place = ShortcutPlace::Desktop;
    std::string name;           // для меню «Пуск» допускается «Группа\Имя»
    std::string target;
    std::string args;
    std::string workDir;
};

struct RegistryValue
{
    std::string root = "HKCU";  // HKCU или HKLM
    std::string key;
    std::string name;           // пустое — значение по умолчанию
    std::string type = "REG_SZ";    // REG_SZ, REG_EXPAND_SZ, REG_DWORD
    std::string value;
};

struct RunEntry
{
    std::string program;
    std::string args;
};

enum class BuildMode { Folder, Single, Both };

struct SetupProject
{
    std::string name;
    std::string version = "1.0.0.0";
    std::string publisher;
    std::string productCode;    // GUID в фигурных скобках
    std::string installDir;
    bool allowChangeDir = true;
    bool perMachine = false;    // false: текущий пользователь (HKCU)
    std::string license;        // файл лицензии (в проекте — относительно проекта)

    std::map<int, std::string> directories;
    std::vector<Component> components;
    std::vector<Shortcut> shortcuts;
    std::vector<RegistryValue> registry;
    std::vector<std::string> killProcesses;
    std::vector<RunEntry> runAfter;

    // Параметры сборки.
    std::string outputDir = "dist";
    std::string singleFile;     // пусто: <name>_setup.exe
    BuildMode mode = BuildMode::Folder;
    CompressionSpec compression;

    std::string title() const { return "Установка " + name; }
};

// forSetup: записать вариант для дистрибутива, без полей сборщика.
bool loadProject(const std::string &path, SetupProject &p, std::string &err);
bool saveProject(const std::string &path, const SetupProject &p, bool forSetup, std::string &err);

// Проверка полноты описания. Пустая строка — ошибок нет.
std::string validateProject(const SetupProject &p);

std::string newGuid();
// Сравнение версий вида 1.2.3.4: <0, 0, >0.
int compareVersions(const std::string &a, const std::string &b);

const char *placeName(ShortcutPlace place);
ShortcutPlace placeFromName(const std::string &name);
const char *placeTitle(ShortcutPlace place);   // «Рабочий стол», ...

// Подстановка переменных: %INSTALLDIR%, известные директории
// (%APPDATA%, %LOCALAPPDATA%, %PROGRAMFILES%, %DESKTOP%, %PROGRAMS%,
// %STARTUP%, ...) и переменные окружения. perMachine выбирает общие
// директории рабочего стола и меню «Пуск».
std::string expandVars(const std::string &s, const std::string &installDir, bool perMachine);
// Директория по номеру из [Directories] с подстановкой переменных.
std::string directoryPath(const SetupProject &p, int id, const std::string &installDir);
