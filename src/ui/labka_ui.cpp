#include "labka_ui.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QSettings>
#include <QByteArray>
#include <QStringList>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==========================================
// LOGIN DIALOG
// ==========================================
LoginDialog::LoginDialog(AuthManager* auth, QWidget* parent)
    : QDialog(parent), auth_(auth) {

    setWindowTitle(QStringLiteral("Rubic-Labka | ") + TR("Iniciar Sesión"));
    setWindowIcon(labkaIconAsset("assets/LOGO/Rubik.svg"));
    resize(400, 500);
    applyDarkTheme();

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* title = new QLabel(TR("Selecciona un Perfil"), this);
    title->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold; margin-bottom: 10px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    profile_list_ = new QListWidget(this);
    profile_list_->setStyleSheet(
        "QListWidget { 
            background-color: #1a1a2e; color: #ffffff; 
            border: 1px solid #ffffff; padding: 5px; }"
        "QListWidget::item { padding: 10px; }"
        "QListWidget::item:selected { 
            background-color: #ffffff; color: #000; }"
    );
    layout->addWidget(profile_list_);

    QPushButton* btn_login = new QPushButton(TR("Entrar al Perfil"), this);
    btn_login->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #000; padding: 10px; font-weight: bold; border-radius: 4px; }"
    );
    connect(btn_login, &QPushButton::clicked, this, &LoginDialog::onLogin);
    layout->addWidget(btn_login);

    QPushButton* btn_create = new QPushButton(TR("+ Crear Nuevo Perfil"), this);
    btn_create->setStyleSheet(
        "QPushButton { 
            background-color: transparent; color: #ffffff; 
            border: 1px solid #ffffff; 
            padding: 10px; font-weight: bold; border-radius: 4px; }"
    );
    connect(btn_create, &QPushButton::clicked, 
        this, &LoginDialog::onCreateProfile);
    layout->addWidget(btn_create);
    connect(profile_list_, &QListWidget::itemDoubleClicked, 
        this, &LoginDialog::onLogin);

    refreshProfiles();
}

