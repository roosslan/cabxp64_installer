#define Uses_TKeys
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TGroup
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TScrollBar
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TSubMenu
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_MsgBox
#include <tvision/tv.h>

#include <windows.h>

#include "../common/format.h"
#include "../common/fsutil.h"
#include "../common/version.h"
#include "../tv/commands.h"
#include "../tv/dialogs.h"
#include "../tv/hotkeys.h"
#include "app.h"
#include "build.h"
#include "projdlg.h"
#include "textview.h"

namespace {

const unsigned kBuildHoldMs = 600;

std::string pad(const std::string &s, int width)
{
    int n = (int) fsu::widen(s).size();
    return n >= width ? s + " " : s + std::string(width - n, ' ');
}

std::string summary(const SetupProject &p, const std::string &path)
{
    std::string t;
    auto row = [&t](const std::string &k, const std::string &v) { t += " " + pad(k, 22) + v + "\n"; };
    row("Продукт", p.name + " " + p.version);
    row("Издатель", p.publisher.empty() ? "—" : p.publisher);
    row("Код продукта", p.productCode);
    row("Директория установки", p.installDir);
    row("Изменение директории", p.allowChangeDir ? "разрешено" : "запрещено");
    row("Установка", p.perMachine ? "для всех пользователей" : "для текущего пользователя");
    row("Лицензия", p.license.empty() ? "нет" : p.license);

    t += "\n Директории\n";
    if (p.directories.empty())
        t += "   (нет)\n";
    for (const auto &d : p.directories)
        t += "   " + pad(std::to_string(d.first), 4) + d.second + "\n";

    t += "\n Компоненты\n";
    if (p.components.empty())
        t += "   (нет)\n";
    for (const Component &c : p.components)
    {
        t += "   " + pad(c.id, 14) + pad(c.cabinet, 16) + "→ " + std::to_string(c.directory) + "   " +
             (c.source.empty() ? "." : c.source) + "\n";
        std::string masks = c.include.empty() ? "*.*" : c.include;
        if (!c.exclude.empty())
            masks += ", кроме " + c.exclude;
        t += "   " + pad("", 14) + masks + (c.recurse ? "" : " (без поддиректорий)") +
             (c.description.empty() ? "" : " — " + c.description) + "\n";
    }

    t += "\n Ярлыки\n";
    if (p.shortcuts.empty())
        t += "   (нет)\n";
    for (const Shortcut &s : p.shortcuts)
        t += "   " + pad(placeTitle(s.place), 15) + pad(s.name, 22) + s.target + "\n";

    t += "\n Реестр\n";
    if (p.registry.empty())
        t += "   (нет)\n";
    for (const RegistryValue &r : p.registry)
        t += "   " + r.root + "\\" + r.key + " : " + (r.name.empty() ? "(по умолчанию)" : r.name) + " = " +
             r.value + "\n";

    std::string procs;
    for (const std::string &k : p.killProcesses)
        procs += (procs.empty() ? "" : ", ") + k;
    t += "\n Завершать процессы\n   " + (procs.empty() ? std::string("(нет)") : procs) + "\n";

    t += "\n Запуск после установки\n";
    if (p.runAfter.empty())
        t += "   (нет)\n";
    for (const RunEntry &r : p.runAfter)
        t += "   " + r.program + (r.args.empty() ? "" : " " + r.args) + "\n";

    const char *mode = p.mode == BuildMode::Single ? "один файл"
                     : p.mode == BuildMode::Both   ? "директория и один файл"
                                                   : "директория";
    const char *comp = p.compression.type == CabCompression::None    ? "без сжатия"
                     : p.compression.type == CabCompression::MSZIP   ? "MSZIP"
                                                                     : "LZX";
    t += "\n Сборка\n";
    t += "   " + pad("Результат", 19) + (path.empty() ? p.outputDir : outputDirectory(p, path)) + "\n";
    t += "   " + pad("Вид", 19) + mode + "\n";
    if (p.mode != BuildMode::Folder)
        t += "   " + pad("Файл", 19) + singleFileName(p) + "\n";
    t += "   " + pad("Сжатие", 19) + comp + "\n";
    return t;
}

SetupProject defaultProject()
{
    SetupProject p;
    p.name = "Новая программа";
    p.productCode = newGuid();
    p.installDir = "%LOCALAPPDATA%\\Programs\\" + p.name;
    p.directories[1] = "%INSTALLDIR%";
    Component c;
    c.id = "program";
    c.description = "Файлы программы";
    c.cabinet = "program.cab";
    c.directory = 1;
    c.source = "files";
    p.components.push_back(c);
    return p;
}

} // namespace

