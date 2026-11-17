// src/ui/main_window.cpp
// Implementación de LabkaWindow (declarada en labka_ui.h). SIN header propio.
// Iconos: SOLO assets de la carpeta. Sin fallbacks de Qt.
#include "labka_ui.h"

#include <QToolBar>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QApplication>
#include <QCursor>
#include <QFile>
#include <QDir>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialog>
#include <QCloseEvent>
#include <QSettings>

// ==========================================
// HELPERS
// ==========================================
static QString labkaShellQuote(const QString& value) {
    QString v = value;
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

static QString labkaRemoteCommand(
    const QString& user,                                  
    const QString& pass,                                 
    const QString& script) {
    QString bash = "bash -c " + labkaShellQuote(script) + " 2>&1";

    if (user.compare("root", Qt::CaseInsensitive) == 0) 
        return bash;
    if (pass.isEmpty()) 
        return "sudo -n " + bash;
    return "printf '%s\\n' " + labkaShellQuote(pass) + " | sudo -S -p '' " + bash;
}

static QString labkaInstallPackage(
    const QString& debPkg,                                   
    const QString& rpmPkg,                                  
    const QString& archPkg) {
    return
        "if command -v apt-get >/dev/null 2>&1; then "
            "export DEBIAN_FRONTEND=noninteractive; "
            "apt-get update -y; apt-get install -y " + debPkg + "; "
        "elif command -v dnf >/dev/null 2>&1; then "
            "dnf install -y " + rpmPkg + "; "
        "elif command -v yum >/dev/null 2>&1; then "
            "yum install -y " + rpmPkg + "; "
        "elif command -v pacman >/dev/null 2>&1; then "
            "pacman -Sy --noconfirm " + archPkg + "; "
        "elif command -v zypper >/dev/null 2>&1; then "
            "zypper --non-interactive install " + rpmPkg + "; "
        "else echo LABKA_NO_PKG_MGR; fi; ";
}

static QString labkaOpenPort(int port) {
    const QString p = QString::number(port);
    return
        "if command -v ufw >/dev/null 2>&1; then "
            "ufw allow " + p + "/tcp >/dev/null 2>&1 || true; "
        "elif command -v firewall-cmd >/dev/null 2>&1; then "
            "firewall-cmd --permanent --add-port=" + p + "/tcp >/dev/null 2>&1 || true; "
            "firewall-cmd --reload >/dev/null 2>&1 || true; "
        "elif command -v iptables >/dev/null 2>&1; then "
            "iptables -I INPUT -p tcp --dport " + p + " -j ACCEPT >/dev/null 2>&1 || true; "
        "fi; ";
}

static QString labkaEnableSSH() {
    return
        "for svc in ssh sshd; do "
            "systemctl enable --now $svc >/dev/null 2>&1 || true; "
        "done; ";
}


// CONSTRUCTOR

LabkaWindow::LabkaWindow(LabkaCore& core, AuthManager& auth, QWidget* parent)
    : QMainWindow(parent), core_(core), auth_(auth) {

    setWindowTitle(QStringLiteral("LABKA"));
    setWindowIcon(labkaIconAsset("assets/LOGO/Rubik.svg"));
    resize(1000, 500);
    setAttribute(Qt::WA_TranslucentBackground, true);
    applyDarkTheme();

    QToolBar* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setFixedHeight(35);
    toolbar->setStyleSheet(
        "QToolBar { background-color: #0a0a0f; border-bottom: 1px solid #ffffff; "
        "padding: 2px; spacing: 8px; }"
    );

    const QString profile_name = QString::fromStdString(auth_.getCurrentProfile());

    QPushButton* btn_account = new QPushButton(profile_name.left(1).toUpper(), this);
    btn_account->setFixedSize(24, 24);
    btn_account->setCursor(Qt::PointingHandCursor);
    btn_account->setToolTip(TR("Cuenta activa: %1").arg(profile_name));
    btn_account->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #000; border: none; border-radius: 12px; "
        "font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #dddddd; }"
    );
    connect(btn_account, &QPushButton::clicked, this, &LabkaWindow::showAccountMenu);
    toolbar->addWidget(btn_account);

    QLabel* lbl_user = new QLabel(profile_name, this);
    lbl_user->setStyleSheet("QLabel { color: #ffffff; font-weight: bold; font-size: 13px; padding-right: 4px; }");
    toolbar->addWidget(lbl_user);

    toolbar->addSeparator();

    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Refresh: SVG de la carpeta; si no existe, carácter ⟳ (nada de iconos Qt)
    const QIcon refresh_ic = labkaIconAsset("assets/refresh.svg");
    QPushButton* btn_refresh = new QPushButton(this);
    if (!refresh_ic.isNull()) btn_refresh->setIcon(refresh_ic);
    else                      btn_refresh->setText(QStringLiteral("⟳"));
    btn_refresh->setFixedSize(28, 28);
    btn_refresh->setCursor(Qt::PointingHandCursor);
    btn_refresh->setToolTip(TR("Actualizar / forzar reconexión ahora"));
    btn_refresh->setStyleSheet(
        "QPushButton { background-color: transparent; color: #ffffff; border: 1px solid #ffffff; "
        "border-radius: 14px; font-size: 15px; font-weight: bold; }"
        "QPushButton:hover { background-color: #ffffff; color: #000; }"
    );
    connect(btn_refresh, &QPushButton::clicked, this, [this]() {
        reconnectSavedPeers();
        checkInternet();
        refreshUI();
    });
    toolbar->addWidget(btn_refresh);

    QPushButton* btn_add = new QPushButton(TR("+ Agregar Dispositivo"), this);
    btn_add->setCursor(Qt::PointingHandCursor);
    btn_add->setStyleSheet(
        "QPushButton { background-color: #1a1a2e; color: #ffffff; border: 1px solid #ffffff; "
        "padding: 4px 10px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #ffffff; color: #000; }"
    );
    connect(btn_add, &QPushButton::clicked, this, &LabkaWindow::showAddDeviceDialog);
    toolbar->addWidget(btn_add);

    QWidget* central = new QWidget(this);
    central->setStyleSheet(
        "background-color: rgba(15, 15, 20, 180); "
        "border: 1px solid rgba(255, 255, 255, 50); border-radius: 10px;"
    );

    QVBoxLayout* main_layout = new QVBoxLayout(central);
    main_layout->setContentsMargins(10, 10, 10, 10);
    map_widget_ = new NetworkMap(this);
    main_layout->addWidget(map_widget_);
    setCentralWidget(central);

    connect(map_widget_, &NetworkMap::nodeClicked, this, &LabkaWindow::showNodeMenu);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &LabkaWindow::refreshUI);
    timer_->start(1000);

    net_timer_ = new QTimer(this);
    connect(net_timer_, &QTimer::timeout, this, &LabkaWindow::checkInternet);
    net_timer_->start(3000);
    checkInternet();

    reconnectSavedPeers();
}

