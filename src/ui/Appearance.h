#pragma once
#include <QColor>
#include <QFont>
#include <QJsonObject>
class QApplication;
class QLabel;
class QWidget;
namespace bt {
void applyDarkPalette(QApplication &app);
struct LyricsStyle {
    QFont font = QFont("Tahoma", 48, QFont::Bold);
    QColor background{"#090d18"}, text{"#f4f5ff"}, sung{"#aa9bff"};
    int alignment = 1, margin = 40, lineGap = 40, performance = 1;
    int lyricFps() const {
        return performance == 0 ? 30 : performance == 2 ? 120 : 60;
    }
    int mixerInterval() const {
        return performance == 0 ? 100 : performance == 2 ? 25 : 40;
    }
    double offset = 0;
    QJsonObject save() const;
    void restore(const QJsonObject &);
    void apply(QLabel *label, QWidget *surface) const;
};
} // namespace bt
