#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QDir>
#include <QIcon>
#include <csignal>
#include <unistd.h>

#include "core/labka_core.h"
#include "core/auth_manager.h"
#include "ui/labka_ui.h"

LabkaCore* g_core = nullptr;

void signalHandler(int signum) {
    if (g_core) g_core->stop();
    _exit(signum);
}

static QString findTranslationsDir() {
    const QString appDir = QApplication::applicationDirPath();

    QStringList candidates = {
        appDir + "/translations",
        appDir + "/../translations",
        appDir + "/../../translations",
        appDir + "/../share/rubic-labka/translations",
        QDir::currentPath() + "/translations",
        QStringLiteral("/usr/share/rubic-labka/translations"),
        QStringLiteral("/usr/local/share/rubic-labka/translations"),
        QString::fromLocal8Bit(qgetenv("LABKA_TRANSLATIONS"))
    };

#ifdef LABKA_SOURCE_DIR
    candidates << QString(LABKA_SOURCE_DIR) + "/translations";
#endif

    for (const QString& c : candidates) {
        if (c.isEmpty()) continue;
        QDir d(c);
        if (d.exists() && !d.entryList(QStringList() << "*.qm", QDir::Files).isEmpty()) {
            return d.absolutePath();
        }
    }
    return QString();
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    signal(SIGINT, signalHandler);

    // ★ Logo SVG de la carpeta (icono de app y de todas las ventanas)
    const QIcon app_icon = labkaIconAsset("assets/LOGO/Rubik.svg");
    if (!app_icon.isNull()) {
        QApplication::setWindowIcon(app_icon);
    } else {
        const QIcon png = labkaIconAsset("assets/LOGO/Rubik.png");
        if (!png.isNull()) QApplication::setWindowIcon(png);
    }

    QSettings ui_settings(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "ui");
    const QString lang = ui_settings.value("lang", "es").toString();

    static QTranslator translator;
    if (lang != "es") {
        const QString dir = findTranslationsDir();
        if (!dir.isEmpty() && translator.load("rubik-labka_" + lang, dir)) {
            QApplication::installTranslator(&translator);
        }
    }

    AuthManager auth;

    LoginDialog login(&auth);
    if (login.exec() != QDialog::Accepted) return 0;
    if (!auth.isLoggedIn()) return 0;

    LabkaCore core;
    g_core = &core;

    for (const auto& [ip, name] : auth.getPeerNames()) {
        core.addManualPeer(ip, name);
    }

    core.start(2122);

    LabkaWindow window(core, auth);
    window.show();

    int ret = app.exec();

    core.stop();
    return ret;
}