// SPDX-License-Identifier: GPL-3.0-or-later
// A TimelineModel driven the way the app drives it: one row set with
// setItems, one role read back with data(). The tests go through the
// model's public face, so the rendering code is tested as it is built into
// the app, private members and all.
#ifndef TESTS_TIMELINE_H
#define TESTS_TIMELINE_H

#include "TimelineModel.h"

#include <QVariantMap>

class Timeline
{
public:
    TimelineModel model;

    int role(const char *name) const { return model.roleNames().key(QByteArray(name), -1); }

    // the role of one row made of item; a new id each time, so nothing is
    // served from the render cache
    QVariant row(QVariantMap item, const char *roleName)
    {
        item.insert(QStringLiteral("id"), QString::number(++m_id));
        model.setItems({ item });
        return model.data(model.index(0), role(roleName));
    }

    // a message's formatted body (the core's "html")
    QString html(const QString &html) { return row({ { "html", html } }, "html").toString(); }
    // the same HTML as a reply's quoted original
    QString replyHtml(const QString &html)
    {
        return row({ { "reply_to", QVariantMap { { "html", html } } } }, "reply_to").toMap().value("html").toString();
    }

private:
    int m_id = 0;
};

#endif
