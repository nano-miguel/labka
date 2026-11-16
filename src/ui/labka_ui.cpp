// src/ui/labka_ui.cpp
#include "labka_ui.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QTreeWidget>
#include <QClipboard>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==========================================
// NETWORK MAP
// ==========================================
NetworkMap::NetworkMap(QWidget* parent) : QWidget(parent), core_(nullptr) {
    setMinimumSize(600, 400);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(15, 15, 20));
    setAutoFillBackground(true);
    setPalette(pal);
}

void NetworkMap::updateNetwork(const std::string& my_ip, const std::string& my_hostname, const std::vector<PeerInfo>& peers, LabkaCore* core) {
    my_ip_ = my_ip;
    my_hostname_ = my_hostname;
    peers_ = peers;
    core_ = core;
    update();
}

std::string NetworkMap::getSubnet(const std::string& ip) {
    size_t last_dot = ip.find_last_of('.');
    return (last_dot != std::string::npos) ? ip.substr(0, last_dot) + ".0/24" : "Desconocida";
}

void NetworkMap::drawIconPC(QPainter& painter, int x, int y, const QString& name, const QString& ip) {
    painter.setPen(QPen(QColor(0, 255, 255), 2));
    painter.setBrush(QColor(0, 255, 255, 50));
    painter.drawRect(x - 30, y - 25, 60, 40);
    painter.drawRect(x - 10, y + 15, 20, 5);
    painter.setPen(QColor(255, 255, 255));
    QFont font = painter.font(); font.setBold(true); painter.setFont(font);
    painter.drawText(QRect(x - 120, y + 25, 240, 20), Qt::AlignCenter, name);
    font.setBold(false); painter.setFont(font);
    painter.drawText(QRect(x - 120, y + 45, 240, 20), Qt::AlignCenter, ip);
}

void NetworkMap::drawIconRouter(QPainter& painter, int x, int y, const QString& subnet) {
    painter.setPen(QPen(QColor(255, 165, 0), 2));
    painter.setBrush(QColor(255, 165, 0, 50));
    painter.drawRect(x - 25, y - 15, 50, 30);
    painter.drawLine(x - 15, y - 15, x - 20, y - 35);
    painter.drawLine(x, y - 15, x, y - 35);
    painter.drawLine(x + 15, y - 15, x + 20, y - 35);
    painter.setPen(QColor(255, 165, 0));
    painter.drawText(QRect(x - 100, y + 25, 200, 20), Qt::AlignCenter, "Subred: " + subnet);
}

void NetworkMap::drawIconDevice(QPainter& painter, int x, int y, const QString& name, const QString& ip, bool is_connected) {
    QColor color = is_connected ? QColor(0, 255, 0) : QColor(200, 200, 200);
    painter.setPen(QPen(color, 2));
    painter.setBrush(QColor(color.red(), color.green(), color.blue(), 50));
    painter.drawEllipse(QPoint(x, y), 15, 15);
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(QRect(x - 80, y + 20, 160, 20), Qt::AlignCenter, name);
    painter.drawText(QRect(x - 80, y + 35, 160, 20), Qt::AlignCenter, ip);
}

void NetworkMap::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width(), h = height();
    int cx = w / 2, cy = h / 2;
    drawn_nodes_.clear();

    drawIconPC(painter, cx, cy, QString::fromStdString(my_hostname_), QString::fromStdString(my_ip_));
    drawn_nodes_.push_back({QPointF(cx, cy), my_ip_, false});

    std::map<std::string, std::vector<PeerInfo>> subnet_groups;
    for (const auto& p : peers_) subnet_groups[getSubnet(p.ip)].push_back(p);

    int num_routers = subnet_groups.size();
    int r_idx = 0;
    
    for (const auto& group : subnet_groups) {
        double angle = (2.0 * M_PI * r_idx) / (num_routers > 0 ? num_routers : 1);
        int rx = cx + 150 * cos(angle);
        int ry = cy + 150 * sin(angle);

        painter.setPen(QPen(QColor(0, 255, 255, 150), 2, Qt::DashLine));
        painter.drawLine(cx, cy, rx, ry);

        drawIconRouter(painter, rx, ry, QString::fromStdString(group.first));
        drawn_nodes_.push_back({QPointF(rx, ry), group.first, false});

        int num_devices = group.second.size();
        for (int d = 0; d < num_devices; d++) {
            double dev_angle = angle + (d - (num_devices - 1) / 2.0) * 0.5; 
            int dx = rx + 90 * cos(dev_angle);
            int dy = ry + 90 * sin(dev_angle);

            bool is_connected = false;
            if (core_) {
                auto client = core_->getSSHClient(group.second[d].ip);
                is_connected = client && client->isConnected();
            }

            if (is_connected) {
                painter.setPen(QPen(QColor(0, 255, 0), 2));
            } else {
                painter.setPen(QPen(QColor(255, 165, 0, 100), 1, Qt::DashLine));
            }
            painter.drawLine(rx, ry, dx, dy);

            drawIconDevice(painter, dx, dy, QString::fromStdString(group.second[d].device_name), QString::fromStdString(group.second[d].ip), is_connected);
            drawn_nodes_.push_back({QPointF(dx, dy), group.second[d].ip, true});
        }
        r_idx++;
    }
}

