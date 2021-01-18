#include "VulkanWindow.h"
#include "ReaderWriterAssimp.h"

#include <vulkan/vulkan.h>

#ifdef Q_OS_LINUX
#undef None
#undef Status
#undef Bool
#undef CursorShape
#endif

#include <QVulkanInstance>
#include <QPlatformSurfaceEvent>
#include <QThread>
#include <QCoreApplication>
#include <QLoggingCategory>

#include <vsg/viewer/Viewer.h>
#include <vsg/ui/KeyEvent.h>
#include <vsg/ui/ScrollWheelEvent.h>
#include <vsg/ui/PointerEvent.h>
#include <vsg/io/read.h>
#include <vsg/traversals/ComputeBounds.h>
#include <vsg/viewer/Trackball.h>
#include <vsg/viewer/RenderGraph.h>
#include <vsg/viewer/View.h>
#include <vsg/commands/ClearAttachments.h>

namespace {
static QLoggingCategory lc("VulkanWindow");
}

namespace vsgQt {

static vsg::ref_ptr<vsg::Data> createWhiteTexture()
{
    auto vsg_data = vsg::vec4Array2D::create(1,1, vsg::Data::Layout{VK_FORMAT_R32G32B32A32_SFLOAT});
    std::fill(vsg_data->begin(), vsg_data->end(), vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    return vsg_data;
}

class Surface : public vsg::Inherit<vsg::Surface, Surface>
{
public:

    Surface(QWindow *window, vsg::Instance *instance)
        : Inherit(nullptr, instance)
    {
        _surface = QVulkanInstance::surfaceForWindow(window);
    }

    virtual ~Surface()
    {
        // Prevent vsg::Surface destructor from calling vkDestroySurfaceKHR on this surface. Qt is owning the surface.
        _surface = nullptr;
    }
};

class Window : public vsg::Inherit<vsg::Window, Window>
{
public:

    Window(VulkanWindow *win, vsg::ref_ptr<vsg::WindowTraits> traits)
        : Inherit(traits)
        , _window(win)
    {
        _instance = win->instance();

        if (traits->shareWindow)
        {
            share(*traits->shareWindow);
        }

        const auto size = win->size();
        _extent2D.width = size.width();
        _extent2D.height = size.height();

        traits->nativeWindow = win;
    }

    Window() = delete;
    Window(const Window &) = delete;
    Window operator=(const Window &) = delete;

    bool visible() const override { return _window != nullptr; }
    bool valid() const override { return _window != nullptr; }

    bool pollEvents(vsg::UIEvents &events) override
    {
        if (bufferedEvents.size() > 0)
        {
            events.splice(events.end(), bufferedEvents);
            bufferedEvents.clear();
            return true;
        }

        return false;
    }

    bool resized() const override
    {
        const auto width = _window->width();
        const auto height = _window->height();
        return width != int(_extent2D.width) || height != int(_extent2D.height);
    }

    void resize() override
    {
        const auto width = _window->width();
        const auto height = _window->height();

        _extent2D.width = width;
        _extent2D.height = height;

        buildSwapchain();
    }

    virtual const char* instanceExtensionSurfaceName() const override
    {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
#endif
    }


    vsg::UIEvents bufferedEvents;

protected:

    virtual void _initSurface() override
    {
        //qCDebug(lc) << __func__ << _instance;

        if (!_instance)
            _initInstance();

        _surface = new Surface(_window, _instance);
    }

    virtual ~Window() override
    {
        clear();
    }

    VulkanWindow *_window = nullptr;
};

class KeyboardMap
{
public:
    KeyboardMap();

    bool getKeySymbol(const QKeyEvent *e, vsg::KeySymbol &keySymbol, vsg::KeySymbol &modifiedKeySymbol, vsg::KeyModifier &keyModifier);

protected:
    using VirtualKeyToKeySymbolMap = std::map<uint16_t, vsg::KeySymbol>;
    VirtualKeyToKeySymbolMap _keycodeMap;
};

}

struct VulkanWindow::Private
{
    QVulkanInstance instance;
    bool initialized{false};
    vsg::ref_ptr<vsg::Instance> vsgInstance;
    vsg::ref_ptr<vsg::Viewer> viewer{vsg::Viewer::create()};
    vsg::ref_ptr<vsg::Group> scenegraph{vsg::Group::create()};
    vsg::ref_ptr<vsg::Group> modelRoot{vsg::Group::create()};
    vsg::ref_ptr<vsgQt::Window> window;
    vsg::ref_ptr<vsg::Camera> camera;
    vsg::ref_ptr<vsg::ViewportState> viewport;
    vsg::ref_ptr<vsg::CommandGraph> commandGraph;
    vsg::ref_ptr<vsg::RenderGraph> renderGraph;
    VkClearColorValue clearColor;
    vsgQt::KeyboardMap keyboard;
};

VulkanWindow::VulkanWindow()
    : QWindow()
    , p(new Private)
{   
    setSurfaceType(VulkanSurface);

    p->scenegraph->addChild(p->modelRoot);
}

QColor VulkanWindow::clearColor() const
{
    if (p->window.valid())
    {
        const auto clear = p->window->clearColor();
        return QColor::fromRgbF(clear.float32[0], clear.float32[1], clear.float32[2], clear.float32[3]);
    }

    return {};
}

VulkanWindow::~VulkanWindow() = default;

void VulkanWindow::exposeEvent(QExposeEvent *e)
{
    if (isExposed())
    {
        if (!p->initialized)
        {
            p->initialized = true;

            const auto rect = e->region().boundingRect();
            const uint32_t width = static_cast<uint32_t>(rect.width());
            const uint32_t height = static_cast<uint32_t>(rect.height());

            auto windowTraits = vsg::WindowTraits::create();
            windowTraits->debugLayer = false;
            windowTraits->apiDumpLayer = false;
            windowTraits->windowTitle = "vsgQtViewer";
            windowTraits->width = width;
            windowTraits->height = height;
            windowTraits->fullscreen = false;
            windowTraits->samples = 4;

            p->window = new vsgQt::Window(this, windowTraits);

            vsg::Names instanceExtensions;
            instanceExtensions.push_back("VK_KHR_surface");
            instanceExtensions.push_back(p->window->instanceExtensionSurfaceName());
            //instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

            vsg::Names requestedLayers;
            requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
//            requestedLayers.push_back("VK_LAYER_LUNARG_api_dump");

            vsg::Names validatedNames = vsg::validateInstancelayerNames(requestedLayers);

            p->vsgInstance = vsg::Instance::create(instanceExtensions, validatedNames);

            p->instance.setVkInstance(p->vsgInstance->getInstance());
            if (p->instance.create())
            {
                qCDebug(lc) << __func__<< "success.";
                setVulkanInstance(&p->instance);

                p->viewer->addWindow(p->window);

#if 1
                vsg::Paths searchPaths = vsg::getEnvPaths("VSG_FILE_PATH");
                //vsg::Path filename = vsg::findFile("models/TestScene.fbx", searchPaths);
                //vsg::Path filename = vsg::findFile("models/teapot.vsgt", searchPaths);
                //vsg::Path filename = "C:/Users/andre/Documents/Assest/Meshes/DamagedHelmet/glTF/DamagedHelmet.gltf";
                vsg::Path filename = "C:/Users/andre/Documents/Assest/Meshes/DamagedHelmet/glTF-Binary/DamagedHelmet.glb";
                //vsg::Path filename = "C:/Users/andre/Documents/Assest/Meshes/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";

                auto opts = vsg::Options::create();
                opts->readerWriter = ReaderWriterAssimp::create();

                if (auto node = vsg::read_cast<vsg::Node>(filename, opts); node.valid())
                {
                    qCDebug(lc) << "Adding node to scene" << filename.c_str();
                    p->modelRoot->addChild(node);
                }
#endif

                // compute the bounds of the scene graph to help position camera
                vsg::ComputeBounds computeBounds;
                p->scenegraph->accept(computeBounds);
                vsg::dvec3 centre = (computeBounds.bounds.min+computeBounds.bounds.max)*0.5;
                double radius = vsg::length(computeBounds.bounds.max-computeBounds.bounds.min)*0.6;
                double nearFarRatio = 0.001;

                // set up the camera
                auto lookAt = vsg::LookAt::create(centre+vsg::dvec3(0.0, -radius*3.5, 0.0), centre, vsg::dvec3(0.0, 0.0, 1.0));

                vsg::ref_ptr<vsg::ProjectionMatrix> perspective;
                if (vsg::ref_ptr<vsg::EllipsoidModel> ellipsoidModel(p->scenegraph->getObject<vsg::EllipsoidModel>("EllipsoidModel")); ellipsoidModel)
                {
                    perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(p->window->extent2D().width) / static_cast<double>(p->window->extent2D().height), nearFarRatio, 0.0);
                }
                else
                {
                    perspective = vsg::Perspective::create(30.0, static_cast<double>(p->window->extent2D().width) / static_cast<double>(p->window->extent2D().height), nearFarRatio*radius, radius * 45);
                }

                p->viewport = vsg::ViewportState::create(p->window->extent2D());
                p->camera = vsg::Camera::create(perspective, lookAt, p->viewport);

                //p->window->clearColor() = p->clearColor;

                p->commandGraph = vsg::CommandGraph::create(p->window);
                p->renderGraph = vsg::RenderGraph::create(p->window);
                p->commandGraph->addChild(p->renderGraph);

                auto view = vsg::View::create(p->camera, p->scenegraph);
                p->renderGraph->addChild(view);

                p->viewer->addEventHandler(vsg::Trackball::create(p->camera));

                p->viewer->assignRecordAndSubmitTaskAndPresentation({p->commandGraph});
                p->viewer->compile();

                vsg::clock::time_point event_time = vsg::clock::now();
                p->window->bufferedEvents.emplace_back(new vsg::ExposeWindowEvent(p->window, event_time, rect.x(), rect.y(), width, height));
            }

            render();
        }
    }
}

