#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "mobileagentcontroller.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AitrainerCorp");
    QCoreApplication::setApplicationName("AitrainerMobile");

    MobileAgentController mobileAgent;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mobileAgent", &mobileAgent);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("aimobile", "Main");

    return QGuiApplication::exec();
}
