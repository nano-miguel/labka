// src/ui/main_window.cpp
// Implementación de LabkaWindow (declarada en labka_ui.h). SIN header propio.
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
#include <QStyle>
#include <QSlider>
#include <QSpinBox>
#include <QDialog>
#include <QCloseEvent>
#include <QIcon>

// ==========================================
// HELPERS GENERALES
// ==========================================
static QString labkaShellQuote(const QString& value) {
    QString v = value;
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

static QString labkaRemoteCommand(const QString& user,
                                  const QString& pass,
                                  const QString& script) {
    QString bash = "bash -c " + labkaShellQuote(script) + " 2>&1";
    if (user.compare("root", Qt::CaseInsensitive) == 0) return bash;
    if (pass.isEmpty()) return "sudo -n " + bash;
    return "printf '%s\\n' " + labkaShellQuote(pass) + " | sudo -S -p '' " + bash;
}

// ==========================================
// ★ FALLBACK MULTI-DISTRO
// Debian/Ubuntu · Fedora/RHEL · Arch · openSUSE
// ==========================================
static QString labkaInstallPackage(const QString& debPkg,
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

// ==========================================
// CONSTRUCTOR
// ==========================================
LabkaWindow::LabkaWindow(LabkaCore& core, AuthManager& auth, QWidget* parent)
    : QMainWindow(parent), core_(core), auth_(auth) {

    setWindowTitle("Rubic-Labka | Network Command Center");
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

    // ★ Rueda de reconectar HASTA LA IZQUIERDA
    QIcon refreshIcon("assets/refresh.svg");
    if (refreshIcon.isNull()) refreshIcon = style()->standardIcon(QStyle::SP_BrowserReload);

    QPushButton* btn_refresh = new QPushButton(refreshIcon, "", this);
    btn_refresh->setFixedSize(28, 28);
    btn_refresh->setCursor(Qt::PointingHandCursor);
    btn_refresh->setToolTip("Actualizar / forzar reconexión ahora");
    btn_refresh->setStyleSheet(
        "QPushButton { background-color: transparent; border: 1px solid #ffffff; border-radius: 14px; }"
        "QPushButton:hover { background-color: #ffffff; }"
    );
    connect(btn_refresh, &QPushButton::clicked, this, [this]() {
        reconnectSavedPeers();
        checkInternet();
        refreshUI();
    });
    toolbar->addWidget(btn_refresh);

    const QString profile_name = QString::fromStdString(auth_.getCurrentProfile());

    QPushButton* btn_account = new QPushButton(profile_name.left(1).toUpper(), this);
    btn_account->setFixedSize(30, 30);
    btn_account->setCursor(Qt::PointingHandCursor);
    btn_account->setToolTip("Cuenta activa: " + profile_name);
    btn_account->setStyleSheet(
        "QPushButton { background-color: #00ffff; color: #000; border: none; border-radius: 15px; "
        "font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #ffffff; }"
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

    QPushButton* btn_add = new QPushButton("+ Agregar Dispositivo", this);
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

    // ★ Sondeo de internet cada 3 s (auto-actualiza, sin intervención)
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
        "QMenu::item:selected { background-color: #ffffff; color: #000; }"
    );
}

// ★ Sondeo robusto: bash /dev/tcp (sin depender de ping), con timeout y respaldo
void LabkaWindow::checkInternet() {
    if (ping_proc_) return;   // ya hay un sondeo en vuelo

    ping_proc_ = new QProcess(this);

    // Guard: si se queda colgado, lo matamos → finished() limpia y marca rojo
    QTimer* guard = new QTimer(ping_proc_);
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, this, [this]() {
        if (ping_proc_ && ping_proc_->state() != QProcess::NotRunning) {
            ping_proc_->kill();
        }
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

void LabkaWindow::showAccountMenu() {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a2e; color: #ffffff; border: 1px solid #ffffff; padding: 5px; }"
        "QMenu::item { padding: 8px 20px; }"
        "QMenu::item:selected { background-color: #ffffff; color: #000; }"
        "QMenu::item:disabled { color: #888888; }"
    );

    QAction* act_profile = menu.addAction("Perfil: " + QString::fromStdString(auth_.getCurrentProfile()));
    act_profile->setEnabled(false);

    menu.addSeparator();

    // ★ Auto conexión ARRIBA de Idiomas
    QAction* act_auto = menu.addAction("Auto conexión");
    act_auto->setCheckable(true);
    act_auto->setChecked(true);
    connect(act_auto, &QAction::toggled, this, [this](bool on) {
        core_.setAutoReconnect(on);
        refreshUI();
    });

    QMenu* lang = menu.addMenu("Idiomas");
    QAction* es = lang->addAction("Español");
    es->setCheckable(true);
    es->setChecked(true);
    QAction* en = lang->addAction("English");
    connect(en, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "Idiomas",
            "English translation is coming soon.\n"
            "La traducción al inglés estará disponible próximamente.");
    });

    QAction* act_config = menu.addAction("Configuración");

    menu.addSeparator();
    QAction* act_logout = menu.addAction("Cerrar sesión");
    QAction* act_exit = menu.addAction("Salir");

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
    dialog.setWindowTitle("Configuración");
    dialog.resize(420, 300);
    dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* lbl_trans = new QLabel("Transparencia de la ventana:", &dialog);
    lbl_trans->setStyleSheet("color:#ffffff; font-weight:bold;");
    layout->addWidget(lbl_trans);

    QSlider* slider_trans = new QSlider(Qt::Horizontal, &dialog);
    slider_trans->setRange(30, 100);
    slider_trans->setValue(static_cast<int>(windowOpacity() * 100));
    layout->addWidget(slider_trans);

    QLabel* lbl_size = new QLabel("Tamaño de la ventana:", &dialog);
    lbl_size->setStyleSheet("color:#ffffff; font-weight:bold; margin-top:10px;");
    layout->addWidget(lbl_size);

    QHBoxLayout* size_row = new QHBoxLayout();
    QSpinBox* spin_w = new QSpinBox(&dialog);
    spin_w->setRange(600, 3840);
    spin_w->setValue(width());
    spin_w->setSuffix(" px ancho");
    QSpinBox* spin_h = new QSpinBox(&dialog);
    spin_h->setRange(400, 2160);
    spin_h->setValue(height());
    spin_h->setSuffix(" px alto");
    size_row->addWidget(spin_w);
    size_row->addWidget(spin_h);
    layout->addLayout(size_row);

    QString spin_style = "QSpinBox { background:#000; color:#fff; border:1px solid #fff; padding:4px; }";
    spin_w->setStyleSheet(spin_style);
    spin_h->setStyleSheet(spin_style);

    connect(slider_trans, &QSlider::valueChanged, this, [this](int v) {
        setWindowOpacity(v / 100.0);
    });
    connect(spin_w, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, spin_h](int w) {
        resize(w, spin_h->value());
    });
    connect(spin_h, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, spin_w](int h) {
        resize(spin_w->value(), h);
    });

    QPushButton* btn_close = new QPushButton("Listo", &dialog);
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
    user = QInputDialog::getText(this, "SSH User", "Usuario SSH para " + ip + ":",
        QLineEdit::Normal, "", &ok);
    if (!ok) return false;
    if (user.trimmed().isEmpty()) user = "root";

    pass = QInputDialog::getText(this, "SSH Password",
        "Contraseña (deja en blanco si usas llave):", QLineEdit::Password, "", &ok);
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
    if (!success) QMessageBox::critical(this, "Error", "No se encontró un emulador de terminal compatible.");
}

