#include "fontspec.h"

#include <QFontDatabase>
#include <QStringList>

namespace FontSpec {

// Pango style words that may follow the family name.
static bool applyStyleWord(QFont &f, const QString &w)
{
    const QString l = w.toLower();
    if (l == "bold") { f.setWeight(QFont::Bold); return true; }
    if (l == "semi-bold" || l == "semibold" || l == "demibold") { f.setWeight(QFont::DemiBold); return true; }
    if (l == "light") { f.setWeight(QFont::Light); return true; }
    if (l == "ultra-light" || l == "extralight") { f.setWeight(QFont::ExtraLight); return true; }
    if (l == "medium") { f.setWeight(QFont::Medium); return true; }
    if (l == "heavy" || l == "black") { f.setWeight(QFont::Black); return true; }
    if (l == "italic") { f.setStyle(QFont::StyleItalic); return true; }
    if (l == "oblique") { f.setStyle(QFont::StyleOblique); return true; }
    if (l == "normal" || l == "regular" || l == "book" || l == "roman") return true;
    if (l == "condensed") { f.setStretch(QFont::Condensed); return true; }
    return false;
}

static QFont fixedFallback(double size)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (size > 0)
        f.setPointSizeF(size);
    f.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
    return f;
}

Parsed fromPutty(const QString &puttyNameIn)
{
    Parsed p;
    QString name = puttyNameIn.trimmed();
    if (name.isEmpty()) {
        p.empty = true;
        p.font = fixedFallback(12);
        return p;
    }
    if (name.startsWith("server:") || name.startsWith('-')) {
        p.serverFont = true;
        p.font = fixedFallback(12);
        return p;
    }
    if (name.startsWith("client:"))
        name = name.mid(7);

    // Pango description: "[FAMILY-LIST] [STYLE-OPTIONS] [SIZE]"
    QStringList words = name.split(' ', Qt::SkipEmptyParts);
    double size = 12;
    bool ok = false;
    if (!words.isEmpty()) {
        QString last = words.last();
        bool px = last.endsWith("px");
        if (px)
            last.chop(2);
        double v = last.toDouble(&ok);
        if (ok) {
            size = px ? v * 0.75 : v;
            words.removeLast();
        }
    }
    QFont f;
    while (!words.isEmpty() && applyStyleWord(f, words.last()))
        words.removeLast();
    QString family = words.join(' ');
    if (family.endsWith(','))
        family.chop(1);
    if (family.isEmpty() || family.compare("Monospace", Qt::CaseInsensitive) == 0) {
        QFont fixed = fixedFallback(size);
        fixed.setWeight(f.weight());
        fixed.setStyle(f.style());
        p.font = fixed;
    } else {
        f.setFamilies(family.split(',', Qt::SkipEmptyParts));
        f.setPointSizeF(size);
        f.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
        p.font = f;
    }
    p.font.setKerning(false);
    return p;
}

QString toPutty(const QFont &font)
{
    QStringList words;
    words << font.family();
    if (font.weight() >= QFont::Bold)
        words << "Bold";
    else if (font.weight() >= QFont::DemiBold)
        words << "Semi-Bold";
    if (font.style() == QFont::StyleItalic)
        words << "Italic";
    else if (font.style() == QFont::StyleOblique)
        words << "Oblique";
    double size = font.pointSizeF() > 0 ? font.pointSizeF() : font.pixelSize() * 0.75;
    words << QString::number(size, 'g', 4);
    return "client:" + words.join(' ');
}

QString resized(const QString &puttyName, int delta)
{
    Parsed p = fromPutty(puttyName);
    if (p.empty || p.serverFont)
        return puttyName;
    double size = p.font.pointSizeF() + delta;
    if (size < 4)
        size = 4;
    QString name = puttyName.trimmed();
    if (name.startsWith("client:"))
        name = name.mid(7);
    // Replace just the size, keeping the rest of the description verbatim.
    QStringList words = name.split(' ', Qt::SkipEmptyParts);
    bool ok = false;
    if (!words.isEmpty())
        words.last().toDouble(&ok);
    if (ok)
        words.removeLast();
    words << QString::number(size, 'g', 4);
    return "client:" + words.join(' ');
}

} // namespace FontSpec
