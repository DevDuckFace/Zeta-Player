#include "VlcPlayer.h"
#include <QPainter>
#include <vlc/vlc.h>

VlcPlayer::VlcPlayer(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(240);
    const char *args[] = {"--no-video-title-show", "--quiet"};
    m_vlc = libvlc_new(2, args);
    if (m_vlc)
        m_mp = libvlc_media_player_new(m_vlc);
}

VlcPlayer::~VlcPlayer() {
    if (m_mp) {
        libvlc_media_player_stop(m_mp);
        libvlc_media_player_release(m_mp);
    }
    if (m_vlc)
        libvlc_release(m_vlc);
}

bool VlcPlayer::play(const QString &mrl, const QString &audioMrl) {
    if (!m_mp) return false;
    libvlc_media_t *media = libvlc_media_new_location(m_vlc, mrl.toUtf8().constData());
    if (!media) return false;
    if (!audioMrl.isEmpty()) {
        QByteArray opt = ":input-slave=" + audioMrl.toUtf8();
        libvlc_media_add_option(media, opt.constData());
    }
    libvlc_media_player_set_media(m_mp, media);
    libvlc_media_release(media);
    libvlc_media_player_set_hwnd(m_mp, reinterpret_cast<void *>(winId()));
    return libvlc_media_player_play(m_mp) == 0;
}

void VlcPlayer::togglePause() {
    if (m_mp) libvlc_media_player_pause(m_mp);
}

void VlcPlayer::stop() {
    if (m_mp) libvlc_media_player_stop(m_mp);
    update();
}

bool VlcPlayer::isPlaying() const {
    return m_mp && libvlc_media_player_is_playing(m_mp);
}

void VlcPlayer::setPositionMs(qint64 ms) {
    if (m_mp) libvlc_media_player_set_time(m_mp, ms);
}

qint64 VlcPlayer::positionMs() const {
    return m_mp ? libvlc_media_player_get_time(m_mp) : 0;
}

qint64 VlcPlayer::lengthMs() const {
    return m_mp ? libvlc_media_player_get_length(m_mp) : 0;
}

void VlcPlayer::setVolume(int vol) {
    if (m_mp) libvlc_audio_set_volume(m_mp, vol);
}

void VlcPlayer::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!isPlaying()) {
        p.setPen(QColor(90, 95, 110));
        QFont f = font();
        f.setPointSize(22);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, "▶");
    }
}
