#pragma once
#include <QSettings>
#include <QString>

inline QSettings &appSettings() {
    static QSettings s("ZetaPlayer", "ZetaPlayer");
    return s;
}

// Language is applied on startup; changing it asks for a restart.
inline bool isPtBr() {
    static bool v = appSettings().value("lang", "en").toString() == "pt";
    return v;
}

// T(english, portuguese) — English is the default language.
inline QString T(const char *en, const char *pt) {
    return QString::fromUtf8(isPtBr() ? pt : en);
}
