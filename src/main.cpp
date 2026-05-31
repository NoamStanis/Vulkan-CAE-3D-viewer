#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QVulkanInstance>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    // ── Force Vulkan before any window is created ─────────────────────────────
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

    QGuiApplication app(argc, argv);

    // ── Set up the QVulkanInstance ────────────────────────────────────────────
    // This is shared between Qt's scene graph and our VulkanRenderer.
    QVulkanInstance vulkanInstance;

#ifdef QT_DEBUG
    // Enable validation layers in debug builds.
    // Requires the Vulkan SDK to be installed.
    vulkanInstance.setLayers({"VK_LAYER_KHRONOS_validation"});
    // Route validation messages through Qt's logging system.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));
#endif

    if (!vulkanInstance.create()) {
        qFatal("Failed to create Vulkan instance (error %d). "
               "Is a Vulkan-capable driver installed?",
               vulkanInstance.errorCode());
        return 1;
    }

    // ── Load QML ──────────────────────────────────────────────────────────────
    QQmlApplicationEngine engine;

    // Make the Vulkan instance available to any QQuickWindow that gets created.
    // Qt picks it up automatically when QQuickWindow::setVulkanInstance is called,
    // but the easiest way is to set it as the default on the engine's root object.
    // We do it after the window is created via the objectCreated signal.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [&vulkanInstance](QObject *obj, const QUrl &) {
            if (auto *w = qobject_cast<QQuickWindow *>(obj))
                w->setVulkanInstance(&vulkanInstance);
        },
        Qt::QueuedConnection);

    const QUrl url(QStringLiteral("qrc:/VulkanCAEViewer/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