void LabkaWindow::openSystemFileExplorer(const QString& ip, const QString& user, bool is_local) {
    if (is_local) { QProcess::startDetached("xdg-open", QStringList() << QDir::homePath()); return; }

    QString sftpUrl = QString("sftp://%1@%2").arg(user, ip);
    QProcess checkProcess;
    checkProcess.start("dpkg-query", QStringList() << "-W" << "-f=${Status}" << "gvfs-backends");
    checkProcess.waitForFinished();
    if (!checkProcess.readAllStandardOutput().contains("install ok installed")) {
        // En Fedora el backend SFTP suele venir con gvfs-fuse / gvfs-smb
        QMessageBox::warning(this, "Dependencia",
            "Para abrir SFTP en el explorador instala:\n"
            "  Debian/Ubuntu: gvfs-backends\n"
            "  Fedora: gvfs-fuse gvfs-smb\n"
            "  Arch: gvfs-smb gvfs-nfs");
        return;
    }

    bool success = QProcess::startDetached("xdg-open", QStringList() << sftpUrl);
    if (!success) success = QProcess::startDetached("gio", QStringList() << "open" << sftpUrl);
    if (success) { core_.markSessionActive(ip.toStdString(), SessionKind::Files); refreshUI(); }
    if (!success) QMessageBox::critical(this, "Error", "No se pudo abrir el explorador.");
}

