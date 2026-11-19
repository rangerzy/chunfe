#pragma once

#include <QString>

class QSettings;

namespace AppSettings {

inline constexpr QLatin1StringView ModelIdKey{"modelConnection/modelId"};
inline constexpr QLatin1StringView ApiKeyKey{"modelConnection/apiKey"};
inline constexpr QLatin1StringView OfflineModelPathKey{"offlineTranscription/modelPath"};
inline constexpr QLatin1StringView BackgroundAnalysisPlaceholder{"{{background}}"};

QString dataDirectory();
QString configFilePath();
QString backgroundAnalysisPrompt(const QString &configFilePath);
QString conversationAdvicePrompt(const QString &configFilePath);
bool migrateLegacyPreferences(QSettings &legacy, QSettings &config);

}