void LoginDialog::applyDarkTheme() {
    setStyleSheet("QDialog { 
        background-color: #0a0a0f; color: #ffffff; }");
}

void LoginDialog::refreshProfiles() {
    profile_list_->clear();
    auto profiles = auth_->getProfiles();
    for (const auto& p : profiles) profile_list_->addItem(QString::fromStdString(p));
    if (profile_list_->count() > 0) profile_list_->setCurrentRow(0);
}

void LoginDialog::onLogin() {
    if (profile_list_->count() == 0) 
        return;

    QString profile = profile_list_->currentItem()->text();
    bool ok;
    QString pass = QInputDialog::getText(
        this, 
        TR("Contraseña de Perfil"), 
        TR("Contraseña para '%1':").arg(profile), 
        QLineEdit::Password, "", &ok);

    if (!ok || pass.isEmpty()) 
        return;

    if (auth_->login(
        profile.toStdString(), 
        pass.toStdString())) 
        accept();
    else QMessageBox::critical(this, 
        TR("Error"), 
        TR("Contraseña incorrecta."));
}

void LoginDialog::onCreateProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, 
            TR("Nuevo Perfil"),
            TR("Nombre del nuevo perfil:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) 
        return;

    if (auth_->profileExists(name.toStdString())) {
        QMessageBox::warning(
        this, 
            TR("Error"),           
            TR("El perfil ya existe."));
        return;
    }

    QString pass = QInputDialog::getText(
        this, 
            TR("Nueva Contraseña"),            
            TR("Contraseña maestra:"), 
        QLineEdit::Password, "", &ok);

    if (!ok || pass.isEmpty()) 
        return;

    QString confirm = QInputDialog::getText(
        this, 
            TR("Confirmar"),           
            TR("Repite la contraseña:"), 
        QLineEdit::Password, "", &ok);

    if (!ok) return;

    if (pass != confirm) {
        QMessageBox::warning(
        this, 
            TR("Error"),            
            TR("Las contraseñas no coinciden."));
        return;
    }

    if (auth_->createProfile(
        name.toStdString(), 
        pass.toStdString())) 
        accept();

    else QMessageBox::critical(this, 
            TR("Error"), 
            TR("No se pudo crear el perfil."));
}

// ==========================================
// NETWORK MAP
// ==========================================
NetworkMap::NetworkMap(QWidget* parent)
    : QWidget(parent),
      core_(nullptr),
      zoom_(1.0f),
      is_panning_(false),
      is_dragging_node_(false) {

    setMinimumSize(600, 400);

    icon_pc_     = labkaPixmapAsset("assets/pc.svg");
    icon_router_ = labkaPixmapAsset("assets/router.svg");
    icon_device_ = labkaPixmapAsset("assets/bash.svg");
    icon_laptop_ = labkaPixmapAsset("assets/laptop.svg");
    icon_placa_  = labkaPixmapAsset("assets/placa.svg");
    icon_pc2_    = labkaPixmapAsset("assets/pc2.svg");
    icon_server_ = labkaPixmapAsset("assets/server.svg");

    setAttribute(Qt::WA_TranslucentBackground);

    btn_zoom_in_ = new QPushButton("+", this);
    btn_zoom_in_->setFixedSize(30, 30);
    btn_zoom_in_->setStyleSheet(
        "QPushButton { 
            background-color: rgba(26, 26, 46, 200); color: #ffffff; 
            border: 1px solid #ffffff; border-radius: 15px; font-weight: bold; font-size: 18px; }"
        "QPushButton:hover { 
            background-color: #ffffff; color: #000; }"
    );
    connect(btn_zoom_in_, &QPushButton::clicked, [this]() { zoom_ *= 1.2f; update(); });

    btn_zoom_out_ = new QPushButton("-", this);
    btn_zoom_out_->setFixedSize(30, 30);
    btn_zoom_out_->setStyleSheet(
        "QPushButton { 
            background-color: rgba(26, 26, 46, 200); color: #ffffff; 
            border: 1px solid #ffffff; border-radius: 15px; font-weight: bold; font-size: 20px; }"
        "QPushButton:hover { 
            background-color: #ffffff; color: #000; }"
    );
    connect(btn_zoom_out_, &QPushButton::clicked, [this]() { zoom_ *= 0.8f; update(); });

    loadLayout();

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "ui");
    ip_mask_octets_ = s.value("ip_mask_octets", 0).toInt();
    if (ip_mask_octets_ < 0) ip_mask_octets_ = 0;
    if (ip_mask_octets_ > 3) ip_mask_octets_ = 3;
}

void NetworkMap::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    int margin = 20;
    int x = width() - margin - btn_zoom_in_->width();
    int mid = height() / 2;

        btn_zoom_in_->move(x, mid - btn_zoom_in_->height() - 3);
        btn_zoom_out_->move(x, mid + 3);
}

void NetworkMap::setInternetStatus(bool ok) {
    if (internet_ok_ != ok) {
        internet_ok_ = ok;
        update();
    }
}

void NetworkMap::setNodeIcon(
    const QString& ip, 
    const QString& key) {

    node_icon_keys_[ip.toStdString()] = key.toStdString();
    update();
}

QString NetworkMap::nodeIconKey(const QString& ip) const {
    auto it = node_icon_keys_.find(ip.toStdString());
    if (it == node_icon_keys_.end()) 
    return QStringLiteral("pc");
    return QString::fromStdString(it->second);
}

void NetworkMap::setIpPrivacy(int visible_octets) {
    if (visible_octets < 0) visible_octets = 0;
    if (visible_octets > 3) visible_octets = 3;

    ip_mask_octets_ = visible_octets;

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "ui");
        s.setValue("ip_mask_octets", ip_mask_octets_);
        s.sync();

    update();
}

