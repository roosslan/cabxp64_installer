#define Uses_TKeys
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TGroup
#define Uses_TDialog
#define Uses_TButton
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TInputLine
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TSItem
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include <windows.h>
#include <shlwapi.h>

#include "../common/format.h"
#include "../common/fsutil.h"
#include "../tv/commands.h"
#include "../tv/dialogs.h"
#include "build.h"
#include "textview.h"
#include "projdlg.h"

namespace {

const size_t npos = (size_t) -1;

// Дополнение строки пробелами до ширины в символах (UTF-8).
std::string pad(const std::string &s, int width)
{
    int n = (int) fsu::widen(s).size();
    return n >= width ? s + " " : s + std::string(width - n, ' ');
}

// TCluster::drawMultiBox не рисует пункт, если длина его текста в байтах
// плюс 5 не меньше 80: русский текст длиннее ~37 букв пропадает. Текст
// пункта сокращается до безопасной длины.
std::string clusterText(const std::string &s)
{
    const size_t limit = 70;
    if (s.size() <= limit)
        return s;
    size_t n = limit - 3;
    while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80)
        --n;
    return s.substr(0, n) + "...";
}

} // namespace

std::string relativePath(const std::string &path, const std::string &baseDir)
{
    if (baseDir.empty() || path.empty())
        return path;
    wchar_t out[MAX_PATH];
    DWORD attr = fsu::dirExists(path) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (!PathRelativePathToW(out, fsu::widen(baseDir).c_str(), FILE_ATTRIBUTE_DIRECTORY,
                             fsu::widen(path).c_str(), attr))
        return path;   // другой диск
    std::string r = fsu::narrow(out);
    if (r.compare(0, 2, ".\\") == 0)
        r.erase(0, 2);
    return r.empty() ? "." : r;
}

// ---------------------------------------------------------------------------
// Редактор списков
// ---------------------------------------------------------------------------

namespace {

class TRowsView : public TListViewer
{
public:
    TRowsView(const TRect &bounds, TScrollBar *vsb, std::vector<std::string> &rows) :
        TListViewer(bounds, 1, nullptr, vsb), rows(rows)
    {
    }

    void getText(char *dest, short item, short maxLen) override
    {
        const std::string &s = item < (short) rows.size() ? rows[item] : std::string();
        size_t n = std::min(s.size(), (size_t) maxLen);
        // Не обрывать многобайтный символ UTF-8.
        while (n > 0 && n < s.size() && ((unsigned char) s[n] & 0xC0) == 0x80)
            --n;
        memcpy(dest, s.data(), n);
        dest[n] = 0;
    }

    void selectItem(short) override
    {
        message(owner, evCommand, cmListEdit, this);
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evKeyDown)
        {
            ushort cmd = ev.keyDown.keyCode == kbEnter ? cmListEdit
                       : ev.keyDown.keyCode == kbIns   ? cmListAdd
                       : ev.keyDown.keyCode == kbDel   ? cmListDelete
                                                       : 0;
            if (cmd)
            {
                message(owner, evCommand, cmd, this);
                clearEvent(ev);
                return;
            }
        }
        TListViewer::handleEvent(ev);
    }

private:
    std::vector<std::string> &rows;
};

struct ListModel
{
    std::function<size_t()> count;
    std::function<std::string(size_t)> text;
    std::function<bool(size_t)> edit;           // npos — добавление
    std::function<void(size_t)> remove;
    std::function<std::string(size_t)> blocker; // причина запрета удаления
    std::function<void(size_t, size_t)> swap;   // пусто — порядок не важен
};