// ---------------------------------------------------------------------------

TProjectWindow::TProjectWindow(const TRect &bounds) :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, "Проект", wnNoNumber)
{
    flags &= ~(wfClose);
    TScrollBar *v = standardScrollBar(sbVertical | sbHandleKeyboard);
    TScrollBar *h = standardScrollBar(sbHorizontal | sbHandleKeyboard);
    TRect r = getExtent();
    r.grow(-1, -1);
    text = new TTextScroller(r, h, v);
    insert(text);
}

void TProjectWindow::update(const SetupProject &p, const std::string &path, bool modified)
{
    delete[] (char *) title;
    std::string t = (path.empty() ? std::string("Новый проект") : fsu::baseName(path)) + (modified ? " *" : "");
    title = newStr(t.c_str());
    if (frame)
        frame->drawView();
    text->setText(summary(p, path));
}

// ---------------------------------------------------------------------------

TBuilderApp::TBuilderApp() :
    TProgInit(&TBuilderApp::initStatusLine, &TBuilderApp::initMenuBar, &TBuilderApp::initDeskTop)
{
}

TMenuBar *TBuilderApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    return new TCabMenuBar(r,
        *new TSubMenu("~Ф~айл", kbNoKey) +
            *new TMenuItem("~Н~овый проект", cmNewProject, kbCtrlN, hcNoContext, "Ctrl-N") +
            *new TMenuItem("~О~ткрыть...", cmOpenProject, kbF3, hcNoContext, "F3") +
            *new TMenuItem("~С~охранить", cmSaveProject, kbF2, hcNoContext, "F2") +
            *new TMenuItem("Сохранить ~к~ак...", cmSaveProjectAs, kbNoKey) +
            newLine() +
            *new TMenuItem("В~ы~ход", cmQuit, kbAltX, hcNoContext, "Alt-X") +
        *new TSubMenu("~П~роект", kbNoKey) +
            *new TMenuItem("~П~родукт...", cmProduct, kbF4, hcNoContext, "F4") +
            *new TMenuItem("~Д~иректории...", cmDirectories, kbNoKey) +
            *new TMenuItem("~К~омпоненты...", cmComponents, kbF5, hcNoContext, "F5") +
            *new TMenuItem("~Я~рлыки...", cmShortcuts, kbNoKey) +
            *new TMenuItem("~Р~еестр...", cmRegistry, kbNoKey) +
            *new TMenuItem("П~р~оцессы...", cmProcesses, kbNoKey) +
            *new TMenuItem("~З~апуск после установки...", cmRunAfter, kbNoKey) +
            newLine() +
            *new TMenuItem("~Ф~айлы дистрибутива...", cmShowFiles, kbF6, hcNoContext, "F6") +
        *new TSubMenu("~С~борка", kbNoKey) +
            *new TMenuItem("~С~обрать дистрибутив", cmBuild, kbF9, hcNoContext, "F9") +
            *new TMenuItem("~З~апустить установку", cmRunSetup, kbCtrlF9, hcNoContext, "Ctrl-F9") +
            newLine() +
            *new TMenuItem("~П~араметры сборки...", cmBuildOptions, kbNoKey) +
        *new TSubMenu("Спр~а~вка", kbNoKey) +
            *new TMenuItem("~О~ программе...", cmAbout, kbNoKey));
}