void LabkaWindow::closeEvent(QCloseEvent* event) {
    if (map_widget_) map_widget_->saveLayout();
    QMainWindow::closeEvent(event);
}

void LabkaWindow::applyDarkTheme() {
    setStyleSheet(
        "QMainWindow { background-color: transparent; } "
        "QMenu { background-color: #1a1a2e; color: #ffffff; border: 1px solid #ffffff; } "
        "QMenu::item { padding: 7px 22px; }"
        "QMenu::item:selected { background-color: #ffffff; color: #000; }"
        "QMenu::item:disabled { color: #9a9aa8; }"
        "QMenu::separator { height: 1px; background: #ffffff; margin: 5px 8px; }"
    );
}

void LabkaWindow::checkInternet() {
    if (ping_proc_) return;

    ping_proc_ = new QProcess(this);

    QTimer* guard = new QTimer(ping_proc_);
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, this, [this]() {
        if (ping_proc_ && ping_proc_->state() != QProcess::NotRunning) ping_proc_->kill();
    });

    connect(ping_proc_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        const bool online = (status == QProcess::NormalExit && code == 0);
        if (map_widget_) map_widget_->setInternetStatus(online);
        if (ping_proc_) { ping_proc_->deleteLater(); ping_proc_ = nullptr; }
        refreshUI();
    });

    connect(ping_proc_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        if (map_widget_) map_widget_->setInternetStatus(false);
        if (ping_proc_) { ping_proc_->deleteLater(); ping_proc_ = nullptr; }
        refreshUI();
    });

    guard->start(3500);

    const QString script =
        "(exec 3<>/dev/tcp/1.1.1.1/53) 2>/dev/null && exit 0; "
        "(exec 3<>/dev/tcp/8.8.8.8/53) 2>/dev/null && exit 0; "
        "(exec 3<>/dev/tcp/9.9.9.9/53) 2>/dev/null && exit 0; "
        "exit 1";

    ping_proc_->start("bash", QStringList() << "-c" << script);
}

