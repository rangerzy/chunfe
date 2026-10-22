#pragma once

#include <QString>

namespace ChunfeFonts {

// Call on the GUI thread after constructing QGuiApplication and before loading
// QML. Registers bundled fonts once and applies the application's font policy.
// Returns false if a bundled font cannot be registered.
bool initialize();

// After initialize(), returns both bundled licenses with display headings.
// Returns an empty string and reports the resource path if either cannot be read.
QString licenseNotices();

} // namespace ChunfeFonts