TStatusLine *TBuilderApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    return new TStatusLine(r,
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~Alt-X~ Выход", kbAltX, cmQuit) +
            *new TStatusItem("~F2~ Сохранить", kbF2, cmSaveProject) +
            *new TStatusItem("~F3~ Открыть", kbF3, cmOpenProject) +
            *new TStatusItem("~F4~ Продукт", kbF4, cmProduct) +
            *new TStatusItem("~F5~ Компоненты", kbF5, cmComponents) +
            *new TStatusItem("~F9~ Собрать", kbF9, cmBuild) +
            *new TStatusItem("~F10~ Меню", kbF10, cmMenu));
}

void TBuilderApp::getEvent(TEvent &event)
{
    TApplication::getEvent(event);
    translateCyrillicHotkey(event, TopView());
}

void TBuilderApp::idle()
{
    TApplication::idle();
    if (!started)
    {
        started = true;
        start();
    }
}

void TBuilderApp::start()
{
    window = new TProjectWindow(deskTop->getExtent());
    deskTop->insert(window);
    std::string open = pending.empty() ? fsu::loadSetting("LastProject", "") : pending;
    pending.clear();
    if (open.empty() || !fsu::fileExists(open) || !openProject(open))
        newProject();
}

std::string TBuilderApp::projectDir() const
{
    return path.empty() ? std::string() : fsu::dirName(path);
}

void TBuilderApp::refresh()
{
    if (window)
        window->update(project, path, modified);
}

void TBuilderApp::edited(bool changed)
{
    if (changed)
    {
        modified = true;
        refresh();
    }
}

void TBuilderApp::newProject()
{
    project = defaultProject();
    path.clear();
    modified = false;
    lastSetup.clear();
    refresh();
}

bool TBuilderApp::openProject(const std::string &file)
{
    SetupProject p;
    std::string err;
    if (!loadProject(file, p, err))
    {
        showError(err);
        return false;
    }
    project = p;
    path = fsu::fullPath(file);
    modified = false;
    lastSetup.clear();
    lastDirectory() = fsu::dirName(path);
    fsu::saveSetting("LastProject", path);
    refresh();
    return true;
}

bool TBuilderApp::save(bool askName)
{
    std::string target = path;
    if (askName || target.empty())
    {
        if (!chooseFile("Сохранить проект", "*.xsp", true, target))
            return false;
        if (fsu::upper(fsu::extension(target)) != "XSP")
            target += ".xsp";
        if (target != path && fsu::fileExists(target) &&
            !confirm("Файл уже существует:\n" + target + "\n\nЗаменить его?"))
            return false;
    }
    // Пути, выбранные до первого сохранения, остаются абсолютными; при
    // смене директории проекта относительные пути пересчитываются.
    SetupProject p = project;
    std::string oldDir = projectDir(), newDir = fsu::dirName(fsu::fullPath(target));
    if (!oldDir.empty() && fsu::upper(oldDir) != fsu::upper(newDir))
    {
        auto rebase = [&](std::string &s) {
            if (!s.empty() && !fsu::isAbsolutePath(s))
                s = relativePath(fsu::fullPath(fsu::joinPath(oldDir, s)), newDir);
        };
        for (Component &c : p.components)
            rebase(c.source);
        rebase(p.license);
        rebase(p.outputDir);
    }
    std::string err;
    if (!saveProject(target, p, false, err))
    {
        showError(err);
        return false;
    }
    project = p;
    path = fsu::fullPath(target);
    modified = false;
    fsu::saveSetting("LastProject", path);
    refresh();
    return true;
}

bool TBuilderApp::confirmDiscard()
{
    if (!modified)
        return true;
    switch (confirm3("Проект изменён. Сохранить изменения?"))
    {
        case cmYes: return save(false);
        case cmNo:  return true;
        default:    return false;
    }
}