void LabkaWindow::launchSystemVNC(const QString& ip) {
    bool success = false;
    if (QFile::exists("/usr/bin/remmina")) success = QProcess::startDetached("remmina", QStringList() << "-c" << ("vnc://" + ip));
    else if (QFile::exists("/usr/bin/vncviewer")) success = QProcess::startDetached("vncviewer", QStringList() << ip);
    else if (QFile::exists("/usr/bin/krdc")) success = QProcess::startDetached("krdc", QStringList() << ("vnc://" + ip));

    if (success) { core_.markSessionActive(ip.toStdString(), SessionKind::Vnc); refreshUI(); }
    if (!success) QMessageBox::information(this, "VNC no encontrado", "Instala remmina, tigervnc-viewer o krdc.");
}

// ★ Auto-setup SSH remoto con fallback multi-distro
void LabkaWindow::autoConfigureRemoteSSH(const QString& ip, const QString& user, const QString& pass) {
    auto client = core_.getSSHClient(ip.toStdString());
    if (!client || !client->isConnected()) return;

    QString script =
        labkaOpenPort(22) +
        labkaEnableSSH() +
        "echo LABKA_SSH_OK";

    std::string out, err;
    client->executeCommand(labkaRemoteCommand(user, pass, script).toStdString(), out, err);
}