void NetworkMap::mousePressEvent(QMouseEvent* event) {
    QPointF pos = event->pos();
    for (const auto& node : drawn_nodes_) {
        if (std::hypot(pos.x() - node.pos.x(), pos.y() - node.pos.y()) < (node.is_device ? 20.0 : 35.0)) {
            if (node.is_device) emit nodeClicked(QString::fromStdString(node.ip), true);
            return;
        }
    }
}

// ==========================================
// LABKA WINDOW
// ==========================================
LabkaWindow::LabkaWindow(LabkaCore& core, QWidget* parent) : QMainWindow(parent), core_(core) {
    setWindowTitle("Rubic-Labka | Network Command Center");
    resize(1000, 750);
    applyDarkTheme();

    QToolBar* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setStyleSheet("QToolBar { background-color: #0a0a0f; border-bottom: 1px solid #00ffff; padding: 5px; }");

    QPushButton* btn_add = new QPushButton("+ Agregar Dispositivo", this);
    btn_add->setStyleSheet("QPushButton { background-color: #1a1a2e; color: #00ffff; border: 1px solid #00ffff; padding: 8px 15px; font-weight: bold; } QPushButton:hover { background-color: #00ffff; color: #000; }");
    connect(btn_add, &QPushButton::clicked, this, &LabkaWindow::showAddDeviceDialog);

    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(btn_add);
    toolbar->addWidget(spacer);

    QWidget* central = new QWidget(this);
    QVBoxLayout* main_layout = new QVBoxLayout(central);
    main_layout->setContentsMargins(0, 0, 0, 0);

    map_widget_ = new NetworkMap(this);
    main_layout->addWidget(map_widget_);
    setCentralWidget(central);

    connect(map_widget_, &NetworkMap::nodeClicked, this, &LabkaWindow::showNodeMenu);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &LabkaWindow::refreshUI);
    timer_->start(1000);
}

void LabkaWindow::applyDarkTheme() {
    setStyleSheet("QMainWindow { background-color: #0a0a0f; } QMenu { background-color: #1a1a2e; color: #00ffff; } QMenu::item:selected { background-color: #00ffff; color: #000; }");
}

void LabkaWindow::refreshUI() {
    auto peers = core_.getPeers();
    map_widget_->updateNetwork(core_.getMyIP(), core_.getMyHostname(), peers, &core_);
}

void LabkaWindow::launchSystemTerminal(const QString& ip) {
    bool ok;
    QString user = QInputDialog::getText(this, "SSH User", "Usuario SSH para " + ip + ":", QLineEdit::Normal, "root", &ok);
    if (!ok || user.isEmpty()) return;

    QString script = QString("ssh %1@%2; echo ''; read -p 'Presiona Enter para cerrar...'").arg(user, ip);
    
    QStringList args;
    args << "-e" << "bash" << "-c" << script;

    bool success = QProcess::startDetached("x-terminal-emulator", args);
    if (!success) success = QProcess::startDetached("gnome-terminal", QStringList() << "--" << "bash" << "-c" << script);
    if (!success) success = QProcess::startDetached("konsole", args);
    if (!success) success = QProcess::startDetached("xterm", args);

    if (!success) {
        QMessageBox::critical(this, "Error", "No se encontró un emulador de terminal compatible.");
    }
}

