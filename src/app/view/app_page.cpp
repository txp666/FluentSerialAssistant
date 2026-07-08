#include "app/view/app_page.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

AppPage::AppPage(const QString &title, const QString &subtitle, QWidget *parent, bool showHeader) : ScrollArea(parent)
{
    setObjectName(QStringLiteral("appPage"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (horizontalFluentScrollBar()) {
        horizontalFluentScrollBar()->setForceHidden(true);
    }

    m_view = new QWidget(this);
    m_view->setObjectName(QStringLiteral("appPageView"));
    m_viewLayout = new QVBoxLayout(m_view);
    m_viewLayout->setContentsMargins(showHeader ? 36 : 12, showHeader ? 20 : 12, showHeader ? 36 : 12,
                                     showHeader ? 36 : 12);
    m_viewLayout->setSpacing(showHeader ? 16 : 10);
    m_viewLayout->setAlignment(Qt::AlignTop);

    if (showHeader) {
        m_headerCard = new HeaderCardWidget(title, m_view);
        m_headerCard->setBorderRadius(8);
        m_headerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        auto *themeButton = new TransparentToolButton(icon(FluentIcon::Constract), m_headerCard);
        themeButton->setToolTip(QStringLiteral("切换主题"));
        m_headerCard->headerLayout()->addStretch(1);
        m_headerCard->headerLayout()->addWidget(themeButton);

        auto *subtitleLabel = new CaptionLabel(subtitle, m_headerCard->view());
        subtitleLabel->setTextColor(QColor(96, 96, 96), QColor(190, 190, 190));
        subtitleLabel->setWordWrap(true);
        m_headerCard->viewLayout()->setContentsMargins(16, 12, 16, 16);
        m_headerCard->viewLayout()->addWidget(subtitleLabel, 1);
        m_viewLayout->addWidget(m_headerCard, 0, Qt::AlignTop);

        connect(themeButton, &TransparentToolButton::clicked, this, []() {
            const Theme current = ThemeManager::instance()->effectiveTheme();
            const Theme next = current == Theme::Dark ? Theme::Light : Theme::Dark;
            FluentConfig::instance()->setThemeMode(next);
            FluentConfig::instance()->save();
            ThemeManager::instance()->setTheme(next);
        });
    }

    setWidget(m_view);
    setWidgetResizable(true);
}

QVBoxLayout *AppPage::contentLayout() const { return m_viewLayout; }

QWidget *AppPage::addSection(const QString &title, QWidget *content, int stretch)
{
    auto *card = new HeaderCardWidget(title, m_view);
    card->setBorderRadius(8);
    card->setSizePolicy(QSizePolicy::Expanding, stretch > 0 ? QSizePolicy::Expanding : QSizePolicy::Maximum);
    if (title.isEmpty()) {
        card->titleLabel()->hide();
        card->headerView()->hide();
        card->separator()->hide();
    }

    auto *cardLayout = card->viewLayout();
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(8);
    content->setParent(card->view());
    cardLayout->addWidget(content, stretch);

    m_viewLayout->addWidget(card, stretch, Qt::AlignTop);
    return card;
}

void AppPage::addHeaderAction(QWidget *action)
{
    if (!m_headerCard || !action) {
        return;
    }
    action->setParent(m_headerCard);
    m_headerCard->headerLayout()->addWidget(action);
}