void LabkaWindow::quitApp() {
    QApplication::quit();
}

void LabkaWindow::applyLanguage(const QString& code) {
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "ui");
    s.setValue("lang", code);
    s.sync();

    if (map_widget_) map_widget_->saveLayout();

    QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
    QApplication::quit();
}

void LabkaWindow::showAccountMenu() {
    QMenu menu(this);

    QAction* act_profile = menu.addAction(
        TR("Perfil: %1").arg(QString::fromStdString(auth_.getCurrentProfile())));
    act_profile->setEnabled(false);

    menu.addSeparator();

    QAction* act_auto = menu.addAction(TR("Auto conexión"));
    act_auto->setCheckable(true);
    act_auto->setChecked(true);
    connect(act_auto, &QAction::toggled, this, [this](bool on) {
        core_.setAutoReconnect(on);
        refreshUI();
    });

    QMenu* lang = menu.addMenu(TR("Idiomas"));

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "ui");
    const QString current_lang = s.value("lang", "es").toString();

    struct LangOpt { const char* code; const char* native; };
    const LangOpt opts[] = {
        {"es", "Espa\xc3\xb1ol"},
        {"en", "English"},
        {"fr", "Fran\xc3\xa7ais"},
        {"de", "Deutsch"},
        {"ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
        {"zh", "\xe4\xb8\xad\xe6\x96\x87"},
        {"ja", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
        {"hi", "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80"}
    };

    for (const auto& o : opts) {
        QAction* a = lang->addAction(QString::fromUtf8(o.native));
        a->setCheckable(true);
        a->setChecked(current_lang == o.code);
        const QString code = QString::fromLatin1(o.code);
        connect(a, &QAction::triggered, this, [this, code]() { applyLanguage(code); });
    }

    // Único icono de menú que sí existe en assets: gear.svg
    QAction* act_config = menu.addAction(TR("Configuración"));
    const QIcon gear = labkaIconAsset("assets/gear.svg");
    if (!gear.isNull()) act_config->setIcon(gear);

    menu.addSeparator();
    QAction* act_logout = menu.addAction(TR("Cerrar sesión"));
    QAction* act_exit   = menu.addAction(TR("Salir"));

    QAction* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected == act_config) {
        showConfigDialog();
    } else if (selected == act_logout) {
        if (map_widget_) map_widget_->saveLayout();
        auth_.logout();
        QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
        QApplication::quit();
    } else if (selected == act_exit) {
        quitApp();
    }
}