class TListEditDialog : public TDialog
{
public:
    TListEditDialog(const char *title, const char *label, const char *heading, ListModel &model) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 78, 21), title),
        model(model)
    {
        options |= ofCentered;
        // Под меткой списка — необязательная строка заголовков колонок.
        int y = heading && *heading ? 4 : 3;
        if (y == 4)
            insert(new TStaticText(TRect(3, 3, 60, 4), heading));
        TScrollBar *sb = new TScrollBar(TRect(60, y, 61, 19));
        insert(sb);
        list = new TRowsView(TRect(3, y, 60, 19), sb, rows);
        insert(list);
        insert(new TLabel(TRect(2, 2, 50, 3), label, list));
        insert(new TButton(TRect(63, 3, 76, 5), "~Д~обавить", cmListAdd, bfNormal));
        insert(new TButton(TRect(63, 5, 76, 7), "~И~зменить", cmListEdit, bfNormal));
        insert(new TButton(TRect(63, 7, 76, 9), "~У~далить", cmListDelete, bfNormal));
        if (model.swap)
        {
            insert(new TButton(TRect(63, 10, 76, 12), "~В~верх", cmListUp, bfNormal));
            insert(new TButton(TRect(63, 12, 76, 14), "Вн~и~з", cmListDown, bfNormal));
        }
        insert(new TButton(TRect(63, 15, 76, 17), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(63, 17, 76, 19), "Отмена", cmCancel, bfNormal));
        refresh(0);
        list->select();
    }

    void handleEvent(TEvent &ev) override
    {
        TDialog::handleEvent(ev);
        if (ev.what != evCommand)
            return;
        size_t cur = list->focused >= 0 && list->focused < (short) rows.size() ? (size_t) list->focused : npos;
        switch (ev.message.command)
        {
            case cmListAdd:
                if (model.edit(npos))
                    refresh(model.count() - 1);
                break;
            case cmListEdit:
                if (cur != npos && model.edit(cur))
                    refresh(cur);
                break;
            case cmListDelete:
                if (cur != npos)
                {
                    std::string why = model.blocker ? model.blocker(cur) : std::string();
                    if (!why.empty())
                        showError(why);
                    else if (confirm("Удалить элемент?\n\n" + model.text(cur)))
                    {
                        model.remove(cur);
                        refresh(cur > 0 && cur >= model.count() ? cur - 1 : cur);
                    }
                }
                break;
            case cmListUp:
                if (model.swap && cur != npos && cur > 0)
                {
                    model.swap(cur, cur - 1);
                    refresh(cur - 1);
                }
                break;
            case cmListDown:
                if (model.swap && cur != npos && cur + 1 < model.count())
                {
                    model.swap(cur, cur + 1);
                    refresh(cur + 1);
                }
                break;
            default:
                return;
        }
        list->select();
        clearEvent(ev);
    }

private:
    void refresh(size_t focus)
    {
        rows.clear();
        for (size_t i = 0; i < model.count(); ++i)
            rows.push_back(model.text(i));
        list->setRange((short) rows.size());
        if (!rows.empty())
            list->focusItemNum((short) std::min(focus, rows.size() - 1));
        list->drawView();
    }

    ListModel &model;
    std::vector<std::string> rows;
    TRowsView *list;
};