void LabkaWindow::openSystemFileExplorer(const QString& ip) {
    bool ok;
    QString user = QInputDialog::getText(this, "SSH User", "Usuario SSH para " + ip + ":", QLineEdit::Normal, "root", &ok);
    if (!ok || user.isEmpty()) return;

    QString sftpUrl = QString("sftp://%1@%2").arg(user, ip);

    QProcess checkProcess;
    checkProcess.start("dpkg-query", QStringList() << "-W" << "-f=${Status}" << "gvfs-backends");
    checkProcess.waitForFinished();
    QString status = checkProcess.readAllStandardOutput();

    if (!status.contains("install ok installed")) {
        QMessageBox::warning(this, "Dependencia faltante", 
            "Para abrir carpetas remotas, tu sistema necesita el paquete 'gvfs-backends' y 'gvfs-fuse'.\n\n"
            "Por favor, ejecuta en una terminal:\n"
            "sudo apt install gvfs-backends gvfs-fuse");
        return;
    }

    bool success = QProcess::startDetached("xdg-open", QStringList() << sftpUrl);
    if (!success) {
        success = QProcess::startDetached("gio", QStringList() << "open" << sftpUrl);
    }

    if (!success) {
        QMessageBox::critical(this, "Error", "No se pudo abrir el explorador de archivos.\n"
                                             "Asegúrate de tener instalado 'xdg-utils' y 'gvfs-backends'.");
    }
}

void LabkaWindow::showNodeMenu(const QString& ip, bool is_device) {
    if (!is_device) return;

    QMenu menu(this);
    menu.addAction("Abrir Terminal SSH (Sistema)");
    menu.addAction("Explorar Archivos (Nativo)");

    QAction* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected->text().startsWith("Abrir Terminal")) {
        launchSystemTerminal(ip);
    } else if (selected->text().startsWith("Explorar Archivos")) {
        openSystemFileExplorer(ip);
    }
}

void LabkaWindow::showAddDeviceDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Agregar Dispositivo");
    dialog.resize(550, 350);
    dialog.setStyleSheet("background-color: #0a0a0f; color: #00ffff;");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel("Ingresa la IP de la otra PC (ej. 192.168.1.50):", &dialog);
    QLineEdit* ip_input = new QLineEdit(&dialog);
    ip_input->setStyleSheet("background-color: #000; color: #00ff00; border: 1px solid #00ff00; padding: 5px;");

    layout->addWidget(label);
    layout->addWidget(ip_input);

    QGroupBox* fw_group = new QGroupBox("Configuración del Firewall y SSH", &dialog);
    fw_group->setStyleSheet("QGroupBox { color: #ff00ff; font-weight: bold; border: 1px solid #ff00ff; }");
    QVBoxLayout* fw_layout = new QVBoxLayout(fw_group);
    
    QLabel* fw_text = new QLabel("Si no puedes conectar, presiona el botón para abrir el puerto 22, instalar y arrancar SSH en ESTA PC:", fw_group);
    fw_text->setWordWrap(true);
    
    QPushButton* btn_fix = new QPushButton("Abrir Puerto e Iniciar SSH", fw_group);
    btn_fix->setStyleSheet("background-color: #1a1a2e; color: #ff00ff; border: 1px solid #ff00ff; padding: 8px; font-weight: bold;");
    
    connect(btn_fix, &QPushButton::clicked, [this]() {
        QString script = "ufw allow 22/tcp; apt-get install -y openssh-server; systemctl start ssh; systemctl enable ssh;";
        QProcess::startDetached("pkexec", QStringList() << "bash" << "-c" << script);
        QMessageBox::information(this, "Firewall", "Ejecutando configuración. Ingresa tu contraseña de administrador en la ventana que aparecerá.");
    });

    fw_layout->addWidget(fw_text);
    fw_layout->addWidget(btn_fix);
    layout->addWidget(fw_group);

    QPushButton* btn_add = new QPushButton("Añadir al Mapa", &dialog);
    btn_add->setStyleSheet("background-color: #1a1a2e; color: #00ffff; border: 2px solid #00ffff; padding: 10px; font-weight: bold;");
    layout->addWidget(btn_add);

    connect(btn_add, &QPushButton::clicked, [this, ip_input, &dialog]() {
        QString ip = ip_input->text();
        if (ip.isEmpty()) return;
        core_.addManualPeer(ip.toStdString(), "Manual_Device");
        refreshUI();
        dialog.accept();
    });

    dialog.exec();
}