void LabkaWindow::showConfigDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(TR("Configuración"));
    dialog.setWindowIcon(labkaIconAsset("assets/LOGO/Rubik.svg"));
    dialog.resize(440, 400);
    dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    const QString lbl_style  = "color:#ffffff; font-weight:bold;";
    const QString spin_style = "QSpinBox { background:#000; color:#fff; border:1px solid #fff; padding:4px; }";

    QLabel* lbl_trans = new QLabel(TR("Transparencia de la ventana:"), &dialog);
    lbl_trans->setStyleSheet(lbl_style);
    layout->addWidget(lbl_trans);

    QSlider* slider_trans = new QSlider(Qt::Horizontal, &dialog);
    slider_trans->setRange(30, 100);
    slider_trans->setValue(static_cast<int>(windowOpacity() * 100));
    layout->addWidget(slider_trans);

    QLabel* lbl_size = new QLabel(TR("Tamaño de la ventana:"), &dialog);
    lbl_size->setStyleSheet(lbl_style + "margin-top:10px;");
    layout->addWidget(lbl_size);

    QHBoxLayout* size_row = new QHBoxLayout();
    QSpinBox* spin_w = new QSpinBox(&dialog);
    spin_w->setRange(600, 3840);
    spin_w->setValue(width());
    spin_w->setSuffix(TR(" px ancho"));
    QSpinBox* spin_h = new QSpinBox(&dialog);
    spin_h->setRange(400, 2160);
    spin_h->setValue(height());
    spin_h->setSuffix(TR(" px alto"));
    spin_w->setStyleSheet(spin_style);
    spin_h->setStyleSheet(spin_style);
    size_row->addWidget(spin_w);
    size_row->addWidget(spin_h);
    layout->addLayout(size_row);

    QLabel* lbl_ip = new QLabel(TR("Privacidad de la dirección IP:"), &dialog);
    lbl_ip->setStyleSheet(lbl_style + "margin-top:12px;");
    layout->addWidget(lbl_ip);

    QCheckBox* chk_ip = new QCheckBox(TR("IP incompleta (ocultar parte de la IP)"), &dialog);
    chk_ip->setStyleSheet("QCheckBox { color:#ffffff; }");
    chk_ip->setChecked(map_widget_->ipPrivacy() > 0);
    layout->addWidget(chk_ip);

    QHBoxLayout* ip_row = new QHBoxLayout();
    QLabel* lbl_oct = new QLabel(TR("Octetos visibles:"), &dialog);
    lbl_oct->setStyleSheet("color:#ffffff;");
    QSpinBox* spin_oct = new QSpinBox(&dialog);
    spin_oct->setRange(1, 3);
    spin_oct->setValue(map_widget_->ipPrivacy() > 0 ? map_widget_->ipPrivacy() : 1);
    spin_oct->setStyleSheet(spin_style);
    spin_oct->setEnabled(chk_ip->isChecked());
    ip_row->addWidget(lbl_oct);
    ip_row->addWidget(spin_oct);
    ip_row->addStretch();
    layout->addLayout(ip_row);

    QLabel* lbl_preview = new QLabel("", &dialog);
    lbl_preview->setStyleSheet("color:#ffcc00; font-size:12px;");
    layout->addWidget(lbl_preview);

    auto applyIpPrivacy = [this, chk_ip, spin_oct, lbl_preview]() {
        const int octets = chk_ip->isChecked() ? spin_oct->value() : 0;
        map_widget_->setIpPrivacy(octets);
        lbl_preview->setText(TR("Ejemplo: %1")
            .arg(map_widget_->maskIp(QString::fromStdString(core_.getMyIP()))));
        refreshUI();
    };

    connect(chk_ip, &QCheckBox::toggled, this, [spin_oct, applyIpPrivacy](bool on) {
        spin_oct->setEnabled(on);
        applyIpPrivacy();
    });
    connect(spin_oct, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [applyIpPrivacy](int) { applyIpPrivacy(); });

    applyIpPrivacy();

    connect(slider_trans, &QSlider::valueChanged, this, [this](int v) {
        setWindowOpacity(v / 100.0);
    });
    connect(spin_w, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, spin_h](int w) {
        resize(w, spin_h->value());
    });
    connect(spin_h, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, spin_w](int h) {
        resize(spin_w->value(), h);
    });

    QPushButton* btn_close = new QPushButton(TR("Listo"), &dialog);
    btn_close->setStyleSheet("QPushButton { background:#fff; color:#000; padding:8px; font-weight:bold; border-radius:4px; }");
    connect(btn_close, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(btn_close);

    dialog.exec();
}

void LabkaWindow::reconnectSavedPeers() {
    const std::string local_ip = core_.getMyIP();

    for (const auto& ip : auth_.getSavedIPs()) {
        if (ip == local_ip) continue;

        std::string user, pass;
        if (!auth_.getCredential(ip, user, pass)) continue;

        core_.setPeerCredentials(ip, user, pass);
    }

    core_.requestReconnectAll();
    refreshUI();
}

void LabkaWindow::refreshUI() {
    auto peers = core_.getPeers();
    map_widget_->updateNetwork(core_.getMyIP(), core_.getMyHostname(), peers, &core_);
}