void LabkaWindow::launchVNCWithSetup(const QString& ip, const QString& user, const QString& pass) {
    auto client = core_.getSSHClient(ip.toStdString());
    if (!client) { QMessageBox::critical(this, "VNC", "No se pudo obtener el cliente SSH."); return; }

    std::string err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!client->isConnected()) {
        if (!client->connect(ip.toStdString(), user.toStdString(), pass.toStdString(), err)) {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical(this, "SSH", "No se pudo conectar por SSH:\n\n" + QString::fromStdString(err));
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
        QMessageBox::critical(this, "VNC", "No se pudo comprobar VNC:\n\n" + errTxt);
        return;
    }

    bool installed = out.contains("VNC_INSTALLED");
    if (!installed) {
        if (QMessageBox::question(this, "VNC remoto",
                "No detecté x11vnc en " + ip + ".\n\n"
                "¿Instalarlo usando SSH?\n"
                "(Debian/Ubuntu, Fedora, Arch u openSUSE, se detecta solo)") != QMessageBox::Yes) return;

        QString install_script =
            labkaInstallPackage("x11vnc", "x11vnc", "x11vnc") +
            labkaOpenPort(5900) +
            "if command -v x11vnc >/dev/null 2>&1; then echo VNC_INSTALL_OK; else echo VNC_INSTALL_FAIL; fi";

        ok = runRemote(labkaRemoteCommand(user, pass, install_script), out, errTxt);
        installed = out.contains("VNC_INSTALL_OK");
        if (!installed) {
            QMessageBox::critical(this, "VNC", "No se pudo instalar x11vnc.\n\n" + out + "\n" + errTxt);
            return;
        }
    }

    ok = runRemote("pgrep -x x11vnc >/dev/null 2>&1 && echo VNC_RUNNING || echo VNC_STOPPED", out, errTxt);
    bool running = out.contains("VNC_RUNNING");

    if (!running) {
        if (QMessageBox::question(this, "VNC remoto",
                "x11vnc instalado pero no en ejecución.\n\n¿Iniciarlo en el puerto 5900?") == QMessageBox::Yes) {
            QString start_script =
                "if pgrep -x x11vnc >/dev/null 2>&1; then echo VNC_RUNNING; else export DISPLAY=:0; "
                "(x11vnc -display :0 -auth guess -forever -shared -nopw -rfbport 5900 >/tmp/x11vnc.labka.log 2>&1 &); "
                "sleep 1; if pgrep -x x11vnc >/dev/null 2>&1; then echo VNC_STARTED; "
                "else echo VNC_START_FAIL; tail -n 20 /tmp/x11vnc.labka.log 2>/dev/null; fi; fi";
            ok = runRemote(labkaRemoteCommand(user, pass, start_script), out, errTxt);
            running = out.contains("VNC_STARTED") || out.contains("VNC_RUNNING");
            if (!running) QMessageBox::warning(this, "VNC", "No se pudo iniciar x11vnc.\n\n" + out + "\n" + errTxt);
        }
    }

    launchSystemVNC(ip);
}

void LabkaWindow::removeDevice(const QString& ip) {
    if (QMessageBox::question(this, "Quitar dispositivo",
            "¿Quitar " + ip + " del mapa y de las credenciales guardadas?") != QMessageBox::Yes) return;
    auth_.removeCredential(ip.toStdString());
    core_.removePeer(ip.toStdString());
    refreshUI();
}

// ★ Personalizar: marca la opción actual con ✓ y borde cian
void LabkaWindow::showPersonalizeDialog(const QString& ip) {
    QDialog dialog(this);
    dialog.setWindowTitle("Personalizar nodo");
    dialog.resize(380, 300);
    dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; border: 1px solid #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* lbl = new QLabel("Icono para " + ip + ":", &dialog);
    lbl->setStyleSheet("color:#ffffff; font-weight:bold;");
    layout->addWidget(lbl);

    const QString current = map_widget_->nodeIconKey(ip);

    const QString base_style =
        "QPushButton { background-color: #1a1a2e; color: #ffffff; border: 1px solid #ffffff; "
        "padding: 8px; font-weight: bold; border-radius: 4px; text-align: left; }"
        "QPushButton:hover { background-color: #ffffff; color: #000; }";

    const QString active_style =
        "QPushButton { background-color: #10303a; color: #00ffff; border: 2px solid #00ffff; "
        "padding: 8px; font-weight: bold; border-radius: 4px; text-align: left; }"
        "QPushButton:hover { background-color: #00ffff; color: #000; }";

    auto addOption = [&](const QString& key, const QString& label, const QString& asset) {
        const bool is_current = (key == current);
        QPushButton* b = new QPushButton((is_current ? "✓  " : "    ") + label, &dialog);
        b->setStyleSheet(is_current ? active_style : base_style);
        QIcon ic(asset);
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

    addOption("pc",     "PC",                    "assets/pc.svg");
    addOption("laptop", "Laptop",                "assets/laptop.svg");
    addOption("placa",  "Placa / Raspberry Pi",  "assets/placa.svg");
    addOption("pc2",    "PC 2",                  "assets/pc2.svg");

    dialog.exec();
}

void LabkaWindow::showNodeMenu(const QString& ip, bool is_device) {
    if (!is_device) return;
    bool is_local = (ip == QString::fromStdString(core_.getMyIP()));

    QMenu menu(this);
    menu.addAction("Abrir Terminal SSH (Bash)");
    menu.addAction("Explorar Archivos");
    if (!is_local) menu.addAction("Control Remoto (VNC)");

    QAction* act_custom = menu.addAction("Personalizar");

    menu.addSeparator();
    QAction* act_remove = menu.addAction("Quitar/Eliminar");

    QAction* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected == act_custom) { showPersonalizeDialog(ip); return; }
    if (selected == act_remove) { removeDevice(ip); return; }

    QString user, pass;
    if (!getCredentials(ip, user, pass)) return;

    if (selected->text().startsWith("Abrir Terminal")) launchSystemTerminal(ip, user, pass, is_local);
    else if (selected->text().startsWith("Explorar Archivos")) openSystemFileExplorer(ip, user, is_local);
    else if (selected->text().startsWith("Control Remoto")) launchVNCWithSetup(ip, user, pass);
}