bool VulkanWindow::event(QEvent *e)
{
    switch (e->type())
    {
    case QEvent::UpdateRequest:
        render();
        break;

    default:
        break;
    }

    return QWindow::event(e);
}

void VulkanWindow::keyPressEvent(QKeyEvent *e)
{
    vsg::KeySymbol keySymbol, modifiedKeySymbol;
    vsg::KeyModifier keyModifier;

    //    if (e->key() == Qt::Key_Escape)
    //        QCoreApplication::exit(0);

    if (p->keyboard.getKeySymbol(e, keySymbol, modifiedKeySymbol, keyModifier))
    {
        vsg::clock::time_point event_time = vsg::clock::now();
        p->window->bufferedEvents.emplace_back(new vsg::KeyPressEvent(p->window, event_time, keySymbol, modifiedKeySymbol, keyModifier));
    }
}

void VulkanWindow::keyReleaseEvent(QKeyEvent *e)
{
    vsg::KeySymbol keySymbol, modifiedKeySymbol;
    vsg::KeyModifier keyModifier;

    if (p->keyboard.getKeySymbol(e, keySymbol, modifiedKeySymbol, keyModifier))
    {
        vsg::clock::time_point event_time = vsg::clock::now();
        p->window->bufferedEvents.emplace_back(new vsg::KeyReleaseEvent(p->window, event_time, keySymbol, modifiedKeySymbol, keyModifier));
    }
}

