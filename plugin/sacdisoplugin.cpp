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

#include "sacdisoplugin.h"

#include "sacdinput.h"
#include "sacdisosettingsdialog.h"

using namespace Qt::StringLiterals;

namespace Fooyin::SacdIso {
namespace {
class SacdIsoPluginSettingsProvider : public PluginSettingsProvider
{
protected:
    QDialog* createSettings(QWidget* parent) override
    {
        return new SacdIsoSettingsDialog(parent);
    }
};
} // namespace

QString SacdIsoPlugin::inputName() const
{
    return u"Super Audio CD (SACD ISO)"_s;
}

namespace {
// `priority` only exists in the development branch of fooyin; releases (e.g. 0.13.x) have no such field.
template <typename Creator>
void setPriority(Creator& creator, int priority)
{
    if constexpr(requires { creator.priority = priority; }) {
        creator.priority = priority;
    }
}
} // namespace

InputCreator SacdIsoPlugin::inputCreator() const
{
    InputCreator creator;
    creator.decoder  = []() {
        return std::make_unique<SacdDecoder>();
    };
    creator.reader = []() {
        return std::make_unique<SacdReader>();
    };
    setPriority(creator, 100);
    return creator;
}

std::unique_ptr<PluginSettingsProvider> SacdIsoPlugin::settingsProvider() const
{
    return std::make_unique<SacdIsoPluginSettingsProvider>();
}
} // namespace Fooyin::SacdIso

#include "moc_sacdisoplugin.cpp"
