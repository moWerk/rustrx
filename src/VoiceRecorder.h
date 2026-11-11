/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef VOICERECORDER_H
#define VOICERECORDER_H

#include <QObject>
#include <QAudioRecorder>

// A voice message as Ogg/Opus, the format other Matrix clients send and play.
// QML has no recorder in Qt 5.6; this wraps QAudioRecorder.
class VoiceRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    explicit VoiceRecorder(QObject *parent = nullptr);

    bool recording() const;
    qint64 duration() const;
    QString path() const { return m_path; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // throw the recording away
    Q_INVOKABLE void discard();

signals:
    void stateChanged();
    void durationChanged();

private:
    void ensureRecorder();
    QAudioRecorder *m_recorder;
    QString m_path;
    QString m_error;
};

#endif