template <class T>
bool editList(const char *title, const char *label, const char *heading, std::vector<T> &items,
              std::function<std::string(const T &)> text,
              std::function<bool(T &, bool)> editItem, bool ordered = true,
              std::function<std::string(const T &)> blocker = nullptr)
{
    std::vector<T> work = items;
    ListModel m;
    m.count = [&] { return work.size(); };
    m.text = [&](size_t i) { return text(work[i]); };
    m.edit = [&](size_t i) {
        T item = i == npos ? T() : work[i];
        if (!editItem(item, i == npos))
            return false;
        if (i == npos)
            work.push_back(item);
        else
            work[i] = item;
        return true;
    };
    m.remove = [&](size_t i) { work.erase(work.begin() + i); };
    if (blocker)
        m.blocker = [&](size_t i) { return blocker(work[i]); };
    if (ordered)
        m.swap = [&](size_t a, size_t b) { std::swap(work[a], work[b]); };

    TListEditDialog *d = new TListEditDialog(title, label, heading, m);
    bool ok = TProgram::deskTop->execView(d) == cmOK;
    TObject::destroy(d);
    if (ok)
        items = work;
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// Продукт
// ---------------------------------------------------------------------------

namespace {

class TProductDialog : public TDialog
{
public:
    TProductDialog(SetupProject &p, const std::string &projectDir) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 76, 22), "Продукт"),
        p(p), projectDir(projectDir)
    {
        options |= ofCentered;
        name = addInput(2, "~Н~азвание:", p.name, 56);
        version = addInput(4, "~В~ерсия:", p.version, 24);
        publisher = addInput(6, "~И~здатель:", p.publisher, 56);
        code = addInput(8, "~К~од продукта:", p.productCode, 44);
        insert(new TButton(TRect(58, 8, 72, 10), "Но~в~ый", cmNewGuid, bfNormal));
        installDir = addInput(10, "~Д~иректория установки:", p.installDir, 73);
        license = addInput(12, "~Л~ицензия (файл):", p.license, 56);
        insert(new TButton(TRect(58, 12, 72, 14), "~О~бзор...", cmBrowseLicense, bfNormal));

        flags = new TCheckBoxes(TRect(3, 15, 40, 16),
            new TSItem("Разрешить менять директорию", nullptr));
        insert(flags);
        setClusterValue(flags, p.allowChangeDir ? 1 : 0);
        scope = new TRadioButtons(TRect(3, 17, 36, 19),
            new TSItem("Для текущего пользователя",
            new TSItem("Для всех пользователей", nullptr)));
        insert(scope);
        insert(new TLabel(TRect(2, 16, 30, 17), "~У~становка:", scope));
        insert(new TStaticText(TRect(37, 18, 72, 19), "— нужны права администратора"));
        setClusterValue(scope, p.perMachine ? 1 : 0);

        insert(new TButton(TRect(24, 19, 36, 21), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(38, 19, 50, 21), "Отмена", cmCancel, bfNormal));
        selectNext(False);
    }

    void handleEvent(TEvent &ev) override
    {
        TDialog::handleEvent(ev);
        if (ev.what != evCommand)
            return;
        if (ev.message.command == cmNewGuid)
            setInputText(code, newGuid());
        else if (ev.message.command == cmBrowseLicense)
        {
            std::string path;
            if (chooseFile("Файл лицензии", "*.txt", false, path))
                setInputText(license, relativePath(path, projectDir));
        }
        else
            return;
        clearEvent(ev);
    }

    Boolean valid(ushort command) override
    {
        if (command == cmOK)
        {
            std::string n = name->data, c = code->data, d = installDir->data;
            if (n.empty())
            {
                showError("Необходимо указать название продукта.");
                return False;
            }
            if (c.size() != 38 || c.front() != '{' || c.back() != '}')
            {
                showError("Код продукта должен быть GUID в фигурных скобках, например\n" + newGuid());
                return False;
            }
            if (d.empty())
            {
                showError("Необходимо указать директорию установки.");
                return False;
            }
            p.name = n;
            p.version = version->data;
            p.publisher = publisher->data;
            p.productCode = fsu::upper(c);
            p.installDir = d;
            p.license = license->data;
            p.allowChangeDir = (clusterValue(flags) & 1) != 0;
            p.perMachine = clusterValue(scope) == 1;
        }
        return TDialog::valid(command);
    }

private:
    TInputLine *addInput(int y, const char *label, const std::string &value, int x2)
    {
        TInputLine *il = new TInputLine(TRect(3, y + 1, x2, y + 2), 1024);
        insert(il);
        insert(new TLabel(TRect(2, y, 40, y + 1), label, il));
        setInputText(il, value);
        return il;
    }

    SetupProject &p;
    std::string projectDir;
    TInputLine *name, *version, *publisher, *code, *installDir, *license;
    TCheckBoxes *flags;
    TRadioButtons *scope;
};

} // namespace

bool productDialog(SetupProject &p, const std::string &projectDir)
{
    TProductDialog *d = new TProductDialog(p, projectDir);
    bool ok = TProgram::deskTop->execView(d) == cmOK;
    TObject::destroy(d);
    return ok;
}

// ---------------------------------------------------------------------------
// Директории
// ---------------------------------------------------------------------------

