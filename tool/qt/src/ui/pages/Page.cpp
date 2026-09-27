#include "Page.h"

#include "Theme.h"
#include "Widgets.h"

#include <QLabel>
#include <QStyle>

Page::Page(Session &s, const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent), m_s(s), m_title(title)
{
    setObjectName(QStringLiteral("page"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(Theme::S5, Theme::S4, Theme::S5, Theme::S4);
    outer->setSpacing(Theme::S3);
    auto *t = new QLabel(title, this);
    t->setObjectName(QStringLiteral("pageTitle"));
    auto *st = new QLabel(subtitle, this);
    st->setObjectName(QStringLiteral("pageSubtitle"));
    st->setWordWrap(true);
    outer->addWidget(t);
    outer->addWidget(st);
    // every page says when its values stopped being live: a stale frame never looks live
    m_stale = new QLabel(this);
    m_stale->setObjectName(QStringLiteral("pill"));
    m_stale->setProperty("severity", QStringLiteral("warn"));
    m_stale->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_stale->hide();
    outer->addWidget(m_stale);
    connect(&m_s, &Session::tick, this, [this] {
        const Session::Link l = m_s.link();
        const bool show = m_s.hasFrame() && (l == Session::Link::Stale || l == Session::Link::Paused || l == Session::Link::Failed);
        if (show) {
            const qint64 age = m_s.ageMs();
            m_stale->setText(QStringLiteral("  %1 — the last frame arrived %2 ago; the values shown are the last received, not live.  ")
                                 .arg(Session::linkText(l).toUpper(), age < 1000 ? QStringLiteral("%1 ms").arg(age)
                                                                                 : QStringLiteral("%1 s").arg(age / 1000.0, 0, 'f', 1)));
        }
        m_stale->setVisible(show);
    });
    m_content = new QVBoxLayout;
    m_content->setSpacing(Theme::S3);
    outer->addLayout(m_content, 1);
}

void Page::onFrames(std::function<void(const TelemetryFrame &)> render)
{
    m_render = std::move(render);
    connect(&m_s, &Session::frame, this, [this] { m_pending = true; });
    m_frameTimer.setInterval(33);
    connect(&m_frameTimer, &QTimer::timeout, this, [this] {
        if (m_pending && m_s.hasFrame()) {
            m_pending = false;
            m_render(m_s.last());
        }
    });
    m_frameTimer.start();
}

QVBoxLayout *Page::scrollContent()
{
    auto *area = new QScrollArea(this);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(area);
    inner->setObjectName(QStringLiteral("page"));
    auto *l = new QVBoxLayout(inner);
    l->setContentsMargins(0, 0, Theme::S2, 0);
    l->setSpacing(Theme::S3);
    area->setWidget(inner);
    m_content->addWidget(area);
    return l;
}

// verify.h img_root_t, as the hello names it (tool/PROTOCOL.md A.5.1 `root`) and DID 0xFD23 carries it
QString Page::rootText(const QJsonObject &root, Theme::Sev *sev)
{
    const QString kind = root.value(QLatin1String("kind")).toString(), id = root.value(QLatin1String("key_id")).toString();
    Theme::Sev s = Theme::Sev::Ok;
    QString t;
    if (kind == QLatin1String("test")) {
        s = Theme::Sev::Crit;
        t = QStringLiteral("TEST root of trust — development unit, not for delivery");
    } else if (kind == QLatin1String("none")) {
        s = Theme::Sev::Crit;
        t = QStringLiteral("Root of trust: no key — every image refused");
    } else if (kind == QLatin1String("build") || kind == QLatin1String("otp")) {
        t = QStringLiteral("Root of trust: %1, key id %2").arg(kind == QLatin1String("otp") ? QStringLiteral("the OTP/HSE key") : QStringLiteral("the build's release key"), id);
    } else if (!kind.isEmpty()) {
        s = Theme::Sev::Warn;
        t = QStringLiteral("Root of trust: kind %1, key id %2 (unknown to this tool)").arg(kind, id);
    }
    if (sev) {
        *sev = s;
    }
    return t;
}

QLabel *Page::addRootBanner()
{
    auto *b = new QLabel(this);
    b->setObjectName(QStringLiteral("pill"));
    b->setAccessibleName(QStringLiteral("rootBanner"));
    b->setWordWrap(true);
    b->hide();
    static_cast<QVBoxLayout *>(layout())->insertWidget(2, b); // under the title and the subtitle
    const auto render = [this, b] {
        const QJsonObject root = m_s.deviceInfo().value(QLatin1String("root")).toObject();
        Theme::Sev sev = Theme::Sev::Neutral;
        const QString t = rootText(root, &sev);
        setPill(b, QStringLiteral("  %1  ").arg(t), sev);
        b->setToolTip(QStringLiteral("The firmware image verifier's root of trust (FW-38), from %1; key id = SHA-256 of the public key, first 4 bytes: %2")
                          .arg(root.value(QLatin1String("source")).toString(QStringLiteral("the bridge's hello")), root.value(QLatin1String("key_id")).toString()));
        b->setVisible(!t.isEmpty());
    };
    connect(&m_s, &Session::infoChanged, b, render);
    connect(&m_s, &Session::transportChanged, b, render);
    return b;
}