bool LabkaWindow::getCredentials(const QString& ip, QString& user, QString& pass) {
    if (ip == QString::fromStdString(core_.getMyIP())) { user = "local"; pass = ""; return true; }

    std::string u, p;
    if (auth_.getCredential(ip.toStdString(), u, p)) {
        user = QString::fromStdString(u);
        pass = QString::fromStdString(p);
        core_.setPeerCredentials(ip.toStdString(), u, p);
        return true;
    }

    bool ok;
    user = QInputDialog::getText(this, TR("SSH User"), TR("Usuario SSH para %1:").arg(ip),
        QLineEdit::Normal, "", &ok);
    if (!ok) return false;
    if (user.trimmed().isEmpty()) user = "root";

    pass = QInputDialog::getText(this, TR("SSH Password"),
        TR("Contraseña (deja en blanco si usas llave):"), QLineEdit::Password, "", &ok);
    if (!ok) return false;

    auth_.addCredential(ip.toStdString(), user.toStdString(), pass.toStdString());
    core_.setPeerCredentials(ip.toStdString(), user.toStdString(), pass.toStdString());
    return true;
}

void LabkaWindow::launchSystemTerminal(const QString& ip, const QString& user,
                                       const QString& pass, bool is_local) {
    QString script;
    if (is_local) script = "bash";
    else {
        QString ssh_cmd = (!pass.isEmpty() && QFile::exists("/usr/bin/sshpass"))
            ? QString("sshpass -p '%1' ssh %2@%3").arg(pass, user, ip)
            : QString("ssh %1@%2").arg(user, ip);
        script = QString("%1; echo ''; read -p 'Presiona Enter para cerrar...'").arg(ssh_cmd);
    }

    QStringList args; args << "-e" << "bash" << "-c" << script;
    bool success = QProcess::startDetached("x-terminal-emulator", args);
    if (!success) success = QProcess::startDetached("gnome-terminal", QStringList() << "--" << "bash" << "-c" << script);
    if (!success) success = QProcess::startDetached("konsole", args);
    if (!success) success = QProcess::startDetached("xterm", args);

    if (success && !is_local) {
        core_.markSessionActive(ip.toStdString(), SessionKind::Terminal);
        refreshUI();
    }
    if (!success) QMessageBox::critical(this, TR("Error"), TR("No se encontró un emulador de terminal compatible."));
}

void LabkaWindow::openSystemFileExplorer(const QString& ip, const QString& user, bool is_local) {
    if (is_local) { QProcess::startDetached("xdg-open", QStringList() << QDir::homePath()); return; }

    QString sftpUrl = QString("sftp://%1@%2").arg(user, ip);
    QProcess checkProcess;
    checkProcess.start("dpkg-query", QStringList() << "-W" << "-f=${Status}" << "gvfs-backends");
    checkProcess.waitForFinished();
    if (!checkProcess.readAllStandardOutput().contains("install ok installed")) {
        QMessageBox::warning(this, TR("Dependencia"),
            "Debian/Ubuntu: gvfs-backends\nFedora: gvfs-fuse gvfs-smb\nArch: gvfs-smb gvfs-nfs");
        return;
    }

    bool success = QProcess::startDetached("xdg-open", QStringList() << sftpUrl);
    if (!success) success = QProcess::startDetached("gio", QStringList() << "open" << sftpUrl);
    if (success) { core_.markSessionActive(ip.toStdString(), SessionKind::Files); refreshUI(); }
    if (!success) QMessageBox::critical(this, TR("Error"), TR("No se pudo abrir el explorador."));
}

void LabkaWindow::launchSystemVNC(const QString& ip) {
    bool success = false;
    if (QFile::exists("/usr/bin/remmina")) success = QProcess::startDetached("remmina", QStringList() << "-c" << ("vnc://" + ip));
    else if (QFile::exists("/usr/bin/vncviewer")) success = QProcess::startDetached("vncviewer", QStringList() << ip);
    else if (QFile::exists("/usr/bin/krdc")) success = QProcess::startDetached("krdc", QStringList() << ("vnc://" + ip));

    if (success) { core_.markSessionActive(ip.toStdString(), SessionKind::Vnc); refreshUI(); }
    if (!success) QMessageBox::information(this, TR("VNC no encontrado"), TR("Instala remmina, tigervnc-viewer o krdc."));
}

