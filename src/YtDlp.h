#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QVector>

struct SearchResult {
    QString id;
    QString title;
    QString url;
    QString uploader;
    QString duration;   // formatted
    QString thumbUrl;
    int itemCount = -1;      // playlists only
    qint64 viewCount = -1;   // videos only (for sorting)
};

struct FormatOption {
    QString label;      // "1080p", "720p"...
    int height = 0;
    bool audioOnly = false;
};

// A live-streaming search: one yt-dlp process prints one JSON entry per line as
// it walks the results, so rows appear continuously instead of page-by-page.
class SearchStream : public QObject {
    Q_OBJECT
public:
    SearchStream(const QStringList &args, bool channels, QObject *parent = nullptr);
    void stop();

signals:
    void chunk(const QVector<SearchResult> &items);
    void finished();

private:
    QProcess *m_proc;
    bool m_channels;
    QByteArray m_buf;
};

// Wraps yt-dlp.exe (QProcess). All methods are async; results come via signals.
class YtDlp : public QObject {
    Q_OBJECT
public:
    // Hard cap so a huge channel/search can't grow the UI unbounded.
    static constexpr int kMaxItems = 1000;

    explicit YtDlp(QObject *parent = nullptr);
    static QString exePath();
    static QString downloadsDir();

    void setBrowserForCookies(const QString &browser) { m_browser = browser; }

    // Streaming searches — caller owns the returned SearchStream.
    SearchStream *streamVideos(const QString &query);
    SearchStream *streamPlaylists(const QString &query);
    SearchStream *streamChannels(const QString &query);
    SearchStream *streamChannelVideos(const QString &channelUrl);

    void fetchPlaylistItems(const QString &playlistUrl);
    void fetchFormats(const QString &videoUrl);
    void resolveStreamUrl(const QString &videoUrl); // for the embedded player

signals:
    void playlistItems(const QVector<SearchResult> &items);
    void formatsReady(const QString &videoUrl, const QVector<FormatOption> &formats);
    void streamUrlReady(const QString &videoUrl, const QString &directUrl,
                        const QString &audioUrl, int height);
    void error(const QString &message);

private:
    QProcess *runJson(const QStringList &args, std::function<void(const QJsonDocument &)> onDone);
    QStringList baseArgs() const;
    QString m_browser;
};

// One download (or live recording) job with progress.
class DownloadJob : public QObject {
    Q_OBJECT
public:
    DownloadJob(const QString &url, const QString &title, int height,
                bool audioOnly, bool isLive, const QString &browser,
                const QString &outDir, QObject *parent = nullptr);
    void start();
    void stop();
    QString title() const { return m_title; }
    bool isLive() const { return m_live; }

signals:
    void progress(double percent, const QString &speed, const QString &eta);
    void statusText(const QString &text);
    void finished(bool ok);

private:
    QString m_url, m_title, m_browser, m_outDir;
    int m_height;
    bool m_audioOnly, m_live;
    QProcess *m_proc = nullptr;
};
