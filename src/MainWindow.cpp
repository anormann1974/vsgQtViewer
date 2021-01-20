#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "VulkanWindow.h"

#include <QFileDialog>
#include <QColorDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    auto window = new VulkanWindow();
    window->setClearColor(Qt::red);

    auto widget = QWidget::createWindowContainer(window, this);
    setCentralWidget(widget);

    connect(ui->actionOpen, &QAction::triggered, this, [=]() {
        if (const auto filename = QFileDialog::getOpenFileName(this, tr("Open file"), nullptr, "Supported files (*.vsgt *.fbx *.gltf *.glb *.obj);;All files (*.*)"); !filename.isEmpty())
            window->loadFile(filename);
    });

    connect(ui->actionClearColor, &QAction::triggered, this, [=]() {
        if (const auto color = QColorDialog::getColor(window->clearColor(), this, tr("Choose background color")); color.isValid())
        {
            window->setClearColor(color);
        }
    });

    connect(ui->actionNew, &QAction::triggered, window, &VulkanWindow::clearScene);
}

MainWindow::~MainWindow()
{
    delete ui;
}

