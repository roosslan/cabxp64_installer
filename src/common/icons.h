#pragma once

#include <string>
#include <vector>

// Иконки в ресурсах исполняемого файла. В программе установки две группы:
// 1 — иконка установки, 2 — иконка удаления. Проводник показывает первую
// группу, поэтому у uninst.exe группа 1 удаляется.

const int kSetupIconGroup = 1;
const int kUninstallIconGroup = 2;

struct IconImage
{
    int width = 0;      // 256 для записи с нулевым размером
    int height = 0;
    int bpp = 0;
    unsigned bytes = 0;
};

// Замена группы иконок изображениями из файла .ico; пустой ico — удаление группы.
bool setIconGroup(const std::string &exe, int group, const std::string &ico, std::string &err);

// Изображения группы (пусто — группы нет).
std::vector<IconImage> iconGroup(const std::string &exe, int group);

// Проверка файла .ico.
bool checkIconFile(const std::string &ico, std::string &err);
