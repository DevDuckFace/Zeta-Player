#pragma once
#include <QWidget>

struct libvlc_instance_t;
struct libvlc_media_player_t;

// Embedded libVLC video surface with its own controls handled by MainWindow.
class VlcPlayer : public QWidget {
    Q_OBJECT
public:
    explicit VlcPlayer(QWidget *parent = nullptr);
    ~VlcPlayer() override;

    bool play(const QString &mrl, const QString &audioMrl = QString());
    void togglePause();
    void stop();
    bool isPlaying() const;
    void setPositionMs(qint64 ms);
    qint64 positionMs() const;
    qint64 lengthMs() const;
    void setVolume(int vol); // 0..100

protected:
    void paintEvent(QPaintEvent *) override;

private:
    libvlc_instance_t *m_vlc = nullptr;
    libvlc_media_player_t *m_mp = nullptr;
};