void LabkaWindow::autoConfigureRemoteSSH(const QString& ip, const QString& user, const QString& pass) {
    auto client = core_.getSSHClient(ip.toStdString());
    if (!client || !client->isConnected()) return;

    QString script = labkaOpenPort(22) + labkaEnableSSH() + "echo LABKA_SSH_OK";

    std::string out, err;
    client->executeCommand(labkaRemoteCommand(user, pass, script).toStdString(), out, err);
}

void LabkaWindow::launchVNCWithSetup(
    const QString& ip, 
    const QString& user, 
    const QString& pass) {

    auto client = core_.getSSHClient(ip.toStdString());
    if (!client) { QMessageBox::critical(this, TR("Error"), TR("No se pudo obtener el cliente SSH.")); return; }

    std::string err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!client->isConnected()) {
        if (!client->connect(ip.toStdString(), user.toStdString(), pass.toStdString(), err)) {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical(this, TR("Error"),
                TR("No se pudo conectar por SSH:") + "\n\n" + QString::fromStdString(err));
            return;
        }
    }
    QApplication::restoreOverrideCursor();

    auto runRemote = [&](const QString& cmd, QString& out, QString& errTxt) -> bool {
        std::string o, e;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool ok = client->executeCommand(cmd.toStdString(), o, e);
        QApplication::restoreOverrideCursor();
        out = QString::fromStdString(o);
        errTxt = QString::fromStdString(e);
        return ok;
    };

    QString out, errTxt;
    bool ok = runRemote("command -v x11vnc >/dev/null 2>&1 && echo VNC_INSTALLED || echo VNC_MISSING", out, errTxt);
    if (!ok && out.trimmed().isEmpty()) {
        QMessageBox::critical(this, TR("Error"), TR("No se pudo comprobar VNC:") + "\n\n" + errTxt);
        return;
    }

    bool installed = out.contains("VNC_INSTALLED");
    if (!installed) {
        if (QMessageBox::question(this, TR("VNC remoto"),
                TR("No detecté x11vnc en %1.\n\n¿Instalarlo usando SSH?\n(Debian/Ubuntu, Fedora, Arch u openSUSE, se detecta solo)").arg(ip))
                != QMessageBox::Yes) return;

        QString install_script =
            labkaInstallPackage("x11vnc", "x11vnc", "x11vnc") +
            labkaOpenPort(5900) +
            "if command -v x11vnc >/dev/null 2>&1; then echo VNC_INSTALL_OK; else echo VNC_INSTALL_FAIL; fi";

        ok = runRemote(labkaRemoteCommand(user, pass, install_script), out, errTxt);
        installed = out.contains("VNC_INSTALL_OK");
        if (!installed) {
            QMessageBox::critical(this, TR("Error"), TR("No se pudo instalar x11vnc.") + "\n\n" + out + "\n" + errTxt);
            return;
        }
    }

    ok = runRemote("pgrep -x x11vnc >/dev/null 2>&1 && echo VNC_RUNNING || echo VNC_STOPPED", out, errTxt);
    bool running = out.contains("VNC_RUNNING");

    if (!running) {
        if (QMessageBox::question(this, TR("VNC remoto"),
                TR("x11vnc instalado pero no en ejecución.\n\n¿Iniciarlo en el puerto 5900?")) == QMessageBox::Yes) {
            QString start_script =
                "if pgrep -x x11vnc >/dev/null 2>&1; then echo VNC_RUNNING; else export DISPLAY=:0; "
                "(x11vnc -display :0 -auth guess -forever -shared -nopw -rfbport 5900 >/tmp/x11vnc.labka.log 2>&1 &); "
                "sleep 1; if pgrep -x x11vnc >/dev/null 2>&1; then echo VNC_STARTED; "
                "else echo VNC_START_FAIL; tail -n 20 /tmp/x11vnc.labka.log 2>/dev/null; fi; fi";
            ok = runRemote(labkaRemoteCommand(user, pass, start_script), out, errTxt);
            running = out.contains("VNC_STARTED") || out.contains("VNC_RUNNING");
            if (!running) QMessageBox::warning(this, TR("VNC remoto"), TR("No se pudo iniciar x11vnc.") + "\n\n" + out + "\n" + errTxt);
        }
    }

    launchSystemVNC(ip);
}

