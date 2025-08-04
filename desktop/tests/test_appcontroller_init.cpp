#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include "../src/ui/controllers/AppController.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "Creating AppController...";
    
    try {
        Murmur::AppController controller;
        qDebug() << "AppController created successfully";
        
        // Try to initialize
        qDebug() << "Initializing AppController...";
        controller.initialize();
        
        // Wait for initialization to complete
        QTimer::singleShot(5000, &app, []() {
            qDebug() << "Timeout - exiting";
            QCoreApplication::quit();
        });
        
        QObject::connect(&controller, &Murmur::AppController::initializedChanged, [&controller]() {
            qDebug() << "AppController initialized:" << controller.isInitialized();
            if (controller.isInitialized()) {
                qDebug() << "SUCCESS - AppController initialized properly";
                QCoreApplication::quit();
            }
        });
        
        return app.exec();
    } catch (const std::exception& e) {
        qDebug() << "Exception caught:" << e.what();
        return 1;
    }
}