// ★ Agregar: auto-setup local con fallback multi-distro
void LabkaWindow::showAddDeviceDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Agregar Dispositivo");
    dialog.resize(480, 420);
    dialog.setStyleSheet("QDialog { background-color: #0a0a0f; color: #ffffff; border: 1px solid #ffffff; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    const QString input_style = "background-color: #000; color: #ffffff; border: 1px solid #ffffff; padding: 6px;";
    const QString label_style = "color: #ffffff; font-weight: bold;";

    QLabel* lbl_ip = new QLabel("Dirección IP:", &dialog);
    lbl_ip->setStyleSheet(label_style);
    QLineEdit* ip_input = new QLineEdit(&dialog);
    ip_input->setStyleSheet(input_style);
    ip_input->setPlaceholderText("192.168.1.42");
    layout->addWidget(lbl_ip);
    layout->addWidget(ip_input);

    QLabel* lbl_user = new QLabel("Usuario SSH:", &dialog);
    lbl_user->setStyleSheet(label_style);
    QLineEdit* user_input = new QLineEdit(&dialog);
    user_input->setStyleSheet(input_style);
    user_input->setPlaceholderText("root");
    layout->addWidget(lbl_user);
    layout->addWidget(user_input);

    QLabel* lbl_pass = new QLabel("Contraseña SSH:", &dialog);
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

    QPushButton* btn_add = new QPushButton("Verificar y Añadir", &dialog);
    btn_add->setStyleSheet("background-color: #1a1a2e; color: #ffffff; border: 2px solid #ffffff; padding: 10px; font-weight: bold;");
    layout->addWidget(btn_add);

    connect(btn_add, &QPushButton::clicked,
            [this, &dialog, ip_input, user_input, pass_input, status]() {

        const QString ip   = ip_input->text().trimmed();
        QString user       = user_input->text().trimmed();
        const QString pass = pass_input->text();

        if (user.isEmpty()) user = "root";

        if (ip.isEmpty()) {
            QMessageBox::warning(&dialog, "Campos vacíos", "La IP es obligatoria.");
            return;
        }

        const bool is_local = (ip == QString::fromStdString(core_.getMyIP()));

        if (is_local) {
            status->setText("Configurando SSH/Firewall local (distro auto-detectada)...");
            QApplication::processEvents();

            const QString local_script =
                labkaInstallPackage("openssh-server", "openssh-server", "openssh") +
                labkaOpenPort(22) +
                labkaEnableSSH();

            QProcess::startDetached("pkexec", QStringList() << "bash" << "-c" << local_script);
        } else {
            status->setText("Verificando conexión SSH...");
            QApplication::processEvents();

            auto client = core_.getSSHClient(ip.toStdString());
            std::string err;
            if (!client->connect(ip.toStdString(), user.toStdString(), pass.toStdString(), err)) {
                status->setText("");
                QMessageBox::critical(&dialog, "Conexión fallida",
                    QString::fromStdString(err) + "\n\nNo se guardaron las credenciales.");
                return;
            }

            status->setText("Configurando SSH/Firewall remoto (distro auto-detectada)...");
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