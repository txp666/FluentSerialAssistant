#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtGui/QColor>

namespace AppTerminal {

constexpr int MaximumColorRules = 64;

struct ColorRule
{
    QString pattern;
    QColor color = QColor(QStringLiteral("#e05252"));
    bool enabled = true;
    bool regularExpression = false;
    bool caseSensitive = false;
    bool wholeLine = false;
};

struct ColorConfig
{
    bool enabled = true;
    bool espIdfEnabled = true;
    QVector<ColorRule> rules;
    QStringList enabledPresets;
};

struct ColorPreset
{
    QString id;
    QString name;
    QString example;
    QVector<ColorRule> rules;
};

struct ColorSpan
{
    int start = 0;
    int length = 0;
    QColor color;
};

QJsonObject toJson(const ColorConfig &config);
ColorConfig fromJson(const QJsonObject &object);
ColorConfig loadColorConfig();
void saveColorConfig(const ColorConfig &config);
QString validationError(const ColorRule &rule);
QVector<ColorPreset> colorPresets();

// Matches each content line independently. Custom rules take priority, followed
// by presets in colorPresets() order. ESP-IDF remains the renderer's fallback.
class ColorMatcher
{
  public:
    void setConfig(const ColorConfig &config);
    bool hasRules() const;
    QVector<ColorSpan> ranges(const QString &text) const;

  private:
    struct CompiledRule
    {
        ColorRule rule;
        QRegularExpression expression;
    };
    QVector<CompiledRule> m_rules;
};

} // namespace AppTerminal
