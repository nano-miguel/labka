#include <QApplication>
#include <csignal>
#include "core/labka_core.h"
#include "ui/labka_ui.h"

LabkaCore* g_core = nullptr;

void signalHandler(int signum) {
    if (g_core) g_core->stop();
    exit(signum);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    signal(SIGINT, signalHandler);

    LabkaCore core;
    g_core = &core;
    core.start(2122);

    LabkaWindow window(core);
    window.show();

    int ret = app.exec();
    core.stop();
    return ret;
}