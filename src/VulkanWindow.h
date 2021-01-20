#pragma once

#include <QWindow>


namespace vsg {
class Instance;
}

class VulkanWindow : public QWindow
{
    Q_OBJECT
    Q_PROPERTY(QColor clearColor READ clearColor WRITE setClearColor)
    Q_PROPERTY(vsg::Instance* instance READ instance)
public:

    VulkanWindow();
    virtual ~VulkanWindow() override;

    QColor clearColor() const;
    void setClearColor(const QColor &color);
    bool loadFile(const QString &filename);
    void clearScene();

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