void VulkanWindow::mouseMoveEvent(QMouseEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    int button = 0;
    switch (e->buttons())
    {
    case Qt::LeftButton: button |= vsg::BUTTON_MASK_1; break;
    case Qt::RightButton: button |= vsg::BUTTON_MASK_3; break;
    case Qt::MiddleButton: button |= vsg::BUTTON_MASK_2; break;
    case Qt::NoButton: button = 0; break;
    default: button = 0; break;
    }

    p->window->bufferedEvents.emplace_back(new vsg::MoveEvent(p->window, event_time, e->x(), e->y(), (vsg::ButtonMask)button));
}

void VulkanWindow::mousePressEvent(QMouseEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    int button = 0;
    switch (e->buttons())
    {
    case Qt::LeftButton: button |= vsg::BUTTON_MASK_1; break;
    case Qt::RightButton: button |= vsg::BUTTON_MASK_3; break;
    case Qt::MiddleButton: button |= vsg::BUTTON_MASK_2; break;
    case Qt::NoButton: button = 0; break;
    default: button = 0; break;
    }

    p->window->bufferedEvents.emplace_back(new vsg::ButtonPressEvent(p->window, event_time, e->x(), e->y(), (vsg::ButtonMask)button, 0));
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    int button = 0;
    switch (e->buttons())
    {
    case Qt::LeftButton: button |= vsg::BUTTON_MASK_1; break;
    case Qt::RightButton: button |= vsg::BUTTON_MASK_3; break;
    case Qt::MiddleButton: button |= vsg::BUTTON_MASK_2; break;
    case Qt::NoButton: button = 0; break;
    default: button = 0; break;
    }

    p->window->bufferedEvents.emplace_back(new vsg::ButtonReleaseEvent(p->window, event_time, e->x(), e->y(), (vsg::ButtonMask)button, 0));
}

