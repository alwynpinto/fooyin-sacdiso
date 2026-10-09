/*
 * Fooyin
 * Copyright © 2026, fooyin contributors
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <core/coresettings.h>

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

namespace Fooyin::SacdIso {
class SacdIsoSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SacdIsoSettingsDialog(QWidget* parent = nullptr);

    void accept() override;

private:
    FySettings m_settings;
    QComboBox* m_areas;
    QComboBox* m_rate;
    QDoubleSpinBox* m_gain;
};
} // namespace Fooyin::SacdIso
