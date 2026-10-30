#include "Text.h"

#include "Database.h"

#include <cmath>

#include <QHash>
#include <QList>
#include <QLocale>
#include <QRegularExpression>
#include <QTextBoundaryFinder>

namespace sc {

QString normalizedTitle(const QString &text) {
    const auto decomposed = text.toLower().normalized(QString::NormalizationForm_D).toUcs4();
    QList<char32_t> out;
    out.reserve(decomposed.size());
    bool gap = false;
    for (const char32_t c : decomposed) {
        // Accents on Latin, Greek and Cyrillic letters. Other scripts' marks
        // (Japanese voicing, Indic vowel signs) change the word and stay.
        if (c >= 0x0300 && c <= 0x036F)
            continue;
        if (QChar::isLetterOrNumber(c) || QChar::isMark(c)) {
            if (gap && !out.isEmpty())
                out.append(U' ');
            gap = false;
            out.append(c);
        } else {
            gap = true;
        }
    }
    return QString::fromUcs4(out.constData(), out.size()).normalized(QString::NormalizationForm_C);
}

QString languageName(const QString &code) {
    static const QHash<QString, QString> names{
        {"en", "English"},  {"es", "Español"},   {"fr", "Français"}, {"pt", "Português"}, {"id", "Bahasa Indonesia"},
        {"it", "Italiano"}, {"de", "Deutsch"},   {"ru", "Русский"},  {"tr", "Türkçe"},    {"vi", "Tiếng Việt"},
        {"ar", "العربية"},  {"ja", "日本語"},    {"ko", "한국어"},   {"zh", "中文"},      {"th", "ไทย"},
        {"pl", "Polski"},   {"multi", "Multi"}};
    if (code.isEmpty())
        return names.value(QStringLiteral("en"));
    return names.value(code.section(QLatin1Char('-'), 0, 0).toLower(), code);
}

QString languageCode(const QString &name) {
    static const QHash<QString, QString> codes{
        {"english", "en"},    {"español", "es"},   {"spanish", "es"},    {"français", "fr"}, {"french", "fr"},
        {"português", "pt"},  {"portuguese", "pt"}, {"bahasa indonesia", "id"}, {"indonesian", "id"},
        {"italiano", "it"},   {"italian", "it"},   {"deutsch", "de"},    {"german", "de"},   {"русский", "ru"},
        {"russian", "ru"},    {"türkçe", "tr"},    {"turkish", "tr"},    {"tiếng việt", "vi"}, {"vietnamese", "vi"},
        {"العربية", "ar"},    {"arabic", "ar"},    {"日本語", "ja"},     {"japanese", "ja"}, {"한국어", "ko"},
        {"korean", "ko"},     {"中文", "zh"},       {"chinese", "zh"},    {"ไทย", "th"},      {"thai", "th"},
        {"polski", "pl"},     {"polish", "pl"},    {"українська", "uk"}, {"ukrainian", "uk"}, {"multi", "multi"}};
    // Marks some lists put around names (left-to-right marks, "(简体)").
    auto key = name;
    key.remove(QChar(0x200e)).remove(QChar(0x200f));
    key = key.section(QLatin1Char('('), 0, 0).trimmed().toLower();
    if (const auto code = codes.value(key); !code.isEmpty())
        return code;
    static const QRegularExpression code(QStringLiteral("^[a-z]{2,3}([-_][A-Za-z0-9]+)?$"));
    return code.match(key).hasMatch() ? key : QString();
}

QString baseLanguage(const QString &code) {
    return code.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).trimmed().toLower();
}

QString contentLanguage(const QString &sourceLanguages, const QString &interfaceLanguage, const QString &systemLocale) {
    for (const auto &candidate : {sourceLanguages.section(QLatin1Char(','), 0, 0),
                                  interfaceLanguage == QLatin1String("system") ? systemLocale : interfaceLanguage}) {
        const auto base = baseLanguage(candidate);
        // The C locale ("C", "c") stands for no language.
        if (base.size() >= 2 && base != QLatin1String("system"))
            return base;
    }
    return QStringLiteral("en");
}

QString contentLanguage() {
    return contentLanguage(Database::pref(QStringLiteral("setting.sourceLanguages")).toString(),
                           Database::pref(QStringLiteral("language"), QStringLiteral("system")).toString(),
                           QLocale::system().name());
}

QStringList builtinLanguages() {
    // No settings (tools such as the probe): every language.
    if (Database::path().isEmpty())
        return {QStringLiteral("*")};
    const auto stored = Database::pref(QStringLiteral("setting.builtinLanguages"));
    QStringList out;
    for (const auto &code : stored.toStringList())
        if (const auto base = baseLanguage(code); !base.isEmpty() && !out.contains(base))
            out.append(base);
    if (!out.isEmpty())
        return out;
    // By default only the content language's (which follows the interface
    // language).
    return {contentLanguage()};
}

