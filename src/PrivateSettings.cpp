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
#include "PrivateSettings.h"

#include <QDebug>
#include <QMetaMethod>
#include <QMetaProperty>

static PrivateStore *s_store = nullptr;

// the keys under this group of the settings file
static const QString kGroup = QStringLiteral("settings/");
// set once the dconf keys were moved here
static const QString kFromDconf = QStringLiteral("settings/fromDconf");

namespace {
struct Entry {
    const char *key;
    QVariant byDefault;
};
}

// each key with its default (the value while it is not set); what each
// one does is said where Settings.qml names it
static const QList<Entry> &entries()
{
    static const QList<Entry> list {
        { "sendReceipts", true },
        { "readWhenShown", true },
        { "sendTyping", true },
        { "showOnline", false },
        { "chatDetails", true },
        { "strangerInvites", true },
        { "notifyText", true },
        { "closedMode", QString() },
        { "reopenLast", false },
        { "lastRoom", QString() },
        { "lastRoomAccount", 0 },
        { "secretsNoticeSeen", false },
        { "callNoticeSeen", false },
        { "searchHistory", QStringLiteral("[]") },
    };
    return list;
}

static const Entry *entry(const QString &key)
{
    for (const Entry &e : entries())
        if (key == QLatin1String(e.key))
            return &e;
    return nullptr;
}

// `v` as the default's type, or invalid when it cannot be
static QVariant typed(QVariant v, const QVariant &byDefault)
{
    if (byDefault.type() == QVariant::String && v.type() == QVariant::StringList)
        v = v.toStringList().join(QStringLiteral(", "));   // an INI value with commas, written by hand
    return v.convert(byDefault.userType()) ? v : QVariant();
}

PrivateStore::PrivateStore(const QString &file, QObject *parent)
    : QObject(parent)
    , m_settings(file, QSettings::IniFormat)
{
    for (const Entry &e : entries()) {
        const QVariant v = typed(m_settings.value(kGroup + QLatin1String(e.key)), e.byDefault);
        m_values.insert(QLatin1String(e.key), v.isValid() ? v : e.byDefault);
    }
    if (!s_store)
        s_store = this;
}

PrivateStore::~PrivateStore()
{
    if (s_store == this)
        s_store = nullptr;
}

PrivateStore *PrivateStore::instance()
{
    return s_store;
}

QStringList PrivateStore::keys()
{
    QStringList all;
    for (const Entry &e : entries())
        all << QLatin1String(e.key);
    return all;
}

QVariant PrivateStore::defaultValue(const QString &key)
{
    const Entry *e = entry(key);
    return e ? e->byDefault : QVariant();
}

QVariant PrivateStore::value(const QString &key) const
{
    return m_values.value(key, defaultValue(key));
}

void PrivateStore::setValue(const QString &key, const QVariant &value)
{
    const Entry *e = entry(key);
    if (!e)
        return;
    const QVariant v = typed(value, e->byDefault);
    if (!v.isValid() || v == m_values.value(key))
        return;
    m_values.insert(key, v);
    // written by QSettings itself on the next turn of the event loop, and at the end
    m_settings.setValue(kGroup + key, v);
    emit changed(key);
}

bool PrivateStore::migrate(OldValues &old)
{
    if (m_settings.value(kFromDconf).toInt() >= 1)
        return true;
    QStringList found;
    for (const Entry &e : entries()) {
        const QString key = QLatin1String(e.key);
        const QVariant v = typed(old.value(key), e.byDefault);
        if (!v.isValid())
            continue;
        found << key;
        // set here already (a start that could not write the flag): that one is newer
        if (m_settings.contains(kGroup + key))
            continue;
        m_values.insert(key, v);
        m_settings.setValue(kGroup + key, v);
    }
    m_settings.setValue(kFromDconf, 1);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        // not left pending: a later write in this run must not mark it done
        m_settings.remove(kFromDconf);
        qWarning() << "settings: cannot write" << m_settings.fileName() << "; dconf kept for the next start";
        return false;
    }
    for (const QString &key : found)
        old.unset(key);
    if (!found.isEmpty() && !old.sync())
        qWarning() << "settings: the old dconf keys could not be removed";
    qInfo().noquote() << "settings:" << found.size() << "kept in the private file instead of dconf";
    return true;
}

PrivateSettings::PrivateSettings(QObject *parent)
    : QObject(parent)
{
    if (PrivateStore *store = PrivateStore::instance())
        connect(store, &PrivateStore::changed, this, &PrivateSettings::relay);
}

QVariant PrivateSettings::get(const char *key) const
{
    const QString name = QLatin1String(key);
    PrivateStore *store = PrivateStore::instance();
    return store ? store->value(name) : PrivateStore::defaultValue(name);
}

void PrivateSettings::set(const char *key, const QVariant &value)
{
    if (PrivateStore *store = PrivateStore::instance())
        store->setValue(QLatin1String(key), value);
}

// a change from any engine: the property's own signal here
void PrivateSettings::relay(const QString &key)
{
    const int i = metaObject()->indexOfProperty(key.toLatin1().constData());
    if (i >= 0)
        metaObject()->property(i).notifySignal().invoke(this);
}
