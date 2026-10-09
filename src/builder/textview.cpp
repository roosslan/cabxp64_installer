#define Uses_TRect
#define Uses_TView
#define Uses_TGroup
#define Uses_TDialog
#define Uses_TButton
#define Uses_TScrollBar
#define Uses_TScroller
#define Uses_TDrawBuffer
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include <algorithm>

#include "textview.h"

TTextScroller::TTextScroller(const TRect &bounds, TScrollBar *h, TScrollBar *v) :
    TScroller(bounds, h, v)
{
    growMode = gfGrowHiX | gfGrowHiY;
}

void TTextScroller::setText(const std::string &text)
{
    lines.clear();
    size_t pos = 0;
    int width = 0;
    while (pos <= text.size())
    {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
        width = std::max(width, strwidth(line));
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    setLimit(width + 1, (int) lines.size());
    drawView();
}

void TTextScroller::draw()
{
    TColorAttr color = getColor(1);
    for (int y = 0; y < size.y; ++y)
    {
        TDrawBuffer b;
        b.moveChar(0, ' ', color, size.x);
        int i = delta.y + y;
        if (i < (int) lines.size())
            b.moveStr(0, lines[i], color, size.x, delta.x);
        writeLine(0, y, size.x, 1, b);
    }
}

void scrollTextDialog(const char *title, const std::string &text)
{
    TRect screen = TProgram::deskTop->getExtent();
    int w = std::min(76, (int) screen.b.x - 2), h = std::min(22, (int) screen.b.y);
    TDialog *d = new TDialog(TRect(0, 0, w, h), title);
    d->options |= ofCentered;
    TScrollBar *v = new TScrollBar(TRect(w - 3, 2, w - 2, h - 4));
    TScrollBar *hs = new TScrollBar(TRect(3, h - 4, w - 3, h - 3));
    d->insert(v);
    d->insert(hs);
    TTextScroller *t = new TTextScroller(TRect(3, 2, w - 3, h - 4), hs, v);
    d->insert(t);
    t->setText(text);
    d->insert(new TButton(TRect(w / 2 - 6, h - 3, w / 2 + 6, h - 1), "O~K~", cmOK, bfDefault));
    t->select();
    TProgram::deskTop->execView(d);
    TObject::destroy(d);
}
