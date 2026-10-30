#include "LyricCanvas.h"
#include <QPainter>
#include <QRegion>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
namespace bt {
LyricCanvas::LyricCanvas(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(150, 120);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this] { tick(); });
    setAppearance(appearance);
}
void LyricCanvas::setAppearance(const LyricsStyle &style) {
    appearance = style;
    dirty = true;
    timer.start(std::max(1, int(std::lround(1000. / appearance.lyricFps()))));
    update();
}
void LyricCanvas::setClock(std::function<double()> p, std::function<bool()> r,
                           std::function<double()> s) {
    clock = std::move(p);
    running = std::move(r);
    speed = std::move(s);
    lastPosition = -1;
}
void LyricCanvas::tick() {
    if (!isVisible() || !clock)
        return;
    double p = clock();
    bool play = running && running();
    bool redraw = dirty || p != position || play || play != wasRunning;
    if (p != lastPosition || play != wasRunning || !play || !anchorClock.isValid()) {
        lastPosition = p;
        anchorClock.restart();
    }
    wasRunning = play;
    position = p;
    if (play)
        position += std::min(.05, anchorClock.nsecsElapsed() / 1e9) * (speed ? speed() : 1.);
    if (redraw)
        update();
}
void LyricCanvas::setPosition(double seconds) {
    position = seconds;
    update();
}
void LyricCanvas::setSong(const QVector<Lyric> &events, double duration) {
    rows.clear();
    rows.push_back({{}, {}});
    for (int i = 0; i < events.size(); ++i) {
        QString text = events[i].text;
        text.replace("\r\n", "\n");
        text.replace('\r', '\n');
        text.replace('\\', '\n');
        text.replace('/', '\n');
        double start = events[i].time;
        double end = i + 1 < events.size()
                         ? events[i + 1].time
                         : std::min(duration, start + std::max(.25, text.size() * .1));
        end = std::max(end, start);
        int glyphs = 0, cursor = 0;
        for (auto ch : text)
            if (ch != '\n')
                ++glyphs;
        for (int c = 0; c < text.size(); ++c) {
            double t = start + (end - start) * cursor / std::max(1, glyphs);
            auto &row = rows.last();
            if (text[c] == '\n') {
                if (!row.text.isEmpty()) {
                    row.boundaries.push_back(t);
                    rows.push_back({{}, {}});
                }
            } else {
                row.text += text[c];
                row.boundaries.push_back(t);
                ++cursor;
            }
        }
        if (i == events.size() - 1 && !rows.last().text.isEmpty())
            rows.last().boundaries.push_back(end);
    }
    if (rows.last().text.isEmpty())
        rows.removeLast();
    for (auto &row : rows)
        if (row.boundaries.size() == row.text.size())
            row.boundaries.push_back(duration);
    position = 0;
    lastPosition = -1;
    dirty = true;
    update();
}
double LyricCanvas::progress(int row, double seconds) const {
    if (row < 0 || row >= rows.size())
        return 0;
    const auto &times = rows[row].boundaries;
    if (times.isEmpty() || seconds < times.first())
        return 0;
    auto next = std::upper_bound(times.begin(), times.end(), seconds);
    if (next == times.end())
        return rows[row].text.size();
    int index = int(next - times.begin()) - 1;
    double span = times[index + 1] - times[index];
    return index + (span > 0 ? std::clamp((seconds - times[index]) / span, 0., 1.) : 1.);
}
int LyricCanvas::currentPage() const {
    int line = 0;
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i].boundaries.first() > position + appearance.offset)
            break;
        line = i;
    }
    return (line / 2) * 2;
}
void LyricCanvas::prepare(int page) {
    qreal width = std::max(1, this->width() - appearance.margin * 2);
    QFont font = appearance.font;
    // Cache shaping/wrapping for the current two-line page; shrink only if it cannot fit.
    for (;;) {
        qreal total = 0;
        for (int r = 0; r < 2; ++r) {
            layouts[r].reset();
            heights[r] = 0;
            if (page + r >= rows.size())
                continue;
            layouts[r] = std::make_unique<QTextLayout>(rows[page + r].text, font);
            layouts[r]->setCacheEnabled(true);
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            option.setAlignment(Qt::AlignHCenter);
            layouts[r]->setTextOption(option);
            layouts[r]->beginLayout();
            while (true) {
                auto line = layouts[r]->createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(width);
                line.setPosition(QPointF(0, heights[r]));
                heights[r] += line.height();
            }
            layouts[r]->endLayout();
            total += heights[r];
        }
        if (layouts[1])
            total += appearance.lineGap;
        if (total <= height() - 2 * appearance.margin || font.pointSize() <= 12)
            break;
        font.setPointSize(font.pointSize() - 1);
    }
    cachedPage = page;
    dirty = false;
}
void LyricCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), appearance.background);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (rows.isEmpty()) {
        p.setFont(appearance.font);
        p.setPen(appearance.text);
        p.drawText(rect(), Qt::AlignCenter, "BT Karaoke");
        return;
    }
    int page = currentPage();
    if (dirty || page != cachedPage)
        prepare(page);
    qreal total = heights[0] + heights[1] + (layouts[1] ? appearance.lineGap : 0);
    qreal y = appearance.alignment == 0   ? appearance.margin
              : appearance.alignment == 2 ? height() - appearance.margin - total
                                          : (height() - total) / 2;
    for (int r = 0; r < 2; ++r)
        if (layouts[r]) {
            QPointF origin(appearance.margin, y);
            p.setPen(appearance.text);
            layouts[r]->draw(&p, origin);
            double amount = progress(page + r, position + appearance.offset);
            QRegion clip;
            for (int v = 0; v < layouts[r]->lineCount(); ++v) {
                auto line = layouts[r]->lineAt(v);
                int first = line.textStart(), end = first + line.textLength();
                if (amount <= first)
                    continue;
                int whole = std::min(end, int(amount));
                qreal x = line.cursorToX(whole);
                if (whole < end)
                    x += (line.cursorToX(whole + 1) - x) * (amount - whole);
                qreal begin = line.cursorToX(first);
                clip += QRectF(origin.x() + std::min(begin, x), y + line.y(), std::abs(x - begin),
                               line.height())
                            .toAlignedRect();
            }
            if (!clip.isEmpty()) {
                p.save();
                p.setClipRegion(clip);
                p.setPen(appearance.sung);
                layouts[r]->draw(&p, origin);
                p.restore();
            }
            y += heights[r] + appearance.lineGap;
        }
}
void LyricCanvas::resizeEvent(QResizeEvent *event) {
    dirty = true;
    QWidget::resizeEvent(event);
}
} // namespace bt