namespace {

struct DirEntry
{
    int id = 0;
    std::string path;
};

bool editPath(const char *title, const char *label, std::string &value)
{
    std::string v = value;
    if (!askText(title, label, v))
        return false;
    value = v;
    return true;
}

} // namespace

bool directoriesDialog(SetupProject &p)
{
    std::vector<DirEntry> items;
    for (const auto &d : p.directories)
        items.push_back({d.first, d.second});
    auto nextId = [&items]() {
        int m = 0;
        for (const DirEntry &e : items)
            m = std::max(m, e.id);
        return m + 1;
    };
    int newId = nextId();
    bool ok = editList<DirEntry>("Директории", "~Д~иректории установки:",
        nullptr, items,
        [](const DirEntry &e) { return pad(std::to_string(e.id), 4) + e.path; },
        [&](DirEntry &e, bool isNew) {
            if (isNew)
            {
                e.id = newId;
                e.path = "%INSTALLDIR%\\";
            }
            if (!editPath(isNew ? "Новая директория" : "Директория",
                          "~П~уть (допускаются %INSTALLDIR%, %APPDATA% и т. п.):", e.path))
                return false;
            if (isNew)
                ++newId;
            return true;
        },
        false,
        [&p](const DirEntry &e) -> std::string {
            for (const Component &c : p.components)
                if (c.directory == e.id)
                    return "Директория " + std::to_string(e.id) + " используется компонентом " + c.id + ".";
            return {};
        });
    if (!ok)
        return false;
    p.directories.clear();
    for (const DirEntry &e : items)
        p.directories[e.id] = e.path;
    return true;
}

// ---------------------------------------------------------------------------
// Компоненты
// ---------------------------------------------------------------------------

namespace {

class TComponentDialog : public TDialog
{
public:
    // Высота зависит от числа директорий: размер окна задаётся сразу, рамка
    // создаётся по нему и при последующем setBounds не изменилась бы.
    static int rowsFor(const SetupProject &p) { return std::max(1, (int) p.directories.size()); }

