#pragma once

#include <string>

#include "../common/project.h"

// Диалоги редактирования проекта. projectDir — директория файла проекта
// (пустая у несохранённого проекта): выбранные пути записываются
// относительно неё. Возвращают true, если проект изменён.

bool productDialog(SetupProject &p, const std::string &projectDir);
bool directoriesDialog(SetupProject &p);
bool componentsDialog(SetupProject &p, const std::string &projectDir);
bool shortcutsDialog(SetupProject &p);
bool registryDialog(SetupProject &p);
bool processesDialog(SetupProject &p);
bool runAfterDialog(SetupProject &p);
bool buildOptionsDialog(SetupProject &p, const std::string &projectDir);

// Список файлов всех компонентов (для проверки масок).
void showComponentFiles(const SetupProject &p, const std::string &projectDir);

std::string relativePath(const std::string &path, const std::string &baseDir);
