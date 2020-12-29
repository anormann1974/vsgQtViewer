#pragma once

#include <QWindow>

namespace vsg {
class Viewer;
class Window;
class StateGroup;
}

namespace vsgQt {
class Window;
}

namespace vsg {
class Instance;
}

class VulkanWindow : public QWindow
{
    Q_OBJECT
public:

    VulkanWindow();
    virtual ~VulkanWindow() override;

    void setClearColor(const QColor &color);
    bool loadFile(const QString &filename);

    vsg::Instance* instance();


protected:

    void render();

    void exposeEvent(QExposeEvent *e) override;
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    struct Private;
    QScopedPointer<Private> p;
};