QString NetworkMap::maskIp(const QString& ip) const {
    if (ip_mask_octets_ <= 0) return ip;

    const QStringList parts = ip.split('.');
    if (parts.size() != 4) return ip;

    QStringList shown;
    for (int i = 0; i < ip_mask_octets_ && i < parts.size(); ++i) shown << parts.at(i);
    return shown.join('.') + ".…";
}

QPixmap NetworkMap::iconForIp(const std::string& ip) const {
    auto it = node_icon_keys_.find(ip);
    if (it != node_icon_keys_.end()) {
        if (it->second == "laptop" && !icon_laptop_.isNull()) return icon_laptop_;
        if (it->second == "placa"  && !icon_placa_.isNull())  return icon_placa_;
        if (it->second == "pc2"    && !icon_pc2_.isNull())    return icon_pc2_;
        if (it->second == "server" && !icon_server_.isNull()) return icon_server_;
    }
    return icon_pc_;
}

void NetworkMap::saveLayout() {
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Rubic-Labka", "layout");

    QStringList entries;
    for (const auto& [key, off] : node_offsets_) {
        QString icon;
        auto it = node_icon_keys_.find(key);
    if (it != node_icon_keys_.end()) icon = QString::fromStdString(it->second);

        QString b64 = QString::fromUtf8(QByteArray::fromStdString(key).toBase64());
        entries << b64 + "|" + QString::number(off.x()) + "|" + QString::number(off.y()) + "|" + icon;
    }

        s.setValue("layout/entries", entries);
        s.sync();
}

void NetworkMap::loadLayout() {
    QSettings s(
        QSettings::IniFormat,
        QSettings::UserScope,
         "Rubic-Labka", "layout");
    QStringList entries = s.value("layout/entries").toStringList();

    for (const QString& e : entries) {
        QStringList p = e.split('|');
    if (p.size() < 3) continue;

        std::string key = QString::fromUtf8(QByteArray::fromBase64(p[0].toUtf8())).toStdString();
    if (key.empty()) continue;

        node_offsets_[key] = QPointF(p[1].toDouble(), p[2].toDouble());
    if (p.size() >= 4 && !p[3].isEmpty()) node_icon_keys_[key] = p[3].toStdString();
    }
}

void NetworkMap::updateNetwork(
    const std::string& my_ip,                              
    const std::string& my_hostname,                               
    const std::vector<PeerInfo>& peers,                              
    LabkaCore* core) {
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

void NetworkMap::drawIconPC(QPainter& painter, int x, int y, 
    const QString& name,                          
    const QString& ip, 
    const QPixmap& icon) {
    if (!icon.isNull()) {
        painter.drawPixmap(x - 30, y - 30, 60, 60,
        icon.scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(255, 255, 255, 50));
        painter.drawRect(x - 30, y - 25, 60, 40);
        painter.drawRect(x - 10, y + 15, 20, 5);
    }

        painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(x - 120, y + 35, 240, 20), Qt::AlignCenter, name);
    font.setBold(false);
        painter.setFont(font);
        painter.drawText(QRect(x - 120, y + 55, 240, 20), Qt::AlignCenter, ip);
}

