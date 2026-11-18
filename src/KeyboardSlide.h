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

#include <QObject>

// Whether a slide-out keyboard (the Pro¹'s) is open, as MCE tells it: asked
// once, then followed by its signal. Phones without one stay closed. Lets
// "Lock to portrait" give way while the keyboard is out.
class KeyboardSlide : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ open NOTIFY openChanged)
public:
    explicit KeyboardSlide(QObject *parent = nullptr);
    bool open() const { return m_open; }

signals:
    void openChanged();

private slots:
    void onSlide(const QString &state);

private:
    bool m_open = false;
};
