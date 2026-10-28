#include "icons.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include "fsutil.h"

namespace {

#pragma pack(push, 1)
struct IcoDirEntry
{
    BYTE width, height, colors, reserved;
    WORD planes, bpp;
    DWORD bytes, offset;
};

struct GrpDirEntry
{
    BYTE width, height, colors, reserved;
    WORD planes, bpp;
    DWORD bytes;
    WORD id;
};
#pragma pack(pop)

struct IcoImage
{
    IcoDirEntry entry;
    std::string data;
};

bool loadIco(const std::string &path, std::vector<IcoImage> &out, std::string &err)
{
    std::string f;
    if (!fsu::readFile(path, f, 16u << 20))
    {
        err = "Не удалось прочитать файл иконки " + path;
        return false;
    }
    if (f.size() < 6 || f[0] || f[1] || f[2] != 1 || f[3])
    {
        err = "Файл не является иконкой (.ico): " + path;
        return false;
    }
    unsigned count = (unsigned char) f[4] | ((unsigned char) f[5] << 8);
    if (count == 0 || 6 + 16 * (size_t) count > f.size())
    {
        err = "Файл иконки повреждён: " + path;
        return false;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        IcoImage img;
        memcpy(&img.entry, f.data() + 6 + 16 * i, sizeof(IcoDirEntry));
        if (img.entry.bytes == 0 || (size_t) img.entry.offset + img.entry.bytes > f.size())
        {
            err = "Файл иконки повреждён: " + path;
            return false;
        }
        img.data = f.substr(img.entry.offset, img.entry.bytes);
        out.push_back(img);
    }
    return true;
}

struct OldGroup
{
    WORD lang;
    std::vector<WORD> ids;
};

BOOL CALLBACK collectLang(HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param)
{
    reinterpret_cast<std::vector<WORD> *>(param)->push_back(lang);
    return TRUE;
}

BOOL CALLBACK collectId(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    if (IS_INTRESOURCE(name))
        reinterpret_cast<std::vector<WORD> *>(param)->push_back((WORD) (ULONG_PTR) name);
    return TRUE;
}

HMODULE openData(const std::string &exe)
{
    return LoadLibraryExW(fsu::widen(exe).c_str(), nullptr,
                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
}

// Записи группы на языке lang.
std::vector<GrpDirEntry> groupEntries(HMODULE h, int group, WORD lang)
{
    std::vector<GrpDirEntry> r;
    HRSRC res = FindResourceExW(h, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, MAKEINTRESOURCEW(group), lang);
    HGLOBAL g = res ? LoadResource(h, res) : nullptr;
    const BYTE *p = g ? (const BYTE *) LockResource(g) : nullptr;
    DWORD size = res ? SizeofResource(h, res) : 0;
    if (!p || size < 6)
        return r;
    unsigned count = p[4] | (p[5] << 8);
    for (unsigned i = 0; i < count && 6 + 14 * (i + 1) <= size; ++i)
    {
        GrpDirEntry e;
        memcpy(&e, p + 6 + 14 * i, sizeof(e));
        r.push_back(e);
    }
    return r;
}

} // namespace

bool checkIconFile(const std::string &ico, std::string &err)
{
    std::vector<IcoImage> images;
    return loadIco(ico, images, err);
}

std::vector<IconImage> iconGroup(const std::string &exe, int group)
{
    std::vector<IconImage> r;
    HMODULE h = openData(exe);
    if (!h)
        return r;
    std::vector<WORD> langs;
    EnumResourceLanguagesW(h, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(group), collectLang, (LONG_PTR) &langs);
    if (!langs.empty())
        for (const GrpDirEntry &e : groupEntries(h, group, langs[0]))
            r.push_back({e.width ? e.width : 256, e.height ? e.height : 256, e.bpp, e.bytes});
    FreeLibrary(h);
    return r;
}

bool setIconGroup(const std::string &exe, int group, const std::string &ico, std::string &err)
{
    std::vector<IcoImage> images;
    if (!ico.empty() && !loadIco(ico, images, err))
        return false;

    // Прежняя группа (на всех языках) и занятые номера изображений.
    std::vector<OldGroup> old;
    std::vector<WORD> usedIds;
    HMODULE h = openData(exe);
    if (!h)
    {
        err = "Не удалось открыть ресурсы " + exe + "\n" + fsu::lastErrorText();
        return false;
    }
    std::vector<WORD> langs;
    EnumResourceLanguagesW(h, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(group), collectLang, (LONG_PTR) &langs);
    for (WORD lang : langs)
    {
        OldGroup og{lang, {}};
        for (const GrpDirEntry &e : groupEntries(h, group, lang))
            og.ids.push_back(e.id);
        old.push_back(og);
    }
    EnumResourceNamesW(h, MAKEINTRESOURCEW(3) /* RT_ICON */, collectId, (LONG_PTR) &usedIds);
    FreeLibrary(h);

    HANDLE upd = BeginUpdateResourceW(fsu::widen(exe).c_str(), FALSE);
    if (!upd)
    {
        err = "Не удалось изменить ресурсы " + exe + "\n" + fsu::lastErrorText();
        return false;
    }
    bool ok = true;
    for (const OldGroup &og : old)
    {
        ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(group), og.lang, nullptr, 0);
        for (WORD id : og.ids)
            ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), og.lang, nullptr, 0);
    }
    if (!images.empty())
    {
        WORD lang = old.empty() ? MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL) : old[0].lang;
        WORD next = usedIds.empty() ? 1 : (WORD) (*std::max_element(usedIds.begin(), usedIds.end()) + 1);
        std::string grp(6, '\0');
        grp[2] = 1;
        grp[4] = (char) (images.size() & 0xFF);
        grp[5] = (char) (images.size() >> 8);
        for (const IcoImage &img : images)
        {
            WORD id = next++;
            ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), lang,
                                       (LPVOID) img.data.data(), (DWORD) img.data.size());
            GrpDirEntry e;
            memcpy(&e, &img.entry, 12);
            e.id = id;
            grp.append((const char *) &e, sizeof(e));
        }
        ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(group), lang,
                                   (LPVOID) grp.data(), (DWORD) grp.size());
    }
    if (!ok)
    {
        err = "Не удалось заменить иконку в " + exe + "\n" + fsu::lastErrorText();
        EndUpdateResourceW(upd, TRUE);
        return false;
    }
    if (!EndUpdateResourceW(upd, FALSE))
    {
        err = "Не удалось записать ресурсы " + exe + "\n" + fsu::lastErrorText();
        return false;
    }
    return true;
}