void NetworkMap::drawIconRouter(QPainter& painter, int x, int y, 
    const QString& subnet) {
    if (!icon_router_.isNull()) {
        painter.drawPixmap(x - 25, y - 25, 50, 50,
                           icon_router_.scaled(50, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(255, 255, 255, 50));
        painter.drawRect(x - 25, y - 15, 50, 30);
        painter.drawLine(x - 15, y - 15, x - 20, y - 35);
        painter.drawLine(x, y - 15, x, y - 35);
        painter.drawLine(x + 15, y - 15, x + 20, y - 35);
    }
    painter.setPen(Qt::white);
    painter.drawText(QRect(x - 100, y + 30, 200, 20), 
        Qt::AlignCenter, TR("Subred: %1").arg(subnet));
}

void NetworkMap::drawIconDevice(QPainter& painter, int x, int y, 
    const QString& name,                                
    const QString& sub, 
    bool is_connected, 
    const QPixmap& icon) {
    if (!icon.isNull()) {
        painter.drawPixmap(x - 25, y - 25, 50, 50,
         icon.scaled(50, 50, 
            Qt::KeepAspectRatio, 
            Qt::SmoothTransformation));
    } else {
        QColor color = is_connected ? QColor(0, 255, 0) : QColor(255, 90, 90);
        painter.setPen(QPen(color, 2));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 50));
        painter.drawEllipse(QPoint(x, y), 15, 15);
    }

        painter.setPen(Qt::white);
    QFont f = painter.font();
    f.setBold(true);
        painter.setFont(f);
        painter.drawText(QRect(x - 90, y + 30, 180, 20), 
            Qt::AlignCenter, name);
    f.setBold(false);
        painter.setFont(f);

        painter.setPen(is_connected ? QColor(0, 255, 120) : QColor(255, 130, 130));
        painter.drawText(QRect(x - 120, y + 46, 240, 20), 
            Qt::AlignCenter, sub);
        painter.setPen(Qt::white);
}

void NetworkMap::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(pan_offset_);
        painter.scale(zoom_, zoom_);

    auto drawLink = [&painter](       
    const QPointF& a, const QPointF& b,                              
    const QColor& color, 
    int width, 
    bool dashed) {

        QPen glow(color);
        glow.setWidth(width + 5);
        glow.setCapStyle(Qt::RoundCap);
        glow.setColor(QColor(
            color.red(), 
            color.green(), 
            color.blue(), 45));
    painter.setPen(glow);
    painter.drawLine(a, b);

        QPen pen(color);
        pen.setWidth(width);
        pen.setCapStyle(Qt::RoundCap);
    if (dashed) 
        pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.drawLine(a, b);
    };

    int w = width(), h = height();
    int cx = w / 2, cy = h / 2;

    drawn_nodes_.clear();

    QPointF pc_pos = QPointF(cx, cy) + node_offsets_[my_ip_];
    drawIconPC(painter, pc_pos.x(), pc_pos.y(),
        QString::fromStdString(my_hostname_),
    maskIp(QString::fromStdString(my_ip_)),
        iconForIp(my_ip_));
    drawn_nodes_.push_back({pc_pos, my_ip_, true});

    std::map<std::string, std::vector<PeerInfo>> subnet_groups;
    for (const auto& p : peers_) 
        subnet_groups[getSubnet(p.ip)].push_back(p);

    const std::string my_subnet = getSubnet(my_ip_);
    if (subnet_groups.find(my_subnet) == 
        subnet_groups.end())
        subnet_groups[my_subnet] = {};

    int num_routers = subnet_groups.size();
    int r_idx = 0;

    for (const auto& group : subnet_groups) {
        double angle = (2.0 * M_PI * r_idx) / (num_routers > 0 ? num_routers : 1);
        int rx = cx + 150 * cos(angle);
        int ry = cy + 150 * sin(angle);
        QPointF router_pos = QPointF(rx, ry) + node_offsets_[group.first];

        const bool is_my_subnet = (group.first == my_subnet);

    if (is_my_subnet && internet_ok_) drawLink(pc_pos, router_pos, QColor(0, 255, 90), 4, false);
    else if (is_my_subnet)            drawLink(pc_pos, router_pos, QColor(255, 60, 60), 3, true);
    else                              drawLink(pc_pos, router_pos, QColor(220, 220, 255), 2, true);

        QString subnet_label = maskIp(QString::fromStdString(group.first).section('/', 0, 0));
        drawIconRouter(painter, 
            router_pos.x(), 
            router_pos.y(), subnet_label);
        drawn_nodes_.push_back({router_pos, group.first, false});

        int num_devices = group.second.size();
    for (int d = 0; d < num_devices; d++) {
            const std::string dev_ip = group.second[d].ip;

            double dev_angle = angle + (d - (num_devices - 1) / 2.0) * 0.5;
            int dx = rx + 90 * cos(dev_angle);
            int dy = ry + 90 * sin(dev_angle);
            QPointF device_pos = QPointF(dx, dy) + node_offsets_[dev_ip];

            const bool active = core_ ? core_->isDeviceActive(dev_ip)     : false;
            const bool known  = core_ ? core_->hasPeerCredentials(dev_ip) : false;
            const int  tries  = core_ ? core_->reconnectAttempts(dev_ip)  : 0;

    if (active)     drawLink(router_pos, device_pos, QColor(0, 255, 90), 3, false);
    else if (known) drawLink(router_pos, device_pos, QColor(255, 60, 60), 3, true);
    else            drawLink(router_pos, device_pos, QColor(200, 200, 220), 2, true);

        QString sub = TR("SSH: %1").arg(QString::fromStdString(group.second[d].device_name));
    if (known && !active) sub += (tries > 0) ? TR(" · reconectando…") : TR(" · caído");

            drawIconDevice(painter, device_pos.x(), device_pos.y(),
            maskIp(QString::fromStdString(dev_ip)),sub, active, 
            iconForIp(dev_ip));

            drawn_nodes_.push_back({device_pos, dev_ip, true});
        }
        r_idx++;
    }
}

