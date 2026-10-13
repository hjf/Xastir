// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_menu.h"
#include <QAction>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPrintPreviewDialog>
#include <QPrinter>

FileMenu::FileMenu(QMainWindow *window, QWidget *map)
  : QObject(window), window_(window), map_(map)
{
  if (auto *action = window->findChild<QAction *>("PULDNFI015")) {
    disconnect(action, nullptr, window, nullptr);
    action->setProperty("implemented", true);
    action->setShortcut(QKeySequence::Print);
    connect(action, &QAction::triggered, this, &FileMenu::preview);
  }
}

bool FileMenu::printMap(QPrinter *printer, const QPixmap &image)
{
  if (!printer || image.isNull()) return false;
  QPainter painter;
  if (!painter.begin(printer)) return false;
  const QRect page = printer->pageRect(QPrinter::DevicePixel).toRect();
  const QSize size = image.size().scaled(page.size(), Qt::KeepAspectRatio);
  const QRect target(page.x() + (page.width() - size.width()) / 2,
                     page.y() + (page.height() - size.height()) / 2,
                     size.width(), size.height());
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.drawPixmap(target, image);
  return painter.end();
}

void FileMenu::preview()
{
  // Freeze the visible map so asynchronous tiles and live traffic cannot alter
  // the document between previewing and printing it.
  const QPixmap image = map_->grab();
  auto *dialog = new QPrintPreviewDialog(window_);
  dialog->setObjectName("mapPrintPreview");
  dialog->setWindowTitle(tr("Print map"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->printer()->setPageOrientation(QPageLayout::Landscape);
  connect(dialog, &QPrintPreviewDialog::paintRequested, dialog,
          [this, image](QPrinter *printer) {
    if (!printMap(printer, image)) {
      auto *error = new QMessageBox(QMessageBox::Warning, tr("Print map"),
        tr("Could not print the map."), QMessageBox::Ok, window_);
      error->setAttribute(Qt::WA_DeleteOnClose);
      error->open();
    }
  });
  dialog->resize(900, 650);
  dialog->open();
}