Boolean TBuilderApp::valid(ushort command)
{
    if (command == cmQuit && !confirmDiscard())
        return False;
    return TApplication::valid(command);
}

void TBuilderApp::build()
{
    if (path.empty() || modified)
    {
        if (!confirm(path.empty() ? "Перед сборкой проект необходимо сохранить. Сохранить?"
                                  : "Проект изменён. Сохранить и собрать?"))
            return;
        if (!save(false))
            return;
    }
    std::string problem = validateProject(project);
    if (!problem.empty())
    {
        showError(problem);
        return;
    }
    std::string stub, err;
    if (!loadSetupStub(stub, err))
    {
        showError(err);
        return;
    }
    BuildResult result;
    bool ok = runWithProgress("Сборка дистрибутива", [&](CabProgress *progress, std::string &e) {
        return buildDistribution(project, path, stub, progress, result, e);
    }, kBuildHoldMs);
    if (!ok)
        return;
    lastSetup = result.outputs.empty() ? std::string() : result.outputs.front();
    std::string text = "Дистрибутив собран: " + pluralFiles(result.files) + ", " + formatSize(result.bytes) + "\n";
    for (const std::string &o : result.outputs)
        text += "\n" + o + " (" + formatSize(fsu::fileSize(o)) + ")";
    showInfo(text);
}

void TBuilderApp::runSetup()
{
    std::string exe = lastSetup;
    if (exe.empty() && !path.empty())
    {
        std::string out = outputDirectory(project, path);
        exe = project.mode == BuildMode::Single ? fsu::joinPath(out, singleFileName(project))
            : project.mode == BuildMode::Both   ? fsu::joinPath(fsu::joinPath(out, "setup"), "setup.exe")
                                                : fsu::joinPath(out, "setup.exe");
    }
    if (exe.empty() || !fsu::fileExists(exe))
    {
        showError("Дистрибутив ещё не собран (F9).");
        return;
    }
    std::wstring cmd = L"\"" + fsu::widen(exe) + L"\"";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                        fsu::widen(fsu::dirName(exe)).c_str(), &si, &pi))
    {
        showError("Не удалось запустить " + exe + "\n" + fsu::lastErrorText());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

void TBuilderApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    if (event.what != evCommand)
        return;
    std::string dir = projectDir();
    switch (event.message.command)
    {
        case cmNewProject:
            if (confirmDiscard())
                newProject();
            break;
        case cmOpenProject:
        {
            std::string file;
            if (confirmDiscard() && chooseFile("Открыть проект", "*.xsp", false, file))
                openProject(file);
            break;
        }
        case cmSaveProject:     save(false); break;
        case cmSaveProjectAs:   save(true); break;
        case cmProduct:         edited(productDialog(project, dir)); break;
        case cmDirectories:     edited(directoriesDialog(project)); break;
        case cmComponents:      edited(componentsDialog(project, dir)); break;
        case cmShortcuts:       edited(shortcutsDialog(project)); break;
        case cmRegistry:        edited(registryDialog(project)); break;
        case cmProcesses:       edited(processesDialog(project)); break;
        case cmRunAfter:        edited(runAfterDialog(project)); break;
        case cmBuildOptions:    edited(buildOptionsDialog(project, dir)); break;
        case cmShowFiles:       showComponentFiles(project, dir); break;
        case cmBuild:           build(); break;
        case cmRunSetup:        runSetup(); break;
        case cmAbout:
            showInfo("XPSetup " XPSETUP_VERSION " — сборщик дистрибутивов\n" XPSETUP_COPYRIGHT
                     "\n\nПрограмма установки в стиле текстового режима Windows XP, "
                     "файлы программы хранятся в CAB-архивах.");
            break;
        default:
            return;
    }
    clearEvent(event);
}
