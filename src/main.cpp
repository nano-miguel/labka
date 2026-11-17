#include <QApplication>
#include <csignal>

#include "core/labka_core.h"
#include "core/auth_manager.h"
#include "ui/labka_ui.h"   // ÚNICO header UI: LoginDialog + NetworkMap + LabkaWindow

LabkaCore* g_core = nullptr;

void signalHandler(int signum) {
    if (g_core) g_core->stop();
    exit(signum);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    signal(SIGINT, signalHandler);

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