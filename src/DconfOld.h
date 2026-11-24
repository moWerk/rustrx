/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "PrivateSettings.h"

#include <MDConfItem>
#include <memory>
#include <vector>

// The private settings' old place, dconf, read once for the move
// (PrivateStore::migrate). The folder is qml/names.js's dconfPath.
class DconfOld : public PrivateStore::OldValues
{
public:
    QVariant value(const QString &key) override
    {
        return MDConfItem(path(key)).value();
    }
    // kept until sync(): an unset item's write goes out asynchronously
    void unset(const QString &key) override
    {
        m_unset.emplace_back(new MDConfItem(path(key)));
        m_unset.back()->unset();
    }
    bool sync() override
    {
        bool ok = true;
        for (auto &item : m_unset)
            ok = item->sync() && ok;
        m_unset.clear();
        return ok;
    }

private:
    static QString path(const QString &key)
    {
        return QLatin1String("/apps/harbour-rustrx/") + key;
    }
    std::vector<std::unique_ptr<MDConfItem>> m_unset;
};
