#include "app/core/terminal_color_rules.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include <algorithm>
#include <limits>
#include <set>

namespace AppTerminal {
namespace {

QStringList normalizedPresetIds(const QStringList &ids)
{
    QStringList normalized;
    for (const QString &id : {QStringLiteral("log_levels"), QStringLiteral("status"), QStringLiteral("at_commands")}) {
        if (ids.contains(id)) {
            normalized.append(id);
        }
    }
    return normalized;
}

QRegularExpression expressionForRule(const ColorRule &rule)
{
    QRegularExpression expression(rule.regularExpression ? rule.pattern : QRegularExpression::escape(rule.pattern));
    if (!rule.caseSensitive) {
        expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }
    return expression;
}

void appendSpan(QVector<ColorSpan> &spans, int start, int length, const QColor &color)
{
    if (length <= 0) {
        return;
    }
    if (!spans.isEmpty() && spans.last().start + spans.last().length == start && spans.last().color == color) {
        spans.last().length += length;
    } else {
        spans.append({start, length, color});
    }
}

} // namespace

QJsonObject toJson(const ColorConfig &config)
{
    QJsonArray rules;
    for (int index = 0; index < config.rules.size() && index < MaximumColorRules; ++index) {
        const ColorRule &rule = config.rules.at(index);
        rules.append(
            QJsonObject{{QStringLiteral("pattern"), rule.pattern},
                        {QStringLiteral("color"), rule.color.isValid() ? rule.color.name(QColor::HexArgb) : QString()},
                        {QStringLiteral("enabled"), rule.enabled},
                        {QStringLiteral("regularExpression"), rule.regularExpression},
                        {QStringLiteral("caseSensitive"), rule.caseSensitive},
                        {QStringLiteral("wholeLine"), rule.wholeLine}});
    }
    return {{QStringLiteral("enabled"), config.enabled},
            {QStringLiteral("espIdfEnabled"), config.espIdfEnabled},
            {QStringLiteral("enabledPresets"), QJsonArray::fromStringList(normalizedPresetIds(config.enabledPresets))},
            {QStringLiteral("rules"), rules}};
}

ColorConfig fromJson(const QJsonObject &object)
{
    ColorConfig config;
    config.enabled = object.value(QStringLiteral("enabled")).toBool(config.enabled);
    config.espIdfEnabled = object.value(QStringLiteral("espIdfEnabled")).toBool(config.espIdfEnabled);
    QStringList presetIds;
    for (const QJsonValue &value : object.value(QStringLiteral("enabledPresets")).toArray()) {
        if (value.isString()) {
            presetIds.append(value.toString());
        }
    }
    config.enabledPresets = normalizedPresetIds(presetIds);
    const QJsonArray rules = object.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue &value : rules) {
        if (config.rules.size() >= MaximumColorRules) {
            break;
        }
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject ruleObject = value.toObject();
        ColorRule rule;
        rule.pattern = ruleObject.value(QStringLiteral("pattern")).toString();
        if (ruleObject.contains(QStringLiteral("color"))) {
            rule.color = QColor(ruleObject.value(QStringLiteral("color")).toString());
        }
        rule.enabled = ruleObject.value(QStringLiteral("enabled")).toBool(rule.enabled);
        rule.regularExpression = ruleObject.value(QStringLiteral("regularExpression")).toBool(rule.regularExpression);
        rule.caseSensitive = ruleObject.value(QStringLiteral("caseSensitive")).toBool(rule.caseSensitive);
        rule.wholeLine = ruleObject.value(QStringLiteral("wholeLine")).toBool(rule.wholeLine);
        config.rules.append(rule);
    }
    return config;
}

ColorConfig loadColorConfig()
{
    AppSettings settings;
    const QJsonDocument document =
        QJsonDocument::fromJson(settings.value(QStringLiteral("terminal/contentColors")).toString().toUtf8());
    return document.isObject() ? fromJson(document.object()) : ColorConfig{};
}

void saveColorConfig(const ColorConfig &config)
{
    AppSettings settings;
    settings.setValue(QStringLiteral("terminal/contentColors"),
                      QString::fromUtf8(QJsonDocument(toJson(config)).toJson(QJsonDocument::Compact)));
    settings.sync();
}

QString validationError(const ColorRule &rule)
{
    if (!rule.enabled) {
        return {};
    }
    if (rule.pattern.isEmpty()) {
        return AppI18n::text("匹配内容不能为空。");
    }
    if (!rule.color.isValid()) {
        return AppI18n::text("请选择有效颜色。");
    }
    if (rule.regularExpression) {
        const QRegularExpression expression = expressionForRule(rule);
        if (!expression.isValid()) {
            return AppI18n::text("正则表达式无效：%1").arg(expression.errorString());
        }
    }
    return {};
}

