#pragma once

// Команды приложения. Значения меньше 256, чтобы их можно было запрещать.
enum : unsigned short
{
    cmNewProject    = 100,
    cmOpenProject,
    cmSaveProject,
    cmSaveProjectAs,
    cmAbout,

    // Разделы проекта.
    cmProduct       = 110,
    cmDirectories,
    cmComponents,
    cmShortcuts,
    cmRegistry,
    cmProcesses,
    cmRunAfter,
    cmBuildOptions,

    cmBuild         = 120,
    cmRunSetup,
    cmShowFiles,

    // Внутренние команды диалогов (используются dialogs.cpp).
    cmAddItemFile   = 140,
    cmAddItemDir,
    cmAddItemMask,
    cmRemoveItem,
    cmBrowseDir,
    cmYesAll,
    cmNoAll,

    // Редактор списков и поля с кнопкой «Обзор».
    cmListAdd       = 160,
    cmListEdit,
    cmListDelete,
    cmListUp,
    cmListDown,
    cmNewGuid,
    cmBrowseSource,
    cmBrowseLicense,
    cmBrowseOutput,
    cmBrowseTarget,
    cmBrowseIcon,
    cmBrowseUninstallIcon,
};
