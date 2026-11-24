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
#include <QSettings>
#include <QStringList>
#include <QVariantHash>

// The settings another app could misuse (audit SEC1): every jailed app may
// read and write dconf, so these live in Rustrx's own settings file, in its
// config folder, which sailjail keeps to this app. The look stays in dconf
// (components/Settings.qml). Until 0.8.4 they were dconf keys of the same
// names under qml/names.js's folder; the first start moves them over once.
class PrivateStore : public QObject
{
    Q_OBJECT
public:
    // where the keys were (dconf), for the one move
    class OldValues
    {
    public:
        virtual ~OldValues() {}
        // invalid when the key is not set
        virtual QVariant value(const QString &key) = 0;
        virtual void unset(const QString &key) = 0;
        // the unsets written; false when that failed
        virtual bool sync() = 0;
    };

    // one per process (main.cpp), for both QML engines
    explicit PrivateStore(const QString &file, QObject *parent = nullptr);
    ~PrivateStore();
    static PrivateStore *instance();

    // Once (a flag in the file): the keys set in `old` into the file, then
    // out of `old`; never read from there again, so a value another app
    // writes later changes nothing. False when the file could not be
    // written: `old` is kept and the next start tries again.
    bool migrate(OldValues &old);

    static QStringList keys();
    static QVariant defaultValue(const QString &key);
    QVariant value(const QString &key) const;
    void setValue(const QString &key, const QVariant &value);

signals:
    void changed(const QString &key);

private:
    QSettings m_settings;
    // typed as the default (the file gives strings back: "false" is true in JS)
    QVariantHash m_values;
};

// The QML face (import Rustrx 1.0): each engine makes its own, all of them
// read and write the process's PrivateStore and hear each other's changes.
// Without a store (qmlscene) the defaults, and nothing is kept.
class PrivateSettings : public QObject
{
    Q_OBJECT
    // the property names are the keys (PrivateStore::keys)
    Q_PROPERTY(bool sendReceipts READ sendReceipts WRITE setSendReceipts NOTIFY sendReceiptsChanged)
    Q_PROPERTY(bool readWhenShown READ readWhenShown WRITE setReadWhenShown NOTIFY readWhenShownChanged)
    Q_PROPERTY(bool sendTyping READ sendTyping WRITE setSendTyping NOTIFY sendTypingChanged)
    Q_PROPERTY(bool showOnline READ showOnline WRITE setShowOnline NOTIFY showOnlineChanged)
    Q_PROPERTY(bool chatDetails READ chatDetails WRITE setChatDetails NOTIFY chatDetailsChanged)
    Q_PROPERTY(bool strangerInvites READ strangerInvites WRITE setStrangerInvites NOTIFY strangerInvitesChanged)
    Q_PROPERTY(bool notifyText READ notifyText WRITE setNotifyText NOTIFY notifyTextChanged)
    Q_PROPERTY(QString closedMode READ closedMode WRITE setClosedMode NOTIFY closedModeChanged)
    Q_PROPERTY(bool reopenLast READ reopenLast WRITE setReopenLast NOTIFY reopenLastChanged)
    Q_PROPERTY(QString lastRoom READ lastRoom WRITE setLastRoom NOTIFY lastRoomChanged)
    Q_PROPERTY(int lastRoomAccount READ lastRoomAccount WRITE setLastRoomAccount NOTIFY lastRoomAccountChanged)
    Q_PROPERTY(bool secretsNoticeSeen READ secretsNoticeSeen WRITE setSecretsNoticeSeen NOTIFY secretsNoticeSeenChanged)
    Q_PROPERTY(bool callNoticeSeen READ callNoticeSeen WRITE setCallNoticeSeen NOTIFY callNoticeSeenChanged)
    Q_PROPERTY(QString searchHistory READ searchHistory WRITE setSearchHistory NOTIFY searchHistoryChanged)
public:
    explicit PrivateSettings(QObject *parent = nullptr);

    bool sendReceipts() const { return get("sendReceipts").toBool(); }
    void setSendReceipts(bool v) { set("sendReceipts", v); }
    bool readWhenShown() const { return get("readWhenShown").toBool(); }
    void setReadWhenShown(bool v) { set("readWhenShown", v); }
    bool sendTyping() const { return get("sendTyping").toBool(); }
    void setSendTyping(bool v) { set("sendTyping", v); }
    bool showOnline() const { return get("showOnline").toBool(); }
    void setShowOnline(bool v) { set("showOnline", v); }
    bool chatDetails() const { return get("chatDetails").toBool(); }
    void setChatDetails(bool v) { set("chatDetails", v); }
    bool strangerInvites() const { return get("strangerInvites").toBool(); }
    void setStrangerInvites(bool v) { set("strangerInvites", v); }
    bool notifyText() const { return get("notifyText").toBool(); }
    void setNotifyText(bool v) { set("notifyText", v); }
    QString closedMode() const { return get("closedMode").toString(); }
    void setClosedMode(const QString &v) { set("closedMode", v); }
    bool reopenLast() const { return get("reopenLast").toBool(); }
    void setReopenLast(bool v) { set("reopenLast", v); }
    QString lastRoom() const { return get("lastRoom").toString(); }
    void setLastRoom(const QString &v) { set("lastRoom", v); }
    int lastRoomAccount() const { return get("lastRoomAccount").toInt(); }
    void setLastRoomAccount(int v) { set("lastRoomAccount", v); }
    bool secretsNoticeSeen() const { return get("secretsNoticeSeen").toBool(); }
    void setSecretsNoticeSeen(bool v) { set("secretsNoticeSeen", v); }
    bool callNoticeSeen() const { return get("callNoticeSeen").toBool(); }
    void setCallNoticeSeen(bool v) { set("callNoticeSeen", v); }
    QString searchHistory() const { return get("searchHistory").toString(); }
    void setSearchHistory(const QString &v) { set("searchHistory", v); }

signals:
    void sendReceiptsChanged();
    void readWhenShownChanged();
    void sendTypingChanged();
    void showOnlineChanged();
    void chatDetailsChanged();
    void strangerInvitesChanged();
    void notifyTextChanged();
    void closedModeChanged();
    void reopenLastChanged();
    void lastRoomChanged();
    void lastRoomAccountChanged();
    void secretsNoticeSeenChanged();
    void callNoticeSeenChanged();
    void searchHistoryChanged();

private:
    QVariant get(const char *key) const;
    void set(const char *key, const QVariant &value);
    void relay(const QString &key);
};