void VulkanWindow::resizeEvent(QResizeEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    if (p->initialized)
        p->window->bufferedEvents.emplace_back(new vsg::ConfigureWindowEvent(p->window, event_time, x(), y(), static_cast<uint32_t>(e->size().width()), static_cast<uint32_t>(e->size().height())));
}

void VulkanWindow::moveEvent(QMoveEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    if (p->initialized)
        p->window->bufferedEvents.emplace_back(new vsg::ConfigureWindowEvent(p->window, event_time, e->pos().x(), e->pos().y(), static_cast<uint32_t>(size().width()), static_cast<uint32_t>(size().height())));
}

void VulkanWindow::wheelEvent(QWheelEvent *e)
{
    vsg::clock::time_point event_time = vsg::clock::now();

    p->window->bufferedEvents.emplace_back(new vsg::ScrollWheelEvent(p->window, event_time, e->angleDelta().y() < 0 ? vsg::vec3(0.0f, -1.0f, 0.0f) : vsg::vec3(0.0f, 1.0f, 0.0f)));
}

void VulkanWindow::setClearColor(const QColor &color)
{
    VkClearColorValue clearColor = {
        {
            (float)color.redF(),
            (float)color.greenF(),
            (float)color.blueF(),
            (float)color.alphaF()
        }
    };

    p->clearColor = clearColor;

    if (p->initialized)
    {
        //p->viewer->compile();
        p->window->clearColor() = clearColor;

        p->commandGraph->getChildren().clear();
        p->commandGraph->addChild(vsg::createRenderGraphForView(p->window, p->camera, p->scenegraph));
    }
}

bool VulkanWindow::loadFile(const QString &filename)
{
    auto opts = vsg::Options::create();
    opts->readerWriter = ReaderWriterAssimp::create();

    if (auto node = vsg::read_cast<vsg::Node>(filename.toStdString(), opts); node.valid())
    {
        qCDebug(lc) << "Adding node to scene" << filename;

//        auto root = vsg::StateGroup::create();
//        p->modelRoot->addChild(root);

        p->modelRoot->addChild(node);
        p->viewer->compile();
        return true;
    }

    return false;
}

vsg::Instance *VulkanWindow::instance()
{
    return p->vsgInstance;
}

void VulkanWindow::render()
{
    if (p->viewer->advanceToNextFrame())
    {
        p->viewer->handleEvents();
        p->viewer->update();
        p->viewer->recordAndSubmit();
        p->viewer->present();
    }

    requestUpdate();
}