    TComponentDialog(Component &c, const SetupProject &p, const std::string &projectDir) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 76, 21 + rowsFor(p)), "Компонент"),
        c(c), projectDir(projectDir)
    {
        for (const auto &d : p.directories)
            dirIds.push_back(d.first);
        int dirRows = rowsFor(p);
        options |= ofCentered;

        id = addInput(2, 3, 30, "~И~дентификатор:", c.id);
        cabinet = addInput(2, 40, 72, "Имя ~C~AB:", c.cabinet);
        description = addInput(4, 3, 72, "~О~писание:", c.description);
        source = addInput(6, 3, 57, "~И~сточник (директория или файл):", c.source);
        insert(new TButton(TRect(58, 7, 72, 9), "~О~бзор...", cmBrowseSource, bfNormal));
        include = addInput(9, 3, 35, "~В~ключать (маски):", c.include);
        exclude = addInput(9, 38, 72, "~И~сключать (маски, пути):", c.exclude);
        flags = new TCheckBoxes(TRect(3, 12, 50, 13), new TSItem("Включать поддиректории", nullptr));
        insert(flags);
        setClusterValue(flags, c.recurse ? 1 : 0);

        TSItem *items = nullptr;
        for (auto it = p.directories.rbegin(); it != p.directories.rend(); ++it)
            items = new TSItem(clusterText(std::to_string(it->first) + "  " + it->second), items);
        if (!items)
            items = new TSItem("(нет директорий)", nullptr);
        dir = new TRadioButtons(TRect(3, 15, 72, 15 + dirRows), items);
        insert(dir);
        insert(new TLabel(TRect(2, 14, 40, 15), "~Д~иректория установки:", dir));
        auto pos = std::find(dirIds.begin(), dirIds.end(), c.directory);
        setClusterValue(dir, pos == dirIds.end() ? 0 : (ushort) (pos - dirIds.begin()));

        int y = 16 + dirRows;
        insert(new TButton(TRect(14, y, 30, y + 2), "~Ф~айлы...", cmShowFiles, bfNormal));
        insert(new TButton(TRect(32, y, 44, y + 2), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(46, y, 58, y + 2), "Отмена", cmCancel, bfNormal));
        selectNext(False);
    }

    void handleEvent(TEvent &ev) override
    {
        TDialog::handleEvent(ev);
        if (ev.what != evCommand)
            return;
        if (ev.message.command == cmBrowseSource)
        {
            std::string start = source->data;
            if (!projectDir.empty() && !fsu::isAbsolutePath(start))
                start = fsu::joinPath(projectDir, start);
            std::string d;
            if (chooseDirectory(fsu::existingDir(start), d))
                setInputText(source, relativePath(d, projectDir));
        }
        else if (ev.message.command == cmShowFiles)
        {
            Component probe = c;
            store(probe);
            ComponentFiles files;
            std::string err;
            if (!collectComponent(probe, projectDir.empty() ? fsu::currentDir() : projectDir, files, err))
                showError(err);
            else
            {
                std::string text = pluralFiles(files.files.size()) + ", " + formatSize(files.bytes) + "\n";
                size_t shown = std::min<size_t>(files.files.size(), 300);
                for (size_t i = 0; i < shown; ++i)
                    text += "\n" + files.files[i].nameInCab;
                if (shown < files.files.size())
                    text += "\n...";
                scrollTextDialog(("Файлы компонента " + probe.id).c_str(), text);
            }
        }
        else
            return;
        clearEvent(ev);
    }

    Boolean valid(ushort command) override
    {
        if (command == cmOK)
        {
            Component t = c;
            store(t);
            if (t.id.empty() || t.id.find_first_of(" \t[].\\/\"") != std::string::npos)
            {
                showError("Идентификатор обязателен и не должен содержать пробелов, точек и скобок.");
                return False;
            }
            if (t.cabinet.empty() || t.cabinet.find_first_of("\\/:*?\"<>|") != std::string::npos)
            {
                showError("Необходимо указать допустимое имя файла CAB.");
                return False;
            }
            if (dirIds.empty())
            {
                showError("Сначала необходимо задать директории установки.");
                return False;
            }
            c = t;
        }
        return TDialog::valid(command);
    }

private:
    TInputLine *addInput(int y, int x1, int x2, const char *label, const std::string &value)
    {
        TInputLine *il = new TInputLine(TRect(x1, y + 1, x2, y + 2), 1024);
        insert(il);
        insert(new TLabel(TRect(x1 - 1, y, x2, y + 1), label, il));
        setInputText(il, value);
        return il;
    }

    void store(Component &t)
    {
        t.id = id->data;
        t.cabinet = cabinet->data;
        if (!t.cabinet.empty() && fsu::upper(fsu::extension(t.cabinet)) != "CAB")
            t.cabinet += ".cab";
        t.description = description->data;
        t.source = source->data;
        t.include = include->data;
        t.exclude = exclude->data;
        t.recurse = (clusterValue(flags) & 1) != 0;
        ushort v = clusterValue(dir);
        t.directory = v < dirIds.size() ? dirIds[v] : 1;
    }

    Component &c;
    std::string projectDir;
    std::vector<int> dirIds;
    TInputLine *id, *cabinet, *description, *source, *include, *exclude;
    TCheckBoxes *flags;
    TRadioButtons *dir;
};

} // namespace

bool componentsDialog(SetupProject &p, const std::string &projectDir)
{
    return editList<Component>("Компоненты", "~К~омпоненты (CAB-архивы):",
        " Идентификатор   CAB              Дир. Источник", p.components,
        [](const Component &c) {
            return pad(c.id, 16) + pad(c.cabinet, 17) + pad(std::to_string(c.directory), 5) + c.source;
        },
        [&](Component &c, bool isNew) {
            if (isNew)
            {
                int n = (int) p.components.size() + 1;
                c.id = "component" + std::to_string(n);
                c.cabinet = c.id + ".cab";
                c.directory = p.directories.empty() ? 1 : p.directories.begin()->first;
            }
            TComponentDialog *d = new TComponentDialog(c, p, projectDir);
            bool ok = TProgram::deskTop->execView(d) == cmOK;
            TObject::destroy(d);
            return ok;
        });
}

