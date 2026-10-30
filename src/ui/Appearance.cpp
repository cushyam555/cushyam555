#include "Appearance.h"
#include <QApplication>
#include <QLabel>
#include <QPalette>
#include <algorithm>
namespace bt {
void applyDarkPalette(QApplication &app) {
    app.setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window, QColor("#0c101a"));
    p.setColor(QPalette::WindowText, QColor("#e8ecf6"));
    p.setColor(QPalette::Base, QColor("#101725"));
    p.setColor(QPalette::AlternateBase, QColor("#1a2335"));
    p.setColor(QPalette::Text, QColor("#e8ecf6"));
    p.setColor(QPalette::Button, QColor("#202a3d"));
    p.setColor(QPalette::ButtonText, QColor("#e8ecf6"));
    p.setColor(QPalette::ToolTipBase, QColor("#26314b"));
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Highlight, QColor("#51499c"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, QColor("#465576"));
    p.setColor(QPalette::Mid, QColor("#34425e"));
    p.setColor(QPalette::Dark, QColor("#080d16"));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#7f8da7"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7f8da7"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#7f8da7"));
    app.setPalette(p);
}
QJsonObject LyricsStyle::save() const {
    return {{"font", font.toString()}, {"background", background.name()}, {"text", text.name()},
            {"sung", sung.name()},     {"alignment", alignment},          {"margin", margin},
            {"offset", offset}};
}
void LyricsStyle::restore(const QJsonObject &o) {
    QFont candidate;
    if (!o["font"].toString().isEmpty() && candidate.fromString(o["font"].toString()) &&
        candidate.pointSize() >= 12 && candidate.pointSize() <= 160)
        font = candidate;
    auto color = [&](const char *key, QColor &target) {
        QColor c(o[key].toString());
        if (c.isValid())
            target = c;
    };
    color("background", background);
    color("text", text);
    color("sung", sung);
    alignment = std::clamp(o["alignment"].toInt(1), 0, 2);
    margin = std::clamp(o["margin"].toInt(40), 0, 120);
    offset = std::clamp(o["offset"].toDouble(), -5., 5.);
}
void LyricsStyle::apply(QLabel *label, QWidget *surface) const {
    surface->setStyleSheet("background:" + background.name() + ";color:" + text.name() + ";");
    auto family = font.family();
    family.replace("'", "");
    label->setStyleSheet(QString("background:transparent;color:%1;font-family:'%2';font-size:%3pt;"
                                 "font-weight:%4;padding:%5px;")
                             .arg(text.name(), family)
                             .arg(font.pointSize())
                             .arg(font.bold() ? 700 : 400)
                             .arg(margin));
    label->setAlignment(Qt::AlignHCenter | (alignment == 0   ? Qt::AlignTop
                                            : alignment == 2 ? Qt::AlignBottom
                                                             : Qt::AlignVCenter));
}
} // namespace bt