void LabkaWindow::removeDevice(const QString& ip) {
    if (QMessageBox::question(this, TR("Quitar dispositivo"),
            TR("¿Quitar %1 del mapa y de las credenciales guardadas?")
            .arg(map_widget_->maskIp(ip))) != QMessageBox::Yes) return;
        auth_.removeCredential(ip.toStdString());
        core_.removePeer(ip.toStdString());
        refreshUI();
}

void LabkaWindow::showPersonalizeDialog(const QString& ip) {
    QDialog dialog(this);
        dialog.setWindowTitle(TR("Personalizar nodo"));
        dialog.setWindowIcon(labkaIconAsset("assets/LOGO/Rubik.svg"));
        dialog.resize(380, 320);
        dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; border: 1px solid #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* lbl = new QLabel(TR("Icono para %1:").arg(map_widget_->maskIp(ip)), &dialog);
    lbl->setStyleSheet("color:#ffffff; font-weight:bold;");
    layout->addWidget(lbl);

    const QString current = map_widget_->nodeIconKey(ip);

    const QString base_style =
        "QPushButton { background-color: #1a1a2e; color: #ffffff; border: 1px solid #ffffff; "
        "padding: 8px; font-weight: bold; border-radius: 4px; text-align: left; }"
        "QPushButton:hover { background-color: #ffffff; color: #000; }";

    const QString active_style =
        "QPushButton { background-color: #2a2a3e; color: #ffffff; border: 2px solid #ffffff; "
        "padding: 8px; font-weight: bold; border-radius: 4px; text-align: left; }"
        "QPushButton:hover { background-color: #ffffff; color: #000; }";

    auto addOption = [&](
        const QString& key, 
        const QString& label, 
        const QString& asset) {

        const bool is_current = (key == current);
        QPushButton* b = new QPushButton((is_current ? "✓  " : "    ") + label, &dialog);
        b->setStyleSheet(is_current ? active_style : base_style);

        const QIcon ic = labkaIconAsset(asset);   // solo si existe en la carpeta
        if (!ic.isNull()) b->setIcon(ic);
        b->setCursor(Qt::PointingHandCursor);

        connect(b, &QPushButton::clicked, this, [this, &dialog, ip, key]() {
            map_widget_->setNodeIcon(ip, key);
            map_widget_->saveLayout();
            refreshUI();
            dialog.accept();
        });
        layout->addWidget(b);
    };

    addOption("pc",     TR("PC 1"),                 "assets/pc.svg");
    addOption("pc2",    TR("PC 2"),                 "assets/pc2.svg");
    addOption("laptop", TR("Laptop"),               "assets/laptop.svg");
    addOption("placa",  TR("Placa / Raspberry Pi"), "assets/placa.svg");
    addOption("server", TR("Servidor"),             "assets/server.svg");

    dialog.exec();
}

// Menú del nodo: texto plano (como antes) + separadores.
// Único icono usado: bash.svg, que sí existe en assets.
void LabkaWindow::showNodeMenu(
    const QString& ip, 
    bool is_device) {

    if (!is_device) return;
    const bool is_local = (ip == QString::fromStdString(core_.getMyIP()));

    QMenu menu(this);

    QAction* head = menu.addAction(map_widget_->maskIp(ip));
    head->setEnabled(false);
    menu.addSeparator();

    QAction* act_term = menu.addAction(TR("Abrir Terminal SSH (Bash)"));
    const QIcon bash_ic = labkaIconAsset("assets/bash.svg");
    if (!bash_ic.isNull()) act_term->setIcon(bash_ic);

    QAction* act_files = menu.addAction(TR("Explorar Archivos"));

    QAction* act_vnc = nullptr;
    if (!is_local) act_vnc = menu.addAction(TR("Control Remoto (VNC)"));

    menu.addSeparator();
    QAction* act_custom = menu.addAction(TR("Personalizar"));

    menu.addSeparator();
    QAction* act_remove = menu.addAction(TR("Quitar/Eliminar"));

    QAction* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected == act_custom) { showPersonalizeDialog(ip); return; }
    if (selected == act_remove) { removeDevice(ip); return; }

    QString user, pass;
    if (!getCredentials(ip, user, pass)) return;

    if (selected == act_term)                launchSystemTerminal(ip, user, pass, is_local);
    else if (selected == act_files)          openSystemFileExplorer(ip, user, is_local);
    else if (act_vnc && selected == act_vnc) launchVNCWithSetup(ip, user, pass);
}

