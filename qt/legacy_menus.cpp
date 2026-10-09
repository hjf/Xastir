// SPDX-License-Identifier: GPL-2.0-or-later
#include "legacy_menus.h"
#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <stdexcept>

namespace {
QString menuText(const QJsonObject &entry)
{
  QString text = entry.value("label").toString();
  // Motif stores mnemonics separately; Qt interprets literal ampersands.
  text.replace('&', "&&");
  const QString mnemonic = entry.value("mnemonic").toString();
  if (!mnemonic.isEmpty()) {
    const auto index = text.indexOf(mnemonic.left(1), 0, Qt::CaseInsensitive);
    if (index >= 0) text.insert(index, '&');
  }
  return text;
}
void populate(QMenu *menu, const QJsonArray &entries, QMainWindow *window)
{
  for (const auto &value : entries) {
    const auto entry = value.toObject();
    if (entry.value("separator").toBool()) { menu->addSeparator(); continue; }
    const QString id = entry.value("id").toString();
    if (entry.contains("children")) {
      auto *child = menu->addMenu(menuText(entry));
      child->setObjectName(id);
      child->setSeparatorsCollapsible(false);
      populate(child, entry.value("children").toArray(), window);
      continue;
    }
    auto *action = menu->addAction(menuText(entry));
    action->setObjectName(id == "PULDNFI004" ? QStringLiteral("actionExit") : id);
    action->setProperty("legacyMenuId", id);
    action->setProperty("legacyLabel", entry.value("label").toString());
    action->setCheckable(entry.value("checkable").toBool());
    if (id == "PULDNFI004") action->setShortcut(QKeySequence::Quit);
    QObject::connect(action, &QAction::triggered, window, [window, action] {
      // A placeholder must not imply that changing a legacy toggle applied a setting.
      if (action->isCheckable()) action->setChecked(false);
      auto *dialog = new QMessageBox(QMessageBox::Information,
        QMainWindow::tr("Not implemented yet"), QMainWindow::tr("Not implemented yet"),
        QMessageBox::Ok, window);
      dialog->setObjectName(QStringLiteral("notImplementedDialog"));
      dialog->setInformativeText(action->property("legacyLabel").toString());
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->open();
    });
  }
}
}
void createLegacyMenus(QMainWindow *window)
{
  // Snapshot of create_appshell's menus in src/main.c, with English labels and
  // mnemonics from config/language-English.sys. Includes optional build features.
  Q_INIT_RESOURCE(legacy_menus);
  QFile file(QStringLiteral(":/menus/legacy_menus.json"));
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Embedded legacy menu definitions are missing");
  const auto menus = QJsonDocument::fromJson(file.readAll()).array();
  if (menus.isEmpty()) throw std::runtime_error("Invalid embedded legacy menus");
  for (const auto &value : menus) {
    const auto entry = value.toObject();
    auto *menu = window->menuBar()->addMenu(menuText(entry));
    menu->setObjectName(entry.value("id").toString());
    // Preserve adjacent separators found in the Motif implementation.
    menu->setSeparatorsCollapsible(false);
    populate(menu, entry.value("children").toArray(), window);
  }
}
