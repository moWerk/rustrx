/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "VoiceRecorder.h"

#include <QAudioEncoderSettings>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>

VoiceRecorder::VoiceRecorder(QObject *parent)
    : QObject(parent)
    , m_recorder(nullptr)
{
}

// made on first use: setting up the media service at start-up stalled the
// app before its window appeared
void VoiceRecorder::ensureRecorder()
{
    if (m_recorder)
        return;
    m_recorder = new QAudioRecorder(this);
    connect(m_recorder, &QAudioRecorder::stateChanged, this, &VoiceRecorder::stateChanged);
    connect(m_recorder, &QAudioRecorder::durationChanged, this, &VoiceRecorder::durationChanged);
    connect(m_recorder, static_cast<void (QAudioRecorder::*)(QMediaRecorder::Error)>(&QAudioRecorder::error),
            this, [this](QMediaRecorder::Error) {
                m_error = m_recorder->errorString();
                emit stateChanged();
            });
}

bool VoiceRecorder::recording() const
{
    return m_recorder && m_recorder->state() == QMediaRecorder::RecordingState;
}

qint64 VoiceRecorder::duration() const
{
    return m_recorder ? m_recorder->duration() : 0;
}

void VoiceRecorder::start()
{
    ensureRecorder();
    if (recording())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/voice";
    QDir().mkpath(dir);
    m_path = dir + "/voice-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".ogg";
    m_error.clear();
    QAudioEncoderSettings settings;
    settings.setCodec(QStringLiteral("audio/opus"));
    settings.setSampleRate(48000);
    settings.setChannelCount(1);
    settings.setQuality(QMultimedia::NormalQuality);
    settings.setEncodingMode(QMultimedia::ConstantQualityEncoding);
    // the names this backend lists: audio/opus in ogg
    m_recorder->setEncodingSettings(settings, QVideoEncoderSettings(), QStringLiteral("ogg"));
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_path));
    m_recorder->record();
    emit stateChanged();
}

void VoiceRecorder::stop()
{
    if (m_recorder)
        m_recorder->stop();
}

void VoiceRecorder::discard()
{
    if (m_recorder)
        m_recorder->stop();
    if (!m_path.isEmpty())
        QFile::remove(m_path);
    m_path.clear();
    emit stateChanged();
}
