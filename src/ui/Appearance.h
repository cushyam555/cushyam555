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
    int alignment = 1, margin = 40;
    double offset = 0;
    QJsonObject save() const;
    void restore(const QJsonObject &);
    void apply(QLabel *label, QWidget *surface) const;
};
} // namespace bt
