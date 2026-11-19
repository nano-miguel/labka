#ifndef LABKA_UI_H
#define LABKA_UI_H

#include <QMainWindow>
#include <QWidget>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QProcess>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QCloseEvent>
#include <QTransform>
#include <QPixmap>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>

#include <vector>
#include <map>
#include <string>

#include "../core/labka_core.h"
#include "../core/auth_manager.h"

// ==========================================
// i18n: contexto único "Labka"
// ==========================================
inline QString TR(const char* source) {
    return QCoreApplication::translate("Labka", source);
}

// ==========================================
// Resolución de assets: sirve desde build/ y desde /usr/share
// ==========================================
inline QString labkaAsset(const QString& rel) {
    if (QFile::exists(rel)) return QFileInfo(rel).absoluteFilePath();

    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList bases;
    bases << appDir
          << appDir + "/.."
          << appDir + "/../share/rubic-labka"
          << appDir + "/../../share/rubic-labka"
          << QDir::currentPath()
          << QStringLiteral("/usr/share/rubic-labka")
          << QStringLiteral("/usr/local/share/rubic-labka");

#ifdef LABKA_SOURCE_DIR
    bases << QString(LABKA_SOURCE_DIR);
#endif

    for (const QString& b : bases) {
        const QString p = b + "/" + rel;
        if (QFile::exists(p)) return QFileInfo(p).absoluteFilePath();
    }
    return rel;
}

// Icono SOLO si el asset existe (nada de iconos de Qt)
inline QIcon labkaIconAsset(const QString& rel) {
    const QString p = labkaAsset(rel);
    return QFile::exists(p) ? QIcon(p) : QIcon();
}

inline QPixmap labkaPixmapAsset(const QString& rel) {
    const QString p = labkaAsset(rel);
    return QFile::exists(p) ? QPixmap(p) : QPixmap();
}

// ==========================================
// LOGIN DIALOG
// ==========================================
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    LoginDialog(AuthManager* auth, QWidget* parent = nullptr);
    ~LoginDialog() = default;

private slots:
    void refreshProfiles();
    void onLogin();
    void onCreateProfile();

private:
    AuthManager* auth_;
    QListWidget* profile_list_;
    void applyDarkTheme();
};

// ==========================================
// NETWORK MAP
// ==========================================
class NetworkMap : public QWidget {
    Q_OBJECT
public:
    NetworkMap(QWidget* parent = nullptr);

    void updateNetwork(
        const std::string& my_ip,                      
        const std::string& my_hostname,                      
        const std::vector<PeerInfo>& peers,LabkaCore* core);

    void setInternetStatus(bool ok);

    void setNodeIcon(
        const QString& ip, 
        const QString& key);
    QString nodeIconKey(
        const QString& ip) const;

    void setIpPrivacy(int visible_octets);
    int  ipPrivacy() const { 
        return ip_mask_octets_; }
    QString maskIp(const QString& ip) const;

    void saveLayout();
    void loadLayout();

signals:
    void nodeClicked(const QString& ip, bool is_device);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::string my_ip_;
    std::string my_hostname_;
    std::vector<PeerInfo> peers_;
    LabkaCore* core_;

    bool internet_ok_ = false;
    int  ip_mask_octets_ = 0;

    struct VisualNode {
        QPointF pos;
        std::string ip;
        bool is_device;
    };

    std::vector<VisualNode> drawn_nodes_;

    QPixmap icon_pc_, icon_router_, icon_device_;
    QPixmap icon_laptop_, icon_placa_, icon_pc2_, icon_server_;

    std::map<std::string, std::string> node_icon_keys_;

    float zoom_;
    QPointF pan_offset_;
    QPoint last_mouse_pos_;

    bool is_panning_, is_dragging_node_;
    std::string dragged_node_ip_;

    std::map<std::string, QPointF> node_offsets_;

    QPushButton* btn_zoom_in_;
    QPushButton* btn_zoom_out_;

    QPixmap iconForIp(const std::string& ip) const;

    void drawIconPC(QPainter& painter, int x, int y, const QString& name,               
    const QString& ip, 
    const QPixmap& icon);

    void drawIconRouter(QPainter& painter, int x, int y, const QString& subnet);
    void drawIconDevice(QPainter& painter, int x, int y, const QString& name,
    const QString& sub, 
    bool is_connected, 
    const QPixmap& icon);

    std::string getSubnet(const std::string& ip);
};

// ==========================================
// LABKA WINDOW (declarada aquí; main_window.cpp implementa)
// ==========================================
class LabkaWindow : public QMainWindow {
    Q_OBJECT
public:
    LabkaWindow(
        LabkaCore& core, 
        AuthManager& auth, 
        QWidget* parent = nullptr);
    ~LabkaWindow() = default;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refreshUI();
    void showNodeMenu(const QString& ip, bool is_device);
    void showAddDeviceDialog();
    void showConfigDialog();
    void showAccountMenu();
    void quitApp();
    void reconnectSavedPeers();

private:
    LabkaCore& core_;
    AuthManager& auth_;

    QTimer* timer_;
    QTimer* net_timer_;
    QProcess* ping_proc_ = nullptr;

    NetworkMap* map_widget_;

    void applyDarkTheme();
    void checkInternet();
    void showPersonalizeDialog(const QString& ip);
    void applyLanguage(const QString& code);

    bool getCredentials(
        const QString& ip, 
        QString& user, 
        QString& pass);

    void launchSystemTerminal(
        const QString& ip, 
        const QString& user,                              
        const QString& pass, 
        bool is_local);
    void openSystemFileExplorer(
        const QString& ip, 
        const QString& user, 
        bool is_local);
    void launchSystemVNC(
        const QString& ip);
    void launchVNCWithSetup(
        const QString& ip, 
        const QString& user, 
        const QString& pass);
    void autoConfigureRemoteSSH(
        const QString& ip, 
        const QString& user, 
        const QString& pass);
    void removeDevice(
        const QString& ip);
};

#endif // LABKA_UI_H