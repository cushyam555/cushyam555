#pragma once
#include "Appearance.h"
#include "core/Midi.h"
#include <QElapsedTimer>
#include <QTextLayout>
#include <QTimer>
#include <QWidget>
#include <array>
#include <functional>
#include <memory>
namespace bt {
class LyricCanvas : public QWidget {
    struct Row {
        QString text;
        QVector<double> boundaries;
    };
    QVector<Row> rows;
    LyricsStyle appearance;
    QTimer timer;
    QElapsedTimer anchorClock;
    std::function<double()> clock;
    std::function<bool()> running;
    std::function<double()> speed;
    double lastPosition = -1, position = 0;
    int cachedPage = -1;
    bool dirty = true, wasRunning = false;
    std::array<std::unique_ptr<QTextLayout>, 2> layouts;
    std::array<qreal, 2> heights{};
    int currentPage() const;
    void prepare(int page);
    void tick();

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  public:
    explicit LyricCanvas(QWidget *parent = nullptr);
    void setSong(const QVector<Lyric> &, double duration);
    void setAppearance(const LyricsStyle &);
    void setClock(std::function<double()> position, std::function<bool()> playing,
                  std::function<double()> rate);
    void setPosition(double seconds);
    double progress(int row, double seconds) const;
    int lineCount() const {
        return rows.size();
    }
    int lineGap() const {
        return appearance.lineGap;
    }
    int frameInterval() const {
        return timer.interval();
    }
};
} // namespace bt
