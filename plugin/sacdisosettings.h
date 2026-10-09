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

#include <algorithm>

namespace Fooyin::SacdIso {
//! Which SACD audio areas are added to the library when an ISO is scanned.
enum class AreaSelection : int
{
    Both = 0,
    StereoOnly,
    MultichannelOnly,
};

constexpr auto SettingAreaSelection = "SacdIso/AreaSelection";
constexpr auto SettingSacdPcmRate   = "SacdIso/SacdPcmRate";
constexpr auto SettingSacdGainDb    = "SacdIso/SacdGainDb";

constexpr int DefaultSacdPcmRate = 176400;

inline AreaSelection areaSelection()
{
    const FySettings settings;
    const int value = settings.value(QLatin1String{SettingAreaSelection}, static_cast<int>(AreaSelection::Both)).toInt();
    return static_cast<AreaSelection>(std::clamp(value, 0, 2));
}

//! PCM rate SACD (DSD) audio is converted to: 352800, 176400 or 88200.
inline int sacdPcmRate()
{
    const FySettings settings;
    const int rate = settings.value(QLatin1String{SettingSacdPcmRate}, DefaultSacdPcmRate).toInt();
    if(rate == 352800 || rate == 176400 || rate == 88200) {
        return rate;
    }
    return DefaultSacdPcmRate;
}

//! Gain (dB) applied to SACD PCM output.
inline double sacdGainDb()
{
    const FySettings settings;
    return std::clamp(settings.value(QLatin1String{SettingSacdGainDb}, 0.0).toDouble(), -24.0, 24.0);
}
} // namespace Fooyin::SacdIso
