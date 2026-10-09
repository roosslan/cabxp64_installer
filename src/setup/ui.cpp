#include "ui.h"

#include <algorithm>
#include <vector>

#include <windows.h>

#include "../common/fsutil.h"
#include "../common/format.h"
#include "screen.h"

namespace ui {

namespace {

// ---------------------------------------------------------------------------
// Тексты экранов. {name} заменяется названием продукта.
// ---------------------------------------------------------------------------

const char *const kCheckingHardware = "Программа установки проверяет конфигурацию оборудования компьютера...";
const char *const kLoadingStatus = "Программа установки загружает файлы ({file})...";

const char *const kWelcome =
    "   Вас приветствует программа установки.\n"
    "\n"
    "   Этот модуль программы установки подготавливает {name}\n"
    "   к работе на этом компьютере.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы приступить к установке {name}, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, не устанавливая {name}, нажмите <F3>.";
const char *const kWelcomeKeys = "ВВОД=Продолжить  F3=Выход";

const char *const kLicenseTitle = "Лицензионное соглашение {name}";
const char *const kLicenseKeys = "F8=Принимаю  ESC=Не принимаю  PAGE DOWN=Следующая страница";

const char *const kExitBox =
    "{name} не установлена на этом компьютере. Если выйти из программы установки сейчас, "
    "то для установки {name} потребуется запустить её снова.\n"
    "\n"
    "   \xE2\x80\xA2  Чтобы продолжить установку, нажмите <ВВОД>.\n"
    "\n"
    "   \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kExitKeys = "F3=Выход  ВВОД=Продолжить";

const char *const kDirText = "   Программа установки установит {name} в следующую директорию:";
const char *const kDirOptions =
    "      \xE2\x80\xA2  Чтобы установить {name} в эту директорию, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы установить в другую директорию, исправьте путь и нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kDirOptionsFixed =
    "      \xE2\x80\xA2  Чтобы установить {name} в эту директорию, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kDirKeys = "ВВОД=Продолжить  ESC=Отменить изменения  F3=Выход";
const char *const kDirKeysFixed = "ВВОД=Продолжить  F3=Выход";

const char *const kExistingUpdate =
    "   Программа установки обнаружила, что на этом компьютере уже установлена\n"
    "   программа {name} версии {old}:\n"
    "\n"
    "      {dir}\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы обновить её до версии {new}, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kExistingSame =
    "   Программа установки обнаружила, что на этом компьютере уже установлена\n"
    "   программа {name} версии {old}:\n"
    "\n"
    "      {dir}\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы переустановить {name}, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kExistingNewer =
    "   На этом компьютере уже установлена более новая версия {name} ({old}):\n"
    "\n"
    "      {dir}\n"
    "\n"
    "   Установка версии {new} поверх неё невозможна. Чтобы установить эту\n"
    "   версию, необходимо сначала удалить установленную программу.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>.";
const char *const kExistingKeys = "ВВОД=Продолжить  F3=Выход";

const char *const kRetryText =
    "   Программа установки не может скопировать файлы:\n"
    "\n"
    "{err}\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы повторить попытку, нажмите <ВВОД>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти из программы установки, нажмите <F3>. Скопированные\n"
    "         файлы будут удалены.";
const char *const kRetryKeys = "ВВОД=Повторить  F3=Выход";

const char *const kUninstallText =
    "   Программа удаления подготовлена к удалению {name} {version}\n"
    "   с этого компьютера.\n"
    "\n"
    "   Директория установки:\n"
    "\n"
    "      {dir}\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы удалить {name}, нажмите <L>.\n"
    "\n"
    "      \xE2\x80\xA2  Чтобы выйти без удаления, нажмите <ESC>.";
const char *const kUninstallKeys = "L=Удалить  ESC=Отмена";

// ---------------------------------------------------------------------------

std::string replaceAll(std::string s, const std::string &from, const std::string &to)
{
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
    return s;
}

std::string subst(const std::string &s, const std::string &key, const std::string &value)
{
    return replaceAll(s, "{" + key + "}", value);
}

std::string named(const char *text, const std::string &name)
{
    return subst(text, "name", name);
}

// Строки текста с переносом по словам (абзацы сохраняются).
std::vector<std::string> wrapLines(const std::string &text, int width)
{
    std::vector<std::string> out;
    std::wstring w = fsu::widen(text);
    w.erase(std::remove(w.begin(), w.end(), L'\r'), w.end());
    size_t pos = 0;
    while (pos <= w.size())
    {
        size_t nl = w.find(L'\n', pos);
        std::wstring line = w.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = nl == std::wstring::npos ? w.size() + 1 : nl + 1;
        for (wchar_t &c : line)
            if (c == L'\t')
                c = L' ';
        if (line.empty())
        {
            out.push_back({});
            continue;
        }
        while (!line.empty())
        {
            size_t cut = line.size();
            if ((int) cut > width)
            {
                cut = line.rfind(L' ', width);
                if (cut == std::wstring::npos || cut == 0)
                    cut = width;
            }
            out.push_back(fsu::narrow(line.substr(0, cut)));
            line = line.substr(cut);
            size_t skip = line.find_first_not_of(L' ');
            line = skip == std::wstring::npos ? std::wstring() : line.substr(skip);
        }
    }
    return out;
}

// Подтверждение выхода по F3: рамка поверх текущего экрана.
bool confirmExit(const std::string &name)
{
    const int x1 = 8, x2 = 71, y1 = 7;
    std::vector<std::string> lines = wrapLines(named(kExitBox, name), x2 - x1 - 5);
    int y2 = y1 + (int) lines.size() + 3;
    for (int y = y1; y <= y2; ++y)
        scr::fill(x1, y, x2 - x1 + 1, scr::Text);
    scr::box(x1, y1, x2, y2, scr::Text, true);
    for (size_t i = 0; i < lines.size(); ++i)
        scr::put(x1 + 3, y1 + 2 + (int) i, scr::Text, lines[i]);
    scr::status(kExitKeys);
    scr::showCursor(-1, 0);
    scr::present();
    for (;;)
    {
        scr::Key k = scr::readKey();
        if (k.vk == VK_F3)
            return true;
        if (k.vk == VK_RETURN)
            return false;
    }
}

void drawPage(const std::string &title, const std::string &body, const std::string &keys)
{
    scr::clear();
    scr::header(title);
    scr::para(0, 4, scr::W - 2, scr::Text, body);
    scr::status(keys);
    scr::showCursor(-1, 0);
}

void drawBar(int x1, int x2, int y, int percent, uint16_t fillAttr)
{
    int width = x2 - x1 - 1;
    int filled = width * std::max(0, std::min(100, percent)) / 100;
    scr::box(x1, y - 1, x2, y + 1, scr::Text);
    scr::fill(x1 + 1, y, filled, fillAttr);
    scr::fill(x1 + 1 + filled, y, width - filled, scr::Text);
}

} // namespace

// ---------------------------------------------------------------------------

std::string seconds(int n)
{
    int m10 = n % 10, m100 = n % 100;
    const char *w = (m10 == 1 && m100 != 11) ? "секунду"
                  : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? "секунды"
                                                                         : "секунд";
    return std::to_string(n) + " " + w;
}

void checkingHardware()
{
    scr::clear(scr::Black);
    scr::put(0, 1, scr::Black, kCheckingHardware);
    scr::showCursor(-1, 0);
    scr::present();
    Sleep(1500);
}

void loading(const std::string &title, const std::string &file)
{
    scr::clear();
    scr::header(title);
    scr::status(subst(kLoadingStatus, "file", file));
    scr::showCursor(-1, 0);
    scr::present();
}

bool welcome(const std::string &name)
{
    scr::flushKeys();
    for (;;)
    {
        drawPage("Установка " + name, named(kWelcome, name), kWelcomeKeys);
        scr::present();
        scr::Key k = scr::readKey();
        if (k.vk == VK_RETURN)
            return true;
        if (k.vk == VK_F3 && confirmExit(name))
            return false;
    }
}

bool license(const std::string &name, const std::string &text)
{
    const int top = 4, rows = scr::H - 1 - top - 1;
    std::vector<std::string> lines = wrapLines(text, scr::W - 4);
    int maxFirst = std::max(0, (int) lines.size() - rows);
    int first = 0;
    for (;;)
    {
        scr::clear();
        scr::header(named(kLicenseTitle, name));
        for (int i = 0; i < rows && first + i < (int) lines.size(); ++i)
            scr::put(2, top + i, scr::Text, lines[first + i]);
        scr::status(kLicenseKeys);
        scr::showCursor(-1, 0);
        scr::present();

        scr::Key k = scr::readKey();
        switch (k.vk)
        {
            case VK_F8:
                return true;
            case VK_ESCAPE:
            case VK_F3:
                if (confirmExit(name))
                    return false;
                break;
            case VK_NEXT:   first = std::min(maxFirst, first + rows); break;
            case VK_PRIOR:  first = std::max(0, first - rows); break;
            case VK_DOWN:   first = std::min(maxFirst, first + 1); break;
            case VK_UP:     first = std::max(0, first - 1); break;
            case VK_HOME:   first = 0; break;
            case VK_END:    first = maxFirst; break;
        }
    }
}

bool chooseDirectory(const SetupProject &p, std::string &dir, uint64_t need)
{
    const std::string original = dir;
    std::wstring edit = fsu::widen(dir);
    size_t caret = edit.size();
    const int fx1 = 6, fx2 = 73, fy = 7;
    const int fieldW = fx2 - fx1 - 1;
    std::string message;
    scr::flushKeys();

    for (;;)
    {
        std::string cur = fsu::narrow(edit);
        scr::clear();
        scr::header(p.title());
        scr::para(0, 4, scr::W - 2, scr::Text, named(kDirText, p.name));

        // Поле ввода: при длинном пути показывается часть с кареткой.
        scr::box(fx1, fy - 1, fx2, fy + 1, scr::Text);
        size_t start = caret > (size_t) fieldW - 1 ? caret - fieldW + 1 : 0;
        std::wstring visible = edit.substr(start, fieldW);
        scr::fill(fx1 + 1, fy, fieldW, scr::Input);
        scr::put(fx1 + 1, fy, scr::Input, fsu::narrow(visible));

        std::string probe = fsu::existingDir(cur);
        uint64_t free = fsu::isAbsolutePath(cur) ? fsu::freeSpace(probe) : 0;
        std::string drive = cur.size() >= 2 && cur[1] == ':' ? fsu::upper(cur.substr(0, 2)) : "";
        scr::put(6, fy + 3, scr::Text, "Требуется на диске:");
        scr::put(36, fy + 3, scr::Text, formatSize(need));
        scr::put(6, fy + 4, scr::Text, drive.empty() ? "Свободно на диске:" : "Свободно на диске " + drive);
        scr::put(36, fy + 4, scr::Text, free ? formatSize(free) : "—");

        int y = fy + 7;
        if (!message.empty())
            y += scr::para(3, y, scr::W - 6, scr::Bright, message) + 1;
        scr::para(0, y, scr::W - 2, scr::Text,
                  named(p.allowChangeDir ? kDirOptions : kDirOptionsFixed, p.name));
        scr::status(p.allowChangeDir ? kDirKeys : kDirKeysFixed);
        if (p.allowChangeDir)
            scr::showCursor(fx1 + 1 + (int) (caret - start), fy);
        else
            scr::showCursor(-1, 0);
        scr::present();

        scr::Key k = scr::readKey();
        if (k.vk == VK_F3)
        {
            if (confirmExit(p.name))
                return false;
            continue;
        }
        if (k.vk == VK_RETURN)
        {
            std::string path = cur;
            while (path.size() > 3 && (path.back() == '\\' || path.back() == '/'))
                path.pop_back();
            if (!fsu::isAbsolutePath(path) || path.size() < 3)
                message = "Необходимо указать полный путь к директории, например C:\\Program Files\\" + p.name + ".";
            else if (fsu::fileExists(path))
                message = "По указанному пути находится файл. Необходимо указать директорию.";
            else if (free && free < need)
                message = "На диске недостаточно свободного места для установки " + p.name + ".";
            else
            {
                dir = fsu::fullPath(path);
                return true;
            }
            continue;
        }
        if (!p.allowChangeDir)
            continue;
        message.clear();
        switch (k.vk)
        {
            case VK_ESCAPE: edit = fsu::widen(original); caret = edit.size(); break;
            case VK_LEFT:   if (caret) --caret; break;
            case VK_RIGHT:  if (caret < edit.size()) ++caret; break;
            case VK_HOME:   caret = 0; break;
            case VK_END:    caret = edit.size(); break;
            case VK_BACK:   if (caret) edit.erase(--caret, 1); break;
            case VK_DELETE: if (caret < edit.size()) edit.erase(caret, 1); break;
            default:
                if (k.ch >= 32 && k.ch != 127 && edit.size() < 240 &&
                    !wcschr(L"*?\"<>|", k.ch))
                    edit.insert(caret++, 1, k.ch);
        }
    }
}

bool existing(const SetupProject &p, const ExistingInstall &e)
{
    int cmp = compareVersions(p.version, e.version);
    const char *text = cmp > 0 ? kExistingUpdate : cmp == 0 ? kExistingSame : kExistingNewer;
    std::string body = named(text, p.name);
    body = subst(body, "old", e.version.empty() ? "?" : e.version);
    body = subst(body, "new", p.version);
    body = subst(body, "dir", e.dir);
    scr::flushKeys();
    for (;;)
    {
        drawPage(p.title(), body, cmp < 0 ? "F3=Выход" : kExistingKeys);
        scr::present();
        scr::Key k = scr::readKey();
        if (k.vk == VK_RETURN && cmp >= 0)
            return true;
        if (k.vk == VK_F3 && (cmp < 0 || confirmExit(p.name)))
            return false;
    }
}

// ---------------------------------------------------------------------------

ProgressScreen::ProgressScreen(const std::string &title, const std::string &message,
                               const std::string &label, bool askRetryOnError) :
    title(title), message(message), label(label), retry(askRetryOnError)
{
    draw();
}

void ProgressScreen::draw()
{
    scr::clear();
    scr::header(title);
    std::vector<std::string> lines = wrapLines(message, 50);
    for (size_t i = 0; i < lines.size(); ++i)
        scr::center(5 + (int) i, scr::Text, lines[i]);

    const int bx1 = 7, bx2 = 72, by1 = 11, by2 = 17;
    scr::box(bx1, by1, bx2, by2, scr::Text);
    scr::put(bx1 + 2, by1 + 1, scr::Text, label);
    scr::center(by1 + 2, scr::Text, std::to_string(percent) + "%");
    drawBar(13, 66, by1 + 4, percent, scr::Yellow);

    scr::status("", statusText);
    scr::showCursor(-1, 0);
    scr::present();
    lastDraw = GetTickCount64();
}

void ProgressScreen::onStatus(const std::string &text)
{
    statusText = text;
    // Частая перерисовка при множестве мелких файлов замедляет копирование.
    if (GetTickCount64() - lastDraw >= 30)
        draw();
}

void ProgressScreen::onPercent(int p)
{
    p = std::max(0, std::min(100, p));
    if (p == percent)
        return;
    percent = p;
    draw();
}

bool ProgressScreen::askRetry(const std::string &err)
{
    if (!retry)
        return false;
    std::string body = subst(kRetryText, "err", "      " + replaceAll(err, "\n", "\n      "));
    scr::flushKeys();
    for (;;)
    {
        drawPage(title, body, kRetryKeys);
        scr::present();
        scr::Key k = scr::readKey();
        if (k.vk == VK_RETURN)
        {
            draw();
            return true;
        }
        if (k.vk == VK_F3)
            return false;
    }
}

// ---------------------------------------------------------------------------

void finish(const std::string &title, const std::string &text, const std::string &countdown,
            const std::string &key)
{
    const int total = 15;
    const int bx1 = 7, bx2 = 72, by1 = 13, by2 = 18;
    scr::flushKeys();
    ULONGLONG start = GetTickCount64();
    for (;;)
    {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= (ULONGLONG) total * 1000)
            return;
        int left = total - (int) (elapsed / 1000);
        scr::clear();
        scr::header(title);
        scr::para(0, 4, scr::W - 2, scr::Text, text);
        scr::box(bx1, by1, bx2, by2, scr::Text);
        scr::put(bx1 + 2, by1 + 1, scr::Text, subst(countdown, "time", seconds(left)));
        drawBar(13, 66, by1 + 3, (int) (elapsed * 100 / (total * 1000)), scr::Red);
        scr::status(key);
        scr::showCursor(-1, 0);
        scr::present();
        scr::Key k;
        if (scr::waitKey(k, 100) && k.vk == VK_RETURN)
            return;
    }
}

void error(const std::string &title, const std::string &text)
{
    std::string body;
    for (const std::string &l : wrapLines(text, scr::W - 8))
        body += "   " + l + "\n";
    body += "\n      \xE2\x80\xA2  Чтобы выйти из программы, нажмите <F3>.";
    scr::flushKeys();
    for (;;)
    {
        drawPage(title, body, "F3=Выход");
        scr::present();
        scr::Key k = scr::readKey();
        if (k.vk == VK_F3 || k.vk == VK_RETURN || k.vk == VK_ESCAPE)
            return;
    }
}

bool confirmUninstall(const InstallLog &log)
{
    std::string body = named(kUninstallText, log.name);
    body = subst(body, "version", log.version);
    body = subst(body, "dir", log.installDir);
    scr::flushKeys();
    for (;;)
    {
        drawPage("Удаление " + log.name, body, kUninstallKeys);
        scr::present();
        scr::Key k = scr::readKey();
        if (k.vk == 'L')
            return true;
        if (k.vk == VK_ESCAPE || k.vk == VK_F3)
            return false;
    }
}

} // namespace ui
