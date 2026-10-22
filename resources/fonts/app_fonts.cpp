#include "app_fonts.h"

#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QResource>
#include <QThread>
#include <QDebug>

#include <array>

// Q_INIT_RESOURCE must be used outside a namespace so a static consumer pulls
// in the resource initializer from the library.
static void initializeFontResources()
{
    Q_INIT_RESOURCE(chunfe_font_assets);
}

namespace ChunfeFonts {

bool initialize()
{
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application || QThread::currentThread() != application->thread()) {
        qCritical("ChunfeFonts::initialize requires QGuiApplication and its GUI thread");
        return false;
    }

    static bool initialized = false;
    if (initialized)
        return true;

    static const bool resourcesInitialized = [] {
        initializeFontResources();
        return true;
    }();
    Q_UNUSED(resourcesInitialized);

    const std::array paths = {
        QStringLiteral(":/fonts/oppo-sans/OPPO Sans 4.0.ttf"),
        QStringLiteral(":/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"),
        QStringLiteral(":/fonts/jetbrains-mono/JetBrainsMono-SemiBold.ttf"),
        QStringLiteral(":/fonts/jetbrains-mono/JetBrainsMono-Bold.ttf")
    };
    QList<int> registeredIds;
    QString chineseFamily;
    QString latinFamily;
    const auto rollback = [&registeredIds] {
        for (int id : registeredIds)
            QFontDatabase::removeApplicationFont(id);
    };

    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto &path = paths[index];
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            qCritical().noquote() << "Could not register bundled font:" << path;
            rollback();
            return false;
        }
        registeredIds.append(id);
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            qCritical().noquote() << "Bundled font has no usable family:" << path;
            rollback();
            return false;
        }
        if (index == 0)
            chineseFamily = families.constFirst();
        else if (index == 1)
            latinFamily = families.constFirst();
        else if (!families.contains(latinFamily)) {
            qCritical().noquote() << "Bundled font does not match the Latin family:"
                                 << path << "expected" << latinFamily;
            rollback();
            return false;
        }
    }

    // Keep the existing application size and features. Normal text uses weight
    // 400; titles and emphasis can still request the supplied heavier faces.
    QFont font = QGuiApplication::font();
    font.setFamilies({latinFamily, chineseFamily});
    font.setStyleName(QString());
    font.setWeight(QFont::Normal);
    QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, chineseFamily);
    QGuiApplication::setFont(font);
    initialized = true;
    return true;
}

QString licenseNotices()
{
    struct License {
        QString title;
        QString path;
    };
    const std::array licenses = {
        License{QStringLiteral("OPPO Sans 4.0 — License Notice"),
                QStringLiteral(":/fonts/oppo-sans/OPPO Sans 4.0 License Notice.txt")},
        License{QStringLiteral("JetBrains Mono 2.304 — SIL Open Font License 1.1"),
                QStringLiteral(":/fonts/jetbrains-mono/OFL.txt")}
    };
    QStringList sections;
    for (const auto &license : licenses) {
        QFile file(license.path);
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical().noquote() << "Could not read bundled font license:"
                                 << license.path << file.errorString();
            return {};
        }
        const auto contents = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            qCritical().noquote() << "Could not read bundled font license:"
                                 << license.path << file.errorString();
            return {};
        }
        sections.append(license.title + QStringLiteral("\n\n") + QString::fromUtf8(contents));
    }
    return sections.join(QStringLiteral("\n\n"));
}

} // namespace ChunfeFonts