// ---------------------------------------------------------------------------
// Ярлыки
// ---------------------------------------------------------------------------

bool shortcutsDialog(SetupProject &p)
{
    return editList<Shortcut>("Ярлыки", "~Я~рлыки:", nullptr, p.shortcuts,
        [](const Shortcut &s) { return pad(placeTitle(s.place), 15) + pad(s.name, 22) + s.target; },
        [](Shortcut &s, bool isNew) {
            if (isNew)
                s.target = "%INSTALLDIR%\\";
            TDialog *d = new TDialog(TRect(0, 0, 72, 20), "Ярлык");
            d->options |= ofCentered;
            TRadioButtons *place = new TRadioButtons(TRect(3, 3, 40, 6),
                new TSItem("Рабочий стол",
                new TSItem("Меню «Пуск»",
                new TSItem("Автозагрузка", nullptr))));
            d->insert(place);
            d->insert(new TLabel(TRect(2, 2, 30, 3), "~Р~асположение:", place));
            setClusterValue(place, (ushort) s.place);
            auto add = [d](int y, const char *label, const std::string &v) {
                TInputLine *il = new TInputLine(TRect(3, y + 1, 69, y + 2), 1024);
                d->insert(il);
                d->insert(new TLabel(TRect(2, y, 69, y + 1), label, il));
                setInputText(il, v);
                return il;
            };
            TInputLine *name = add(7, "~И~мя (для меню «Пуск» — Группа\\Имя):", s.name);
            TInputLine *target = add(9, "~О~бъект:", s.target);
            TInputLine *args = add(11, "~П~араметры:", s.args);
            TInputLine *work = add(13, "Рабочая ~д~иректория (пусто — директория объекта):", s.workDir);
            d->insert(new TButton(TRect(22, 17, 34, 19), "O~K~", cmOK, bfDefault));
            d->insert(new TButton(TRect(36, 17, 48, 19), "Отмена", cmCancel, bfNormal));
            d->selectNext(False);
            bool ok = TProgram::deskTop->execView(d) == cmOK && *name->data && *target->data;
            if (ok)
            {
                s.place = (ShortcutPlace) clusterValue(place);
                s.name = name->data;
                s.target = target->data;
                s.args = args->data;
                s.workDir = work->data;
            }
            TObject::destroy(d);
            return ok;
        });
}

// ---------------------------------------------------------------------------
// Реестр
// ---------------------------------------------------------------------------

bool registryDialog(SetupProject &p)
{
    static const char *const kTypes[] = {"REG_SZ", "REG_EXPAND_SZ", "REG_DWORD"};
    return editList<RegistryValue>("Реестр", "~З~начения реестра:", nullptr, p.registry,
        [](const RegistryValue &r) {
            return r.root + "\\" + r.key + " : " + (r.name.empty() ? "(по умолчанию)" : r.name) + " = " + r.value;
        },
        [](RegistryValue &r, bool isNew) {
            if (isNew)
                r.key = "Software\\";
            TDialog *d = new TDialog(TRect(0, 0, 72, 20), "Значение реестра");
            d->options |= ofCentered;
            TRadioButtons *root = new TRadioButtons(TRect(3, 3, 34, 5),
                new TSItem("HKCU (пользователь)",
                new TSItem("HKLM (компьютер)", nullptr)));
            d->insert(root);
            d->insert(new TLabel(TRect(2, 2, 30, 3), "~К~орень:", root));
            setClusterValue(root, fsu::upper(r.root) == "HKLM" ? 1 : 0);
            TRadioButtons *type = new TRadioButtons(TRect(37, 3, 69, 6),
                new TSItem("REG_SZ",
                new TSItem("REG_EXPAND_SZ",
                new TSItem("REG_DWORD", nullptr))));
            d->insert(type);
            d->insert(new TLabel(TRect(36, 2, 60, 3), "~Т~ип:", type));
            ushort t = 0;
            for (ushort i = 0; i < 3; ++i)
                if (r.type == kTypes[i])
                    t = i;
            setClusterValue(type, t);
            auto add = [d](int y, const char *label, const std::string &v) {
                TInputLine *il = new TInputLine(TRect(3, y + 1, 69, y + 2), 1024);
                d->insert(il);
                d->insert(new TLabel(TRect(2, y, 69, y + 1), label, il));
                setInputText(il, v);
                return il;
            };
            TInputLine *key = add(7, "~Р~аздел:", r.key);
            TInputLine *name = add(9, "~П~араметр (пусто — по умолчанию):", r.name);
            TInputLine *value = add(11, "~З~начение (допускается %INSTALLDIR%):", r.value);
            d->insert(new TButton(TRect(22, 17, 34, 19), "O~K~", cmOK, bfDefault));
            d->insert(new TButton(TRect(36, 17, 48, 19), "Отмена", cmCancel, bfNormal));
            d->selectNext(False);
            bool ok = TProgram::deskTop->execView(d) == cmOK && *key->data;
            if (ok)
            {
                r.root = clusterValue(root) == 1 ? "HKLM" : "HKCU";
                r.type = kTypes[std::min<ushort>(clusterValue(type), 2)];
                r.key = key->data;
                r.name = name->data;
                r.value = value->data;
            }
            TObject::destroy(d);
            return ok;
        });
}