QString titleKey(const QString &text) { return normalizedTitle(text).remove(QLatin1Char(' ')); }

QString chapterPart(const QString &title) {
    static const QRegularExpression cjkExtra(QString::fromUtf8("番外|特别篇|特別篇|特別編|特别编|外传|外傳|おまけ"));
    static const QRegularExpression latinExtra(QStringLiteral(R"(\b(extras?|specials?|omake|bonus)\b|\bside[ -]?stor)"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cjkVolume(QString::fromUtf8("[卷巻册冊권]"));
    static const QRegularExpression cjkChapter(QString::fromUtf8("[话話章回화]"));
    static const QRegularExpression latinVolume(QStringLiteral(R"(\b(vol(ume)?|tome|tomo|band)\.?\s*\d)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression latinChapter(QStringLiteral(R"(\b(ch(apter)?|chap|ep(isode)?|cap[ií]tulo|cap|chapitre|kapitel)\.?\s*\d)"),
                                                 QRegularExpression::CaseInsensitiveOption);
    const bool numbered = latinChapter.match(title).hasMatch();
    // "Chapter 45: Special Training" is a chapter; "Extra 2" and "番外 1" are not.
    if (cjkExtra.match(title).hasMatch() || (!numbered && latinExtra.match(title).hasMatch()))
        return QStringLiteral("extra");
    const bool cjk = cjkVolume.match(title).hasMatch() && !cjkChapter.match(title).hasMatch();
    const bool latin = latinVolume.match(title).hasMatch() && !numbered;
    return cjk || latin ? QStringLiteral("volume") : QStringLiteral("chapter");
}

namespace {

QString canonicalNumber(QString text) {
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || value < 0 || value > 100000 || !std::isfinite(value))
        return {};
    // 12.50 -> 12.5, 012 -> 12, 1e3 never (plain digits only reach here).
    return QString::number(value, 'f', 6).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
}

} // namespace

QString chapterNumber(const QString &number, const QString &title) {
    static const QRegularExpression plain(QStringLiteral(R"(^\s*0*(\d+(?:[.,]\d+)?)\s*$)"));
    if (const auto m = plain.match(number); m.hasMatch())
        return canonicalNumber(m.captured(1));
    // A number field with words in it ("Vol.3 Ch.12") is read as a title.
    for (const auto &text : {number, title}) {
        if (text.trimmed().isEmpty())
            continue;
        static const QRegularExpression latin(
            QStringLiteral(R"((?:\b(?:ch(?:apter|ap)?|ep(?:isode)?|eps|cap(?:[ií]tulo)?|chapitre|kapitel|chuong|ch[uư]ơng|bab|episodio)\.?|#)\s*0*(\d+(?:[.,]\d+)?))"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        static const QRegularExpression cjk(QString::fromUtf8(R"((?:第|제)?\s*0*(\d+(?:\.\d+)?)\s*[话話章回화회])"));
        if (const auto m = latin.match(text); m.hasMatch())
            return canonicalNumber(m.captured(1));
        if (const auto m = cjk.match(text); m.hasMatch())
            return canonicalNumber(m.captured(1));
        // Volumes and extras are not numbered chapters.
        if (chapterPart(text) != QLatin1String("chapter"))
            return {};
        static const QRegularExpression prologue(QStringLiteral(R"(\b(prologue|prólogo|prologo|序章|프롤로그)\b)"),
                                                 QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        if (prologue.match(text).hasMatch())
            return QStringLiteral("0");
        // "12 - Title", "012": a leading number, a volume's taken out first.
        static const QRegularExpression volume(QStringLiteral(R"(\b(?:vol(?:ume)?|tome|tomo|band|s(?:eason)?)\.?\s*\d+)"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression leading(QStringLiteral(R"(^\D{0,3}0*(\d+(?:[.,]\d+)?))"));
        if (const auto m = leading.match(QString(text).remove(volume).trimmed()); m.hasMatch())
            return canonicalNumber(m.captured(1));
    }
    return {};
}

QList<QPair<int, int>> sentenceSpans(const QString &text) {
    QList<QPair<int, int>> out;
    const auto add = [&](int from, int to) {
        while (from < to && text.at(from).isSpace())
            ++from;
        while (to > from && text.at(to - 1).isSpace())
            --to;
        for (int i = from; i < to; ++i)
            if (text.at(i).isLetterOrNumber()) {
                out.append({from, to - from});
                return;
            }
    };
    QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);
    int start = 0;
    while (finder.toNextBoundary() >= 0) {
        const int end = int(finder.position());
        if (end <= start)
            continue;
        add(start, end);
        start = end;
    }
    if (start < text.size())
        add(start, int(text.size()));
    return out;
}

} // namespace sc