vsgQt::KeyboardMap::KeyboardMap()
    : _keycodeMap{
        {0x0, vsg::KEY_Undefined},
        {Qt::Key_Space, vsg::KEY_Space},

        {'0', vsg::KEY_0},
        {'1', vsg::KEY_1},
        {'2', vsg::KEY_2},
        {'3', vsg::KEY_3},
        {'4', vsg::KEY_4},
        {'5', vsg::KEY_5},
        {'6', vsg::KEY_6},
        {'7', vsg::KEY_7},
        {'8', vsg::KEY_8},
        {'9', vsg::KEY_9},

        {'a', vsg::KEY_a},
        {'b', vsg::KEY_b},
        {'c', vsg::KEY_c},
        {'d', vsg::KEY_d},
        {'e', vsg::KEY_e},
        {'f', vsg::KEY_f},
        {'g', vsg::KEY_g},
        {'h', vsg::KEY_h},
        {'i', vsg::KEY_i},
        {'j', vsg::KEY_j},
        {'k', vsg::KEY_k},
        {'l', vsg::KEY_l},
        {'m', vsg::KEY_m},
        {'n', vsg::KEY_n},
        {'o', vsg::KEY_o},
        {'p', vsg::KEY_p},
        {'q', vsg::KEY_q},
        {'r', vsg::KEY_r},
        {'s', vsg::KEY_s},
        {'t', vsg::KEY_t},
        {'u', vsg::KEY_u},
        {'z', vsg::KEY_v},
        {'w', vsg::KEY_w},
        {'x', vsg::KEY_x},
        {'y', vsg::KEY_y},
        {'z', vsg::KEY_z},

        {'A', vsg::KEY_A},
        {'B', vsg::KEY_B},
        {'C', vsg::KEY_C},
        {'D', vsg::KEY_D},
        {'E', vsg::KEY_E},
        {'F', vsg::KEY_F},
        {'G', vsg::KEY_G},
        {'H', vsg::KEY_H},
        {'I', vsg::KEY_I},
        {'J', vsg::KEY_J},
        {'K', vsg::KEY_K},
        {'L', vsg::KEY_L},
        {'M', vsg::KEY_M},
        {'N', vsg::KEY_N},
        {'O', vsg::KEY_O},
        {'P', vsg::KEY_P},
        {'Q', vsg::KEY_Q},
        {'R', vsg::KEY_R},
        {'S', vsg::KEY_S},
        {'T', vsg::KEY_T},
        {'U', vsg::KEY_U},
        {'V', vsg::KEY_V},
        {'W', vsg::KEY_W},
        {'X', vsg::KEY_X},
        {'Y', vsg::KEY_Y},
        {'Z', vsg::KEY_Z},

        /* Cursor control & motion */

        {Qt::Key_Home, vsg::KEY_Home},
        {Qt::Key_Left, vsg::KEY_Left},   /* Move left, left arrow */
        {Qt::Key_Up, vsg::KEY_Up},       /* Move up, up arrow */
        {Qt::Key_Right, vsg::KEY_Right}, /* Move right, right arrow */
        {Qt::Key_Down, vsg::KEY_Down},   /* Move down, down arrow */
        //{Qt::Key_Home, vsg::KEY_Prior}, /* Prior, previous */
        //{ VK_, KEY_Page_Up = 0xFF55,
        {Qt::Key_Home, vsg::KEY_Next}, /* Next */
        //KEY_Page_Down = 0xFF56,
        {Qt::Key_End, vsg::KEY_End}, /* EOL */
        //{ KEY_Begin = 0xFF58, /* BOL */

        {'!', vsg::KEY_Exclaim},
        {'"', vsg::KEY_Quotedbl},
        {'#', vsg::KEY_Hash},
        {'$', vsg::KEY_Dollar},
        {'&', vsg::KEY_Ampersand},
        {Qt::Key_QuoteLeft, vsg::KEY_Quote},
        {'(', vsg::KEY_Leftparen},
        {')', vsg::KEY_Rightparen},
        {'*', vsg::KEY_Asterisk},
        {'+', vsg::KEY_Plus},
        {Qt::Key_Comma, vsg::KEY_Comma},
        {Qt::Key_Minus, vsg::KEY_Minus},
        {Qt::Key_Period, vsg::KEY_Period},
        {Qt::Key_Slash, vsg::KEY_Slash},
        {':', vsg::KEY_Colon},
        {Qt::Key_Semicolon, vsg::KEY_Semicolon},
        {'<', vsg::KEY_Less},
        {Qt::Key_Equal, vsg::KEY_Equals}, // + isn't an unmodded key, why does windows map is as a virtual??
        {'>', vsg::KEY_Greater},
        {'?', vsg::KEY_Question},
        {'@', vsg::KEY_At},
        {Qt::Key_BracketLeft, vsg::KEY_Leftbracket},
        {Qt::Key_Backslash, vsg::KEY_Backslash},
        {Qt::Key_BracketRight, vsg::KEY_Rightbracket},
        {'|', vsg::KEY_Caret},
        {'_', vsg::KEY_Underscore},
        {0xc0, vsg::KEY_Backquote},

        {Qt::Key_Back, vsg::KEY_BackSpace}, /* back space, back char */
        {Qt::Key_Tab, vsg::KEY_Tab},
        //    KEY_Linefeed = 0xFF0A, /* Linefeed, LF */
        {Qt::Key_Clear, vsg::KEY_Clear},
        {Qt::Key_Return, vsg::KEY_Return}, /* Return, enter */
        {Qt::Key_Pause, vsg::KEY_Pause},   /* Pause, hold */
        {Qt::Key_ScrollLock, vsg::KEY_Scroll_Lock},
        //    KEY_Sys_Req = 0xFF15,
        {Qt::Key_Escape, vsg::KEY_Escape},
        {Qt::Key_Delete, vsg::KEY_Delete}, /* Delete, rubout */

        /* Misc Functions */

        {Qt::Key_Select, vsg::KEY_Select}, /* Select, mark */
        {Qt::Key_Print, vsg::KEY_Print},
        {Qt::Key_Execute, vsg::KEY_Execute}, /* Execute, run, do */
        {Qt::Key_Insert, vsg::KEY_Insert},   /* Insert, insert here */
        //{ KEY_Undo = 0xFF65,    /* Undo, oops */
        //KEY_Redo = 0xFF66,    /* redo, again */
        {Qt::Key_Menu, vsg::KEY_Menu}, /* On Windows, this is VK_APPS, the context-menu key */
        // KEY_Find = 0xFF68,    /* Find, search */
        {Qt::Key_Cancel, vsg::KEY_Cancel}, /* Cancel, stop, abort, exit */
        {Qt::Key_Help, vsg::KEY_Help},     /* Help */
        //{ KEY_Break = 0xFF6B,
        //KEY_Mode_switch = 0xFF7E,   /* Character set switch */
        //KEY_Script_switch = 0xFF7E, /* Alias for mode_switch */
        {Qt::Key_NumLock, vsg::KEY_Num_Lock},

        /* Keypad Functions, keypad numbers cleverly chosen to map to ascii */

        //KEY_KP_Space = 0xFF80, /* space */
        //KEY_KP_Tab = 0xFF89,
        //KEY_KP_Enter = 0xFF8D, /* enter */
        //KEY_KP_F1 = 0xFF91,    /* PF1, KP_A, ... */
        //KEY_KP_F2 = 0xFF92,
        //KEY_KP_F3 = 0xFF93,
        //KEY_KP_F4 = 0xFF94,
        //KEY_KP_Home = 0xFF95,
        //KEY_KP_Left = 0xFF96,
        //KEY_KP_Up = 0xFF97,
        //KEY_KP_Right = 0xFF98,
        //KEY_KP_Down = 0xFF99,
        //KEY_KP_Prior = 0xFF9A,
        //KEY_KP_Page_Up = 0xFF9A,
        //KEY_KP_Next = 0xFF9B,
        //KEY_KP_Page_Down = 0xFF9B,
        //KEY_KP_End = 0xFF9C,
        //KEY_KP_Begin = 0xFF9D,
        //KEY_KP_Insert = 0xFF9E,
        //KEY_KP_Delete = 0xFF9F,
        //KEY_KP_Equal = 0xFFBD, /* equals */
        //KEY_KP_Multiply = 0xFFAA,
        //KEY_KP_Add = 0xFFAB,
        //KEY_KP_Separator = 0xFFAC, /* separator, often comma */
        //KEY_KP_Subtract = 0xFFAD,
        //KEY_KP_Decimal = 0xFFAE,
        //KEY_KP_Divide = 0xFFAF,

        {Qt::Key_0, vsg::KEY_KP_0},
        {Qt::Key_1, vsg::KEY_KP_1},
        {Qt::Key_2, vsg::KEY_KP_2},
        {Qt::Key_3, vsg::KEY_KP_3},
        {Qt::Key_4, vsg::KEY_KP_4},
        {Qt::Key_5, vsg::KEY_KP_5},
        {Qt::Key_6, vsg::KEY_KP_6},
        {Qt::Key_7, vsg::KEY_KP_7},
        {Qt::Key_8, vsg::KEY_KP_8},
        {Qt::Key_9, vsg::KEY_KP_9},

        /*
                * Auxiliary Functions; note the duplicate definitions for left and right
                * function keys;  Sun keyboards and a few other manufactures have such
                * function key groups on the left and/or right sides of the keyboard.
                * We've not found a keyboard with more than 35 function keys total.
                */

        {Qt::Key_F1, vsg::KEY_F1},
        {Qt::Key_F2, vsg::KEY_F2},
        {Qt::Key_F3, vsg::KEY_F3},
        {Qt::Key_F4, vsg::KEY_F4},
        {Qt::Key_F5, vsg::KEY_F5},
        {Qt::Key_F6, vsg::KEY_F6},
        {Qt::Key_F7, vsg::KEY_F7},
        {Qt::Key_F8, vsg::KEY_F8},
        {Qt::Key_F9, vsg::KEY_F9},
        {Qt::Key_F10, vsg::KEY_F10},
        {Qt::Key_F11, vsg::KEY_F11},
        {Qt::Key_F12, vsg::KEY_F12},
        {Qt::Key_F13, vsg::KEY_F13},
        {Qt::Key_F14, vsg::KEY_F14},
        {Qt::Key_F15, vsg::KEY_F15},
        {Qt::Key_F16, vsg::KEY_F16},
        {Qt::Key_F17, vsg::KEY_F17},
        {Qt::Key_F18, vsg::KEY_F18},
        {Qt::Key_F19, vsg::KEY_F19},
        {Qt::Key_F20, vsg::KEY_F20},
        {Qt::Key_F21, vsg::KEY_F21},
        {Qt::Key_F22, vsg::KEY_F22},
        {Qt::Key_F23, vsg::KEY_F23},
        {Qt::Key_F24, vsg::KEY_F24},

        //KEY_F25 = 0xFFD6,
        //KEY_F26 = 0xFFD7,
        //KEY_F27 = 0xFFD8,
        //KEY_F28 = 0xFFD9,
        //KEY_F29 = 0xFFDA,
        //KEY_F30 = 0xFFDB,
        //KEY_F31 = 0xFFDC,
        //KEY_F32 = 0xFFDD,
        //KEY_F33 = 0xFFDE,
        //KEY_F34 = 0xFFDF,
        //KEY_F35 = 0xFFE0,

        /* Modifiers */

        {Qt::Key_Shift, vsg::KEY_Shift_L},     /* Left shift */
        //            {VK_RSHIFT, KEY_Shift_R},     /* Right shift */
        //            {VK_LCONTROL, KEY_Control_L}, /* Left control */
        {Qt::Key_Control, vsg::KEY_Control_R}, /* Right control */
        {Qt::Key_CapsLock, vsg::KEY_Caps_Lock}  /* Caps lock */
        //KEY_Shift_Lock = 0xFFE6, /* Shift lock */

        //KEY_Meta_L = 0xFFE7,  /* Left meta */
        //KEY_Meta_R = 0xFFE8,  /* Right meta */
        //            {VK_LMENU, KEY_Alt_L},  /* Left alt */
        //            {VK_RMENU, KEY_Alt_R},  /* Right alt */
        //            {VK_LWIN, KEY_Super_L}, /* Left super */
        //            {VK_RWIN, KEY_Super_R}  /* Right super */
        //KEY_Hyper_L = 0xFFED, /* Left hyper */
        //KEY_Hyper_R = 0xFFEE  /* Right hyper */
    }
{
}

bool vsgQt::KeyboardMap::getKeySymbol(const QKeyEvent *e, vsg::KeySymbol &keySymbol, vsg::KeySymbol &modifiedKeySymbol, vsg::KeyModifier &keyModifier)
{
    auto itr = _keycodeMap.find((uint32_t)e->key());

    if (itr == _keycodeMap.end())
        return false;

    keySymbol = itr->second;
    modifiedKeySymbol = keySymbol;

    uint16_t modifierMask = 0;
    switch (e->modifiers())
    {
    case Qt::NoModifier:
        break;
    case Qt::ShiftModifier:
        modifierMask |= vsg::KeyModifier::MODKEY_Shift;
        break;
    case Qt::ControlModifier:
        modifierMask |= vsg::KeyModifier::MODKEY_Control;
        break;
    case Qt::AltModifier:
        modifierMask |= vsg::KeyModifier::MODKEY_Alt;
        break;
    }

    keyModifier = (vsg::KeyModifier)modifierMask;

    return true;
}