// ---------------------------------------------------------------------------
// Процессы и запуск после установки
// ---------------------------------------------------------------------------

bool processesDialog(SetupProject &p)
{
    return editList<std::string>("Процессы", "~З~авершать перед установкой и удалением:", nullptr,
        p.killProcesses,
        [](const std::string &s) { return s; },
        [](std::string &s, bool) { return askText("Процесс", "~И~мя файла (например, app.exe):", s); });
}

bool runAfterDialog(SetupProject &p)
{
    return editList<RunEntry>("Запуск после установки", "~П~рограммы:", nullptr, p.runAfter,
        [](const RunEntry &r) { return r.program + (r.args.empty() ? "" : " " + r.args); },
        [](RunEntry &r, bool isNew) {
            if (isNew)
                r.program = "%INSTALLDIR%\\";
            TDialog *d = new TDialog(TRect(0, 0, 72, 11), "Запуск после установки");
            d->options |= ofCentered;
            TInputLine *prog = new TInputLine(TRect(3, 3, 69, 4), 1024);
            d->insert(prog);
            d->insert(new TLabel(TRect(2, 2, 60, 3), "~П~рограмма:", prog));
            setInputText(prog, r.program);
            TInputLine *args = new TInputLine(TRect(3, 6, 69, 7), 1024);
            d->insert(args);
            d->insert(new TLabel(TRect(2, 5, 60, 6), "П~а~раметры:", args));
            setInputText(args, r.args);
            d->insert(new TButton(TRect(22, 8, 34, 10), "O~K~", cmOK, bfDefault));
            d->insert(new TButton(TRect(36, 8, 48, 10), "Отмена", cmCancel, bfNormal));
            d->selectNext(False);
            bool ok = TProgram::deskTop->execView(d) == cmOK && *prog->data;
            if (ok)
            {
                r.program = prog->data;
                r.args = args->data;
            }
            TObject::destroy(d);
            return ok;
        });
}

// ---------------------------------------------------------------------------
// Параметры сборки
// ---------------------------------------------------------------------------