QVector<ColorPreset> colorPresets()
{
    const QColor red(QStringLiteral("#e05252"));
    const QColor amber(QStringLiteral("#b87900"));
    const QColor green(QStringLiteral("#218a5b"));
    const QColor blue(QStringLiteral("#3478d4"));
    return {
        {QStringLiteral("esp_idf"), AppI18n::text("ESP-IDF 日志"), QStringLiteral("I (123) / W (123) / E (123)"), {}},
        {QStringLiteral("log_levels"),
         AppI18n::text("通用日志等级"),
         QStringLiteral("ERROR / WARN / INFO / DEBUG"),
         {{QStringLiteral(R"(\b(?:ERROR|ERR|FATAL)\b)"), red, true, true, false, true},
          {QStringLiteral(R"(\bWARN(?:ING)?\b)"), amber, true, true, false, true},
          {QStringLiteral(R"(\bINFO\b)"), green, true, true, false, true},
          {QStringLiteral(R"(\bDEBUG\b)"), blue, true, true, false, true}}},
        {QStringLiteral("status"),
         AppI18n::text("成功与失败"),
         QStringLiteral("OK / PASS / SUCCESS / FAIL / TIMEOUT / 成功 / 失败 / 超时"),
         {{QStringLiteral(R"(\b(?:OK|PASS|SUCCESS)\b|成功)"), green, true, true, false, false},
          {QStringLiteral(R"(\bFAIL(?:ED|URE)?\b|失败)"), red, true, true, false, false},
          {QStringLiteral(R"(\bTIMEOUT\b|超时)"), amber, true, true, false, false}}},
        {QStringLiteral("at_commands"),
         AppI18n::text("AT 指令响应"),
         QStringLiteral("OK / ERROR / +CME ERROR / +CMS ERROR / CONNECT / NO CARRIER / BUSY"),
         {{QStringLiteral(R"(^\s*(?:ERROR|\+(?:CME|CMS)\s+ERROR(?:\s*:\s*.*)?)\s*$)"), red, true, true, false, true},
          {QStringLiteral(R"(^\s*(?:OK|CONNECT(?:\s+\d+)?)\s*$)"), green, true, true, false, true},
          {QStringLiteral(R"(^\s*(?:NO\s+CARRIER|BUSY)\s*$)"), amber, true, true, false, true}}}};
}

void ColorMatcher::setConfig(const ColorConfig &config)
{
    m_rules.clear();
    const auto appendRule = [this](const ColorRule &rule) {
        if (!rule.enabled || rule.pattern.isEmpty() || !rule.color.isValid()) {
            return;
        }
        QRegularExpression expression = expressionForRule(rule);
        if (!expression.isValid()) {
            return;
        }
        expression.optimize();
        m_rules.append({rule, expression});
    };
    if (config.enabled) {
        for (int index = 0; index < config.rules.size() && index < MaximumColorRules; ++index) {
            appendRule(config.rules.at(index));
        }
    }
    // The UI selection order does not change the documented preset priority.
    for (const ColorPreset &preset : colorPresets()) {
        if (!config.enabledPresets.contains(preset.id)) {
            continue;
        }
        for (const ColorRule &rule : preset.rules) {
            appendRule(rule);
        }
    }
}

bool ColorMatcher::hasRules() const { return !m_rules.isEmpty(); }

QVector<ColorSpan> ColorMatcher::ranges(const QString &text) const
{
    QVector<ColorSpan> spans;
    if (m_rules.isEmpty() || text.isEmpty()) {
        return spans;
    }

    struct Event
    {
        int position;
        int ruleIndex;
        int delta;
    };
    // QTextCursor positions and ColorSpan offsets are UTF-16 integers.
    const int textLength = static_cast<int>(std::min(text.size(), qsizetype(std::numeric_limits<int>::max())));
    int lineStart = 0;
    while (lineStart < textLength) {
        int lineEnd = lineStart;
        while (lineEnd < textLength && text.at(lineEnd) != QLatin1Char('\n') && text.at(lineEnd) != QLatin1Char('\r')) {
            ++lineEnd;
        }
        const QStringView line = QStringView(text).mid(lineStart, lineEnd - lineStart);
        QVector<Event> events;
        for (int ruleIndex = 0; ruleIndex < m_rules.size(); ++ruleIndex) {
            const CompiledRule &compiled = m_rules.at(ruleIndex);
            QRegularExpressionMatchIterator matches = compiled.expression.globalMatchView(line);
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                if (match.capturedLength() <= 0) {
                    continue;
                }
                const int start = compiled.rule.wholeLine ? lineStart : lineStart + int(match.capturedStart());
                const int end = compiled.rule.wholeLine ? lineEnd : lineStart + int(match.capturedEnd());
                events.append({start, ruleIndex, 1});
                events.append({end, ruleIndex, -1});
                if (compiled.rule.wholeLine) {
                    break;
                }
            }
        }

        // A sweep resolves overlaps without allocating one entry per character.
        // Rule indices preserve the user-defined priority, even for whole-line rules.
        std::sort(events.begin(), events.end(),
                  [](const Event &left, const Event &right) { return left.position < right.position; });
        QVector<int> counts(m_rules.size(), 0);
        std::set<int> active;
        int previous = lineStart;
        qsizetype eventIndex = 0;
        while (eventIndex < events.size()) {
            const int position = events.at(eventIndex).position;
            if (!active.empty()) {
                appendSpan(spans, previous, position - previous, m_rules.at(*active.begin()).rule.color);
            }
            do {
                const Event &event = events.at(eventIndex++);
                int &count = counts[event.ruleIndex];
                count += event.delta;
                if (count > 0) {
                    active.insert(event.ruleIndex);
                } else {
                    active.erase(event.ruleIndex);
                }
            } while (eventIndex < events.size() && events.at(eventIndex).position == position);
            previous = position;
        }

        lineStart = lineEnd;
        if (lineStart < textLength && text.at(lineStart) == QLatin1Char('\r')) {
            ++lineStart;
        }
        if (lineStart < textLength && text.at(lineStart) == QLatin1Char('\n')) {
            ++lineStart;
        }
    }
    return spans;
}

} // namespace AppTerminal
