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
#include <QPushButton>
#include "core/entity.h"

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) {
    QApplication a(argc, argv);
    constexpr auto id = ligndt::core::EntityId::make(1U, 1);
    const QString button_text = QString::asprintf(
        "\nIndex: %u\nVersion: %u\nRaw: 0x%08X",
        id.index(), id.version(), id.bits()
    );
    QPushButton button(button_text, nullptr);
    button.resize(300, 150);
    button.show();
    return QApplication::exec();
}
