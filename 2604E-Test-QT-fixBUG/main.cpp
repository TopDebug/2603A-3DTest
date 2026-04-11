#include "Application.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVersionNumber>
#include <QVulkanInstance>
#include <QWindow>

#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        QApplication qapp(argc, argv);

        QVulkanInstance vulkanInstance;
        vulkanInstance.setApiVersion(QVersionNumber(1, 2, 0));
        if (!vulkanInstance.create()) {
            throw std::runtime_error("QVulkanInstance::create() failed (install a Vulkan driver and dev packages).");
        }

        Application engine;
        MainWindow mainWin(&engine, &vulkanInstance);

        mainWin.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents);

        engine.attachAndInit(mainWin.vulkanWindow(), &vulkanInstance);
        mainWin.syncViewPanelFromApp();

        mainWin.vulkanWindow()->installEventFilter(&mainWin);

        QTimer frameTimer;
        QObject::connect(&frameTimer, &QTimer::timeout, [&engine, &mainWin]() {
            engine.drawFrame();
            mainWin.syncOrthoSpinsFromAppUnlessFocused();
            mainWin.updateReadoutLabel(QString::fromStdString(engine.cursorReadoutString()));
        });
        frameTimer.start(16);

        return qapp.exec();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
