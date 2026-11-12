#pragma once

#include <QDebug>
#include <QLoggingCategory>

// Consumers should call cf_log_configure_target to assign their module name.
#ifndef CF_LOG_CATEGORY
#define CF_LOG_CATEGORY "default"
#endif

namespace CfLogDetail {
[[maybe_unused]] Q_STATIC_LOGGING_CATEGORY(category, CF_LOG_CATEGORY)
}

#define DEBUG_CF_LOG() qCDebug(CfLogDetail::category)
#define INFO_CF_LOG() qCInfo(CfLogDetail::category)
#define WARNING_CF_LOG() qCWarning(CfLogDetail::category)
#define ERROR_CF_LOG() qCCritical(CfLogDetail::category)

namespace CfLog {
// Call once at the beginning of each executable's main().
void initialize();
}
