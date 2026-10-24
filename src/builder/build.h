#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/cabfile.h"
#include "../common/project.h"

// Сборка дистрибутива по проекту.
//
//   Директория (folder): <Output>\setup.exe, setup.sif, license.txt, *.cab
//   Один файл (single):  <Output>\<SingleFile> — setup.exe с дописанным CAB,
//                        внутри которого setup.sif, license.txt и *.cab
//   Оба варианта (both): директория в <Output>\setup, один файл в <Output>

struct ComponentFiles
{
    std::vector<CabSource> files;
    uint64_t bytes = 0;
};

// Файлы компонента по источнику и маскам. Пути источника — относительно
// директории проекта.
bool collectComponent(const Component &c, const std::string &projectDir, ComponentFiles &out,
                      std::string &err);

struct BuildResult
{
    std::vector<std::string> outputs;   // созданные дистрибутивы
    uint64_t files = 0;
    uint64_t bytes = 0;
};

// stub — содержимое setup.exe (программа установки).
bool buildDistribution(const SetupProject &p, const std::string &projectPath, const std::string &stub,
                       CabProgress *progress, BuildResult &result, std::string &err);

std::string singleFileName(const SetupProject &p);

// Подстановка переменных окружения (%APP_BUILD% и т. п.) в путь проекта при
// сборке. Ошибка — если переменная не задана.
bool expandBuildPath(const std::string &path, std::string &out, std::string &err);

// Версия продукта из параметра сборки: допускается тег вида v1.2.3; версия —
// от одного до четырёх чисел через точку.
bool normalizeVersion(const std::string &in, std::string &out, std::string &err);

std::string outputDirectory(const SetupProject &p, const std::string &projectPath);

// Программа установки, встроенная в сборщик ресурсом.
bool loadSetupStub(std::string &data, std::string &err);