void NetworkMap::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        last_mouse_pos_ = event->pos();
        QTransform t;
        t.translate(
            pan_offset_.x(), 
            pan_offset_.y());
        t.scale(zoom_, zoom_);QPointF scene_pos = 
    
        t.inverted().map(QPointF(event->pos()));

    for (const auto& node : drawn_nodes_) {
        double tolerance = (node.is_device ? 25.0 : 35.0) / zoom_;
    if (std::hypot(
            scene_pos.x() - 
            node.pos.x(), 
            scene_pos.y() - 
            node.pos.y()) < tolerance) {

        is_dragging_node_ = true;
        dragged_node_ip_ = node.ip;
        return;
            }
        }
        is_panning_ = true;
    }
}

void NetworkMap::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - last_mouse_pos_;
    last_mouse_pos_ = event->pos();
    if (is_dragging_node_) {
        node_offsets_[dragged_node_ip_]
        += QPointF(delta.x() / zoom_, delta.y() / zoom_);
        update();
    } else if (is_panning_ && (event->buttons() & Qt::LeftButton)) {
        pan_offset_ += delta;
        update();
    }
}

void NetworkMap::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_panning_ = false;
        is_dragging_node_ = false;
    }
}

void NetworkMap::contextMenuEvent(QContextMenuEvent* event) {
    QTransform t;
        t.translate(pan_offset_.x(), pan_offset_.y());
        t.scale(zoom_, zoom_);
    QPointF scene_pos = t.inverted().map(QPointF(event->pos()));

    for (const auto& node : drawn_nodes_) {
        double tolerance = (node.is_device ? 25.0 : 35.0) / zoom_;
    if (std::hypot(scene_pos.x() - 
        node.pos.x(), 
        scene_pos.y() - 
        node.pos.y()) < tolerance) {

    if (node.is_device) {
        emit nodeClicked(QString::fromStdString(node.ip), true);
        event->accept();
        return;
            }
        }
    }
    event->ignore();
}

void NetworkMap::wheelEvent(QWheelEvent* event) {
    float factor = event->angleDelta().y() > 0 ? 1.1f : 0.9f;
    zoom_ *= factor;
    if (zoom_ < 0.2f) zoom_ = 0.2f;
    if (zoom_ > 5.0f) zoom_ = 5.0f;
    update();
}