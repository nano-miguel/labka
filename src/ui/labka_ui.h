// src/ui/labka_ui.h
#ifndef LABKA_UI_H
#define LABKA_UI_H

#include <QMainWindow>
#include <QWidget>
#include <QTimer>
#include <QPushButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QProcess>
#include <QLineEdit>
#include <QDialog>
#include <vector>
#include <map>
#include "../core/labka_core.h"

// ==========================================
// NETWORK MAP (El Lienzo Visual)
// ==========================================
class NetworkMap : public QWidget {
    Q_OBJECT
  public:
    NetworkMap(QWidget* parent = nullptr);
    void updateNetwork(const std::string& my_ip, const std::string& my_hostname, const std::vector<PeerInfo>& peers, LabkaCore* core);
  signals:
    void nodeClicked(const QString& ip, bool is_device);
  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
  private:
    std::string my_ip_;
    std::string my_hostname_;
    std::vector<PeerInfo> peers_;
    LabkaCore* core_;
    
    struct VisualNode {
        QPointF pos;
        std::string ip;
        bool is_device;
    };
    std::vector<VisualNode> drawn_nodes_;

    void drawIconPC(QPainter& painter, int x, int y, const QString& name, const QString& ip);
    void drawIconRouter(QPainter& painter, int x, int y, const QString& subnet);
    void drawIconDevice(QPainter& painter, int x, int y, const QString& name, const QString& ip, bool is_connected);
    std::string getSubnet(const std::string& ip);
};

// ==========================================
// VENTANA PRINCIPAL
// ==========================================
class LabkaWindow : public QMainWindow {
    Q_OBJECT
  public:
    LabkaWindow(LabkaCore& core, QWidget* parent = nullptr);
    ~LabkaWindow() = default;
  private slots:
    void refreshUI();
    void showNodeMenu(const QString& ip, bool is_device);
    void showAddDeviceDialog();
  private:
    LabkaCore& core_;
    QTimer* timer_;
    NetworkMap* map_widget_;
    void applyDarkTheme();
    void launchSystemTerminal(const QString& ip);
    void openSystemFileExplorer(const QString& ip);
};

#endif // LABKA_UI_H