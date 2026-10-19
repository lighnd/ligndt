//
// Created by Gleb Obitotsky on 18.10.2026.
//
// Copyright 2026 Gleb Obitotsky,
//           2026 lighnd
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "core/entity.h"
#include "core/entity_manager.h"

using EntityId = ligndt::core::EntityId;
using EntityManager = ligndt::core::EntityManager<4096>;

class FixtureWindow : public QWidget {
  Q_OBJECT
 public:
  FixtureWindow() {
    auto* layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    layout->addWidget(m_status);

    auto addButton = [&](const char* label, auto slot) {
      auto* b = new QPushButton(label, this);
      layout->addWidget(b);
      connect(b, &QPushButton::clicked, this, slot);
    };
    addButton("Spawn fixture", &FixtureWindow::onSpawn);
    addButton("Destroy last", &FixtureWindow::onDestroyLast);
    addButton("Reset pool", &FixtureWindow::onReset);

    refresh();
  }

 private slots:
  void onSpawn() {
    if (auto r = m_world.create()) {
      m_live.append(*r);
      qInfo() << "Created idx" << r->index() << "ver" << r->version();
    } else {
      qWarning() << "Pool exhausted:"
                 << static_cast<qulonglong>(m_world.active_count()) << "active";
    }
    refresh();
  }

  void onDestroyLast() {
    if (m_live.isEmpty()) return;
    if (auto r = m_world.destroy(m_live.takeLast()); !r)
      qWarning() << "destroy failed: code" << static_cast<int>(r.error());
    refresh();
  }

  void onReset() {
    m_world.reset();
    m_live.clear();
    refresh();
  }

 private:
  void refresh() const {
    m_status->setText(
        QString("Active: %1 / %2")
            .arg(static_cast<qulonglong>(m_world.active_count()))
            .arg(static_cast<qulonglong>(EntityManager::capacity())));
  }

  EntityManager m_world;
  QList<EntityId> m_live;
  QLabel* m_status = nullptr;
};

#include "main.moc"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  FixtureWindow w;
  w.resize(400, 300);
  w.show();
  return QApplication::exec();
}