namespace {

class TBuildDialog : public TDialog
{
public:
    TBuildDialog(SetupProject &p, const std::string &projectDir) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 72, 19), "Параметры сборки"),
        p(p), projectDir(projectDir)
    {
        options |= ofCentered;
        output = new TInputLine(TRect(3, 3, 54, 4), 1024);
        insert(output);
        insert(new TLabel(TRect(2, 2, 50, 3), "~Д~иректория результата:", output));
        setInputText(output, p.outputDir);
        insert(new TButton(TRect(55, 2, 69, 4), "~О~бзор...", cmBrowseOutput, bfNormal));

        mode = new TRadioButtons(TRect(3, 6, 34, 9),
            new TSItem("Директория",
            new TSItem("Один файл",
            new TSItem("Оба варианта", nullptr))));
        insert(mode);
        insert(new TLabel(TRect(2, 5, 30, 6), "~В~ид дистрибутива:", mode));
        setClusterValue(mode, (ushort) p.mode);

        comp = new TRadioButtons(TRect(37, 6, 69, 9),
            new TSItem("Без сжатия",
            new TSItem("MSZIP (быстрое)",
            new TSItem("LZX (максимальное)", nullptr))));
        insert(comp);
        insert(new TLabel(TRect(36, 5, 60, 6), "~С~жатие:", comp));
        setClusterValue(comp, (ushort) p.compression.type);

        single = new TInputLine(TRect(3, 11, 69, 12), 1024);
        insert(single);
        insert(new TLabel(TRect(2, 10, 69, 11), "~И~мя файла (пусто — <название>_setup.exe):", single));
        setInputText(single, p.singleFile);

        insert(new TButton(TRect(22, 15, 34, 17), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(36, 15, 48, 17), "Отмена", cmCancel, bfNormal));
        selectNext(False);
    }

    void handleEvent(TEvent &ev) override
    {
        TDialog::handleEvent(ev);
        if (ev.what == evCommand && ev.message.command == cmBrowseOutput)
        {
            std::string start = output->data;
            if (!projectDir.empty() && !fsu::isAbsolutePath(start))
                start = fsu::joinPath(projectDir, start);
            std::string d;
            if (chooseDirectory(fsu::existingDir(start), d))
                setInputText(output, relativePath(d, projectDir));
            clearEvent(ev);
        }
    }

    Boolean valid(ushort command) override
    {
        if (command == cmOK)
        {
            if (!*output->data)
            {
                showError("Необходимо указать директорию результата.");
                return False;
            }
            std::string s = single->data;
            if (!s.empty() && s.find_first_of("\\/:*?\"<>|") != std::string::npos)
            {
                showError("Имя файла не должно содержать путь и символы \\ / : * ? \" < > |");
                return False;
            }
            if (!s.empty() && fsu::upper(fsu::extension(s)) != "EXE")
                s += ".exe";
            p.outputDir = output->data;
            p.mode = (BuildMode) clusterValue(mode);
            p.compression.type = (CabCompression) clusterValue(comp);
            p.singleFile = s;
        }
        return TDialog::valid(command);
    }

private:
    SetupProject &p;
    std::string projectDir;
    TInputLine *output, *single;
    TRadioButtons *mode, *comp;
};

} // namespace

bool buildOptionsDialog(SetupProject &p, const std::string &projectDir)
{
    TBuildDialog *d = new TBuildDialog(p, projectDir);
    bool ok = TProgram::deskTop->execView(d) == cmOK;
    TObject::destroy(d);
    return ok;
}

void showComponentFiles(const SetupProject &p, const std::string &projectDir)
{
    std::string text;
    uint64_t totalFiles = 0, totalBytes = 0;
    for (const Component &c : p.components)
    {
        ComponentFiles files;
        std::string err;
        std::string base = projectDir.empty() ? fsu::currentDir() : projectDir;
        if (!collectComponent(c, base, files, err))
        {
            text += "\n" + err + "\n";
            continue;
        }
        totalFiles += files.files.size();
        totalBytes += files.bytes;
        text += "\n[" + c.id + "] " + c.cabinet + " — " + pluralFiles(files.files.size()) + ", " +
                formatSize(files.bytes) + "\n";
        size_t shown = std::min<size_t>(files.files.size(), 100);
        for (size_t i = 0; i < shown; ++i)
            text += "    " + files.files[i].nameInCab + "\n";
        if (shown < files.files.size())
            text += "    ...\n";
    }
    scrollTextDialog("Файлы дистрибутива", "Всего: " + pluralFiles(totalFiles) + ", " + formatSize(totalBytes) + "\n" + text);
}
