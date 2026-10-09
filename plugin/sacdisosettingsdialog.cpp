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

#include "sacdisosettingsdialog.h"

#include "sacdisosettings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>

using namespace Qt::StringLiterals;

namespace Fooyin::SacdIso {
SacdIsoSettingsDialog::SacdIsoSettingsDialog(QWidget* parent)
    : QDialog{parent}
    , m_areas{new QComboBox(this)}
    , m_rate{new QComboBox(this)}
    , m_gain{new QDoubleSpinBox(this)}
{
    setWindowTitle(tr("SACD ISO Settings"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &SacdIsoSettingsDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &SacdIsoSettingsDialog::reject);

    auto* sacdGroup  = new QGroupBox(tr("Super Audio CD"), this);
    auto* sacdLayout = new QGridLayout(sacdGroup);

    m_areas->addItem(tr("Stereo and multichannel"), static_cast<int>(AreaSelection::Both));
    m_areas->addItem(tr("Stereo only"), static_cast<int>(AreaSelection::StereoOnly));
    m_areas->addItem(tr("Multichannel only"), static_cast<int>(AreaSelection::MultichannelOnly));

    m_rate->addItem(tr("352.8 kHz"), 352800);
    m_rate->addItem(tr("176.4 kHz"), 176400);
    m_rate->addItem(tr("88.2 kHz"), 88200);

    m_gain->setRange(-24.0, 24.0);
    m_gain->setSingleStep(0.5);
    m_gain->setDecimals(1);
    m_gain->setSuffix(u" dB"_s);

    int row{0};
    sacdLayout->addWidget(new QLabel(tr("Audio areas") + u":"_s, this), row, 0);
    sacdLayout->addWidget(m_areas, row++, 1);
    sacdLayout->addWidget(new QLabel(tr("DSD to PCM sample rate") + u":"_s, this), row, 0);
    sacdLayout->addWidget(m_rate, row++, 1);
    sacdLayout->addWidget(new QLabel(tr("Gain") + u":"_s, this), row, 0);
    sacdLayout->addWidget(m_gain, row++, 1);
    sacdLayout->addWidget(new QLabel(tr("Area selection applies the next time the library is scanned."), this), row++,
                          0, 1, 2);
    sacdLayout->setColumnStretch(2, 1);

    auto* layout = new QGridLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addWidget(sacdGroup, 0, 0);
    layout->addWidget(buttons, 1, 0, Qt::AlignBottom);

    m_areas->setCurrentIndex(std::max(0, m_areas->findData(static_cast<int>(areaSelection()))));
    m_rate->setCurrentIndex(std::max(0, m_rate->findData(sacdPcmRate())));
    m_gain->setValue(sacdGainDb());
}

void SacdIsoSettingsDialog::accept()
{
    m_settings.setValue(QLatin1String{SettingAreaSelection}, m_areas->currentData().toInt());
    m_settings.setValue(QLatin1String{SettingSacdPcmRate}, m_rate->currentData().toInt());
    m_settings.setValue(QLatin1String{SettingSacdGainDb}, m_gain->value());

    done(Accepted);
}
} // namespace Fooyin::SacdIso
