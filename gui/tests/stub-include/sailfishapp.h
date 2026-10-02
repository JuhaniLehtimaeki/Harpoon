#pragma once

// Desktop stand-in for libsailfishapp, used only to check that
// gui/src/harpoon.cpp compiles outside the SailfishOS SDK.

#include <QGuiApplication>
#include <QQuickView>
#include <QUrl>

namespace SailfishApp {
inline QGuiApplication *application(int &argc, char **argv) { return new QGuiApplication(argc, argv); }
inline QQuickView *createView() { return new QQuickView; }
inline QUrl pathTo(const QString &filename) { return QUrl::fromLocalFile(filename); }
inline QUrl pathToMainQml() { return pathTo(QStringLiteral("qml/harpoon.qml")); }
} // namespace SailfishApp
