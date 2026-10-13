// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "snapshot_control.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

class SnapshotTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirSnapshotTests");
    QCoreApplication::setApplicationName("Snapshots");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void menuCapturesMapAndBoundsQueue() {
    QTemporaryDir output;
    MainWindow window(ApplicationOptions{});
    auto *map = window.findChild<MapWidget *>();
    map->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    QVERIFY(snapshots);
    snapshots->configure(output.path(), 1);
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    auto *action = window.findChild<QAction *>("PULDNFI014");
    QVERIFY(action && action->property("implemented").toBool());
    QVERIFY(!action->isChecked());
    const auto expected = map->grab().toImage();
    action->trigger();
    QVERIFY(snapshots->enabled());
    QVERIFY(snapshots->busy());
    for (int i = 0; i < 100; ++i) snapshots->captureNow();
    QTRY_COMPARE(saved.count(), 1);
    const QString path = output.filePath("snapshot.png");
    QCOMPARE(saved.first().first().toString(), path);
    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), expected.size());
    QCOMPARE(image.convertToFormat(QImage::Format_RGB32), expected.convertToFormat(QImage::Format_RGB32));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files).size(), 1);
    auto *timer = snapshots->findChild<QTimer *>("pngSnapshotTimer");
    QVERIFY(timer && timer->isActive());
    QCOMPARE(timer->interval(), 60000);
    // Drive real periodic timer delivery without waiting a production minute.
    map->setMapBackground(Qt::green);
    timer->setInterval(20);
    QTRY_VERIFY(saved.count() >= 2);
    action->trigger();
    QVERIFY(!snapshots->enabled());
    QVERIFY(!timer->isActive());
    QTRY_VERIFY(!snapshots->busy());
    const auto count = saved.count();
    snapshots->captureNow();
    QTest::qWait(50);
    QCOMPARE(saved.count(), count);
    QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList{"snapshot.png"});
    QVERIFY(QImage(path).convertToFormat(QImage::Format_RGB32) != image.convertToFormat(QImage::Format_RGB32));
  }
  void timingCancelApplyAndRestart() {
    QTemporaryDir output;
    ApplicationOptions options;
    {
      MainWindow window(options);
      auto *snapshots = window.findChild<SnapshotControl *>();
      const auto initial = snapshots->directory();
      window.findChild<QAction *>("PULDNCF003")->trigger();
      auto *dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingSnapshotDirectory")->setText(output.path());
      dialog->findChild<QSpinBox *>("timingSnapshotInterval")->setValue(2);
      dialog->reject();
      QCOMPARE(snapshots->directory(), initial);
      QCOMPARE(snapshots->intervalMinutes(), 5);
      QVERIFY(!QSettings().contains("snapshots/directory"));
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      window.findChild<QAction *>("PULDNCF003")->trigger();
      dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingSnapshotDirectory")->setText(output.path());
      dialog->findChild<QSpinBox *>("timingSnapshotInterval")->setValue(2);
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
      QCOMPARE(snapshots->directory(), output.path());
      QCOMPARE(snapshots->intervalMinutes(), 2);
      QVERIFY(!snapshots->enabled());
      window.findChild<QAction *>("PULDNFI014")->trigger();
      QVERIFY(snapshots->enabled());
      QVERIFY(!snapshots->busy()); // Hidden windows are not captured.
    }
    MainWindow restored(options);
    restored.findChild<MapWidget *>()->setMapsEnabled(false);
    auto *snapshots = restored.findChild<SnapshotControl *>();
    QCOMPARE(snapshots->directory(), output.path());
    QCOMPARE(snapshots->intervalMinutes(), 2);
    QVERIFY(restored.findChild<QAction *>("PULDNFI014")->isChecked());
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    restored.show();
    QTRY_COMPARE(saved.count(), 1);
  }
  void failureDisablesToggleAndPreservesExistingTarget() {
    QTemporaryDir output;
    const auto target = output.filePath("snapshot.png");
    QVERIFY(QDir().mkpath(target));
    QFile marker(target + "/keep.txt");
    QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("keep"); marker.close();
    MainWindow window(ApplicationOptions{});
    window.findChild<MapWidget *>()->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    snapshots->configure(output.path(), 1);
    QSignalSpy errors(snapshots, &SnapshotControl::errorOccurred);
    window.findChild<QAction *>("PULDNFI014")->trigger();
    QTRY_COMPARE(errors.count(), 1);
    QVERIFY(!snapshots->enabled());
    QVERIFY(!window.findChild<QAction *>("PULDNFI014")->isChecked());
    QVERIFY(!QSettings().value("snapshots/enabled").toBool());
    QVERIFY(marker.open(QIODevice::ReadOnly)); QCOMPARE(marker.readAll(), QByteArray("keep"));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files).size(), 0);
  }
  void reconfigureIgnoresSupersededFailure() {
    QTemporaryDir output;
    const auto blocked = output.filePath("not-a-directory");
    QFile file(blocked); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
    MainWindow window(ApplicationOptions{});
    window.findChild<MapWidget *>()->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    snapshots->configure(blocked, 1);
    QSignalSpy errors(snapshots, &SnapshotControl::errorOccurred);
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    snapshots->setEnabled(true);
    QVERIFY(snapshots->busy());
    snapshots->configure(output.filePath("valid"), 2);
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(errors.count(), 0);
    QVERIFY(snapshots->enabled());
    QVERIFY(!QImage(output.filePath("valid/snapshot.png")).isNull());
  }
  void legacyOptionsAndInvalidInterval() {
    QTemporaryDir output;
    QFile file(output.filePath("legacy.cnf"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("STATION_CALLSIGN:LOCAL\nSNAPSHOT_INTERVAL:7\nSNAPSHOTS_ENABLED:1\n"); file.close();
    ApplicationOptions options;
    QString error;
    QVERIFY2(readXastirConfig(file.fileName(), options.config, &error), qPrintable(error));
    QSettings().setValue("snapshots/directory", output.path());
    QSettings().setValue("snapshots/intervalMinutes", "invalid");
    MainWindow window(options);
    auto *snapshots = window.findChild<SnapshotControl *>();
    QCOMPARE(snapshots->intervalMinutes(), 7);
    QVERIFY(snapshots->enabled());
    QVERIFY(!QFile::exists(output.filePath("snapshot.png")));
  }
  void shutdownFinishesOutstandingWrite() {
    QTemporaryDir output;
    {
      MainWindow window(ApplicationOptions{});
      window.findChild<MapWidget *>()->setMapsEnabled(false);
      window.show();
      auto *snapshots = window.findChild<SnapshotControl *>();
      snapshots->configure(output.path(), 1);
      snapshots->setEnabled(true);
      QVERIFY(snapshots->busy());
    }
    QVERIFY(!QImage(output.filePath("snapshot.png")).isNull());
  }
};
QTEST_MAIN(SnapshotTest)
#include "test_snapshots.moc"
