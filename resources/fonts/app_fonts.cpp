#include "app_fonts.h"

#include <QFontDatabase>
#include <QDir>
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

    const QDir interDirectory(QStringLiteral(":/fonts/Inter-4.1/extras/ttf"));
    const auto interFiles = interDirectory.entryList({QStringLiteral("*.ttf")},
                                                   QDir::Files, QDir::Name);
    if (interFiles.isEmpty()) {
        qCritical().noquote() << "No bundled Inter fonts found:" << interDirectory.path();
        return false;
    }
    QStringList paths = {QStringLiteral(":/fonts/oppo-sans/OPPO Sans 4.0.ttf")};
    for (const auto &file : interFiles)
        paths.append(interDirectory.filePath(file));

    QList<int> registeredIds;
    const auto rollback = [&registeredIds] {
        for (int id : registeredIds)
            QFontDatabase::removeApplicationFont(id);
    };

    for (const auto &path : paths) {
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            qCritical().noquote() << "Could not register bundled font:" << path;
            rollback();
            return false;
        }
        registeredIds.append(id);
    }

    // These family names belong to the bundled, unmodified font files.
    // Inter Display is registered too, for consumers that explicitly select it.
    const QString chineseFamily = QStringLiteral("OPPO Sans 4.0");
    const QString latinFamily = QStringLiteral("Inter");
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
        License{QStringLiteral("Inter 4.1 — SIL Open Font License 1.1"),
                QStringLiteral(":/fonts/Inter-4.1/LICENSE.txt")}
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
