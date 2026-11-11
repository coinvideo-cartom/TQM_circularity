#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    if(a.arguments().contains("--smoke-test")) {
        QTimer::singleShot(200, &a, [&]{w.grab().save("ui-smoke.png");a.quit();});
    }
    return a.exec();
}
