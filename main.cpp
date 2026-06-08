#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "agentcontroller.h"

void myMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QFile file("qml_errors.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream stream(&file);
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        stream << "[" << timeStr << "] ";
        switch (type) {
        case QtDebugMsg:
            stream << "Debug: " << msg << " (" << context.file << ":" << context.line << ")\n";
            break;
        case QtInfoMsg:
            stream << "Info: " << msg << "\n";
            break;
        case QtWarningMsg:
            stream << "Warning: " << msg << " (" << context.file << ":" << context.line << ")\n";
            break;
        case QtCriticalMsg:
            stream << "Critical: " << msg << " (" << context.file << ":" << context.line << ")\n";
            break;
        case QtFatalMsg:
            stream << "Fatal: " << msg << " (" << context.file << ":" << context.line << ")\n";
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    // Clean up any previous log file
    QFile::remove("qml_errors.txt");
    qInstallMessageHandler(myMessageHandler);

    QGuiApplication app(argc, argv);

    AgentController agentController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("agent", &agentController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Aitrainer", "Main");

    return app.exec();
}
