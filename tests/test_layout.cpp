#include "ui/MainWindow.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QDockWidget>
#include <QAction>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QtTest/QtTest>

using namespace occ;

class TestLayout : public QObject
{
    Q_OBJECT
private slots:
    void suppliedLogoIsEmbedded()
    {
        const QPixmap logo = IconFactory::pixmap(QStringLiteral("logo"), 128);
        QVERIFY(!logo.isNull());
        const QPixmap source(QStringLiteral(":/branding/logo.png"));
        QVERIFY(!source.isNull());
        QCOMPARE(logo.toImage(), source.scaled(128, 128, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation).toImage());
        const QIcon icon = IconFactory::icon(QStringLiteral("logo"));
        QVERIFY(!icon.isNull());
        QVERIFY(icon.availableSizes().contains(QSize(256, 256)));
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("OpenCardCanvasLayoutTest"));
        AppSettings::instance().resetToDefaults();
    }

    void docksAreContained_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("compact") << QSize(900, 620);
        QTest::newRow("normal") << QSize(1280, 800);
    }

    void docksAreContained()
    {
        QFETCH(QSize, size);
        MainWindow window;
        window.resize(size);
        window.show();
        QTest::qWait(100);
        const auto docks = window.findChildren<QDockWidget *>();
        QCOMPARE(docks.size(), 5);
        auto *properties = window.findChild<QDockWidget *>(QStringLiteral("PropertyDock"));
        auto *align = window.findChild<QDockWidget *>(QStringLiteral("AlignDock"));
        QVERIFY(window.tabifiedDockWidgets(properties).contains(align));
        for (auto *dock : docks) {
            QVERIFY2(!dock->isFloating(), qPrintable(dock->objectName()));
            QVERIFY2(window.dockWidgetArea(dock) != Qt::NoDockWidgetArea,
                     qPrintable(dock->objectName()));
            if (!dock->isVisible() || dock->visibleRegion().isEmpty())
                continue;
            QVERIFY2(window.rect().contains(dock->geometry()), qPrintable(dock->objectName()));
            QVERIFY2(dock->rect().contains(dock->widget()->geometry()),
                     qPrintable(dock->objectName() + QStringLiteral(" content escapes dock")));
        }
        QVERIFY(window.centralWidget()->width() >= 200);
        QVERIFY(window.centralWidget()->height() >= 180);
        for (auto *dock : docks) {
            if (dock->visibleRegion().isEmpty())
                continue;
            QVERIFY(!dock->geometry().intersects(window.centralWidget()->geometry()));
            for (auto *other : docks) {
                if (other == dock || other->visibleRegion().isEmpty())
                    continue;
                QVERIFY2(!dock->geometry().intersects(other->geometry()),
                         qPrintable(dock->objectName() + QStringLiteral(" overlaps ")
                                    + other->objectName()));
            }
        }
    }

    void tallPanelsCanScroll()
    {
        MainWindow window;
        window.show();
        QTest::qWait(100);
        for (const auto &name : { "AlignDock", "PersonalizationDock" }) {
            auto *dock = window.findChild<QDockWidget *>(QString::fromLatin1(name));
            QVERIFY(dock);
            auto *scroll = qobject_cast<QScrollArea *>(dock->widget());
            QVERIFY2(scroll, name);
            dock->setFloating(true);
            dock->resize(380, 180);
            dock->show();
            QTest::qWait(50);
            QVERIFY2(scroll->verticalScrollBar()->maximum() > 0, name);
        }
    }

    void resetRedocksFloatingPanels()
    {
        MainWindow window;
        window.show();
        const auto docks = window.findChildren<QDockWidget *>();
        for (auto *dock : docks) {
            dock->setFloating(true);
            dock->hide();
        }
        auto *reset = window.findChild<QAction *>(QStringLiteral("ResetLayoutAction"));
        QVERIFY(reset);
        reset->trigger();
        QTest::qWait(100);
        for (auto *dock : docks) {
            QVERIFY2(!dock->isFloating(), qPrintable(dock->objectName()));
            QVERIFY(!dock->isHidden());
            QVERIFY(window.dockWidgetArea(dock) != Qt::NoDockWidgetArea);
        }
    }
};

QTEST_MAIN(TestLayout)
#include "test_layout.moc"