void LabkaWindow::showAddDeviceDialog() {
    QDialog dialog(this);
        dialog.setWindowTitle(TR("Agregar Dispositivo"));
        dialog.setWindowIcon(labkaIconAsset("assets/LOGO/Rubik.svg"));
        dialog.resize(480, 420);
        dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; border: 1px solid #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    const QString input_style = "background-color: #000; color: #ffffff; border: 1px solid #ffffff; padding: 6px;";
    const QString label_style = "color: #ffffff; font-weight: bold;";

    QLabel* lbl_ip = new QLabel(TR("Dirección IP:"), &dialog);
        lbl_ip->setStyleSheet(label_style);
    QLineEdit* ip_input = new QLineEdit(&dialog);
        ip_input->setStyleSheet(input_style);
        ip_input->setPlaceholderText("192.168.1.42");
        layout->addWidget(lbl_ip);
        layout->addWidget(ip_input);

    QLabel* lbl_user = new QLabel(TR("Usuario SSH:"), &dialog);
        lbl_user->setStyleSheet(label_style);
    QLineEdit* user_input = new QLineEdit(&dialog);
        user_input->setStyleSheet(input_style);
        user_input->setPlaceholderText("root");
        layout->addWidget(lbl_user);
        layout->addWidget(user_input);

    QLabel* lbl_pass = new QLabel(TR("Contraseña SSH:"), &dialog);
        lbl_pass->setStyleSheet(label_style);
    QLineEdit* pass_input = new QLineEdit(&dialog);
        pass_input->setStyleSheet(input_style);
        pass_input->setEchoMode(QLineEdit::Password);
        layout->addWidget(lbl_pass);
        layout->addWidget(pass_input);

    QLabel* status = new QLabel("", &dialog);
        status->setStyleSheet("color: #ffcc00; font-size: 12px;");
        status->setWordWrap(true);
        layout->addWidget(status);

    QPushButton* btn_add = new QPushButton(TR("Verificar y Añadir"), &dialog);
        btn_add->setStyleSheet("background-color: #1a1a2e; color: #ffffff; border: 2px solid #ffffff; padding: 10px; font-weight: bold;");
        layout->addWidget(btn_add);

    connect(btn_add, &QPushButton::clicked,[this, &dialog, ip_input, user_input, pass_input, status]() {

        const QString ip   = ip_input->text().trimmed();
        QString user       = user_input->text().trimmed();
        const QString pass = pass_input->text();

        if (user.isEmpty()) user = "root";

        if (ip.isEmpty()) {
            QMessageBox::warning(&dialog, TR("Campos vacíos"), TR("La IP es obligatoria."));
            return;
        }

        const bool is_local = (ip == QString::fromStdString(core_.getMyIP()));

        if (is_local) {
            status->setText(TR("Configurando SSH/Firewall local (distro auto-detectada)..."));
            QApplication::processEvents();

            const QString local_script =
                labkaInstallPackage("openssh-server", "openssh-server", "openssh") +
                labkaOpenPort(22) +
                labkaEnableSSH();

            QProcess::startDetached("pkexec", QStringList() << "bash" << "-c" << local_script);
        } else {
            status->setText(TR("Verificando conexión SSH..."));
            QApplication::processEvents();
            auto client = core_.getSSHClient(ip.toStdString());
            std::string err;

        if (!client->connect(ip.toStdString(), user.toStdString(), pass.toStdString(), err)) {
                status->setText("");
                QMessageBox::critical(&dialog, TR("Conexión fallida"),
                QString::fromStdString(err) + "\n\n" + TR("No se guardaron las credenciales."));
                return;
            }

            status->setText(TR("Configurando SSH/Firewall remoto (distro auto-detectada)..."));
            QApplication::processEvents();
            autoConfigureRemoteSSH(ip, user, pass);
        }

        auth_.addCredential(ip.toStdString(), user.toStdString(), pass.toStdString());
        core_.addManualPeer(ip.toStdString(), user.toStdString());
        core_.setPeerCredentials(ip.toStdString(), user.toStdString(), pass.toStdString());

        refreshUI();
        dialog.accept();
    });

    dialog.exec();
}