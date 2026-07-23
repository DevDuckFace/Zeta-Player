#include "YtDlp.h"
#include "Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

// Suppress the console window that Windows would otherwise flash for every
// yt-dlp.exe subprocess. Leaves the app as a single GUI window.
static void hideConsole(QProcess *p) {
#ifdef Q_OS_WIN
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    Q_UNUSED(p)
#endif
}

static QString fmtDuration(double secs) {
    if (secs <= 0) return QString();
    int s = int(secs);
    int h = s / 3600, m = (s % 3600) / 60, ss = s % 60;
    if (h > 0) return QString::asprintf("%d:%02d:%02d", h, m, ss);
    return QString::asprintf("%d:%02d", m, ss);
}

static QString fmtCount(double n) {
    if (n >= 1e6) return QString::asprintf("%.1fM", n / 1e6);
    if (n >= 1e3) return QString::asprintf("%.0fK", n / 1e3);
    return QString::number(qint64(n));
}

YtDlp::YtDlp(QObject *parent) : QObject(parent) {}

QString YtDlp::exePath() {
    return QCoreApplication::applicationDirPath() + "/yt-dlp.exe";
}

QString YtDlp::downloadsDir() {
    QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/ProjetoZeta";
    QString d = appSettings().value("downloadDir", def).toString();
    QDir().mkpath(d);
    return d;
}

QStringList YtDlp::baseArgs() const {
    QStringList a{"--no-warnings", "--no-check-certificates"};
    if (!m_browser.isEmpty())
        a << "--cookies-from-browser" << m_browser;
    return a;
}

QProcess *YtDlp::runJson(const QStringList &args, std::function<void(const QJsonDocument &)> onDone) {
    auto *p = new QProcess(this);
    hideConsole(p);
    p->setProgram(exePath());
    p->setArguments(args);
    connect(p, &QProcess::finished, this, [this, p, onDone](int code, QProcess::ExitStatus) {
        QByteArray out = p->readAllStandardOutput();
        QByteArray err = p->readAllStandardError();
        p->deleteLater();
        if (out.trimmed().isEmpty()) {
            emit error(code == 0 ? T("No results.", "Nenhum resultado.")
                                 : QString::fromLocal8Bit(err).trimmed());
            return;
        }
        onDone(QJsonDocument::fromJson(out));
    });
    p->start();
    return p;
}

static SearchResult parseEntry(const QJsonObject &o, bool channels) {
    SearchResult r;
    r.id = o.value("id").toString();
    r.title = o.value("title").toString();
    if (r.title.isEmpty()) r.title = o.value("channel").toString();
    r.url = o.value("url").toString();
    if (r.url.isEmpty()) r.url = o.value("webpage_url").toString();
    const QJsonArray thumbs = o.value("thumbnails").toArray();

    if (channels) {
        double followers = o.value("channel_follower_count").toDouble();
        if (followers > 0)
            r.uploader = fmtCount(followers) + T(" subscribers", " inscritos");
        // Channel avatars are JPEG but come protocol-relative (//host/..),
        // which QNetworkRequest can't load without a scheme.
        if (!thumbs.isEmpty()) {
            QString t = thumbs.last().toObject().value("url").toString();
            if (t.startsWith("//")) t = "https:" + t;
            r.thumbUrl = t;
        }
        return r;
    }

    r.uploader = o.value("uploader").toString();
    if (r.uploader.isEmpty()) r.uploader = o.value("channel").toString();
    r.duration = fmtDuration(o.value("duration").toDouble());
    if (o.contains("playlist_count"))
        r.itemCount = o.value("playlist_count").toInt(-1);
    if (o.contains("view_count") && !o.value("view_count").isNull())
        r.viewCount = qint64(o.value("view_count").toDouble());
    // YouTube serves the sqp-parametrized *.jpg thumbnails as WebP, which Qt
    // can't decode without the webp plugin. Rebuild a plain JPEG URL from the
    // video id instead. The id comes from the /vi/<id>/ path of any thumbnail
    // (works for playlist covers too, where r.id is the playlist id).
    QString vid;
    if (!thumbs.isEmpty()) {
        static const QRegularExpression viRe("/vi/([^/]+)/");
        auto m = viRe.match(thumbs.last().toObject().value("url").toString());
        if (m.hasMatch())
            vid = m.captured(1);
    }
    if (vid.isEmpty() && r.itemCount < 0 && r.id.length() == 11)
        vid = r.id; // plain video result
    if (!vid.isEmpty())
        r.thumbUrl = "https://i.ytimg.com/vi/" + vid + "/hqdefault.jpg";
    return r;
}

static QVector<SearchResult> parseEntries(const QJsonDocument &doc, bool channels = false) {
    QVector<SearchResult> out;
    const QJsonArray entries = doc.object().value("entries").toArray();
    for (const auto &v : entries)
        out.push_back(parseEntry(v.toObject(), channels));
    return out;
}

// ---------------- SearchStream ----------------

SearchStream::SearchStream(const QStringList &args, bool channels, QObject *parent)
    : QObject(parent), m_channels(channels) {
    m_proc = new QProcess(this);
    hideConsole(m_proc);
    // Force unbuffered stdout so --print lines arrive one at a time (they'd
    // otherwise be block-buffered when piped, defeating the live streaming).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    m_proc->setProcessEnvironment(env);
    m_proc->setProgram(YtDlp::exePath());
    m_proc->setArguments(args);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_buf += m_proc->readAllStandardOutput();
        QVector<SearchResult> batch;
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            QByteArray line = m_buf.left(nl);
            m_buf.remove(0, nl + 1);
            line = line.trimmed();
            if (line.isEmpty()) continue;
            QJsonParseError err;
            QJsonDocument d = QJsonDocument::fromJson(line, &err);
            if (err.error == QJsonParseError::NoError && d.isObject())
                batch.push_back(parseEntry(d.object(), m_channels));
        }
        if (!batch.isEmpty())
            emit chunk(batch);
    });
    connect(m_proc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        emit finished();
    });
    m_proc->start();
}

void SearchStream::stop() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect();
        m_proc->kill();
    }
}

static QString resultsUrl(const QString &query) {
    return "https://www.youtube.com/results?search_query=" +
           QString::fromUtf8(QUrl::toPercentEncoding(query));
}

SearchStream *YtDlp::streamVideos(const QString &query) {
    QStringList a = baseArgs();
    a << "--flat-playlist" << "--playlist-end" << "150"
      << "--print" << "%(.{id,title,url,duration,channel,view_count})j" << resultsUrl(query);
    return new SearchStream(a, false, this);
}

SearchStream *YtDlp::streamPlaylists(const QString &query) {
    QStringList a = baseArgs();
    a << "--flat-playlist" << "--playlist-end" << "150"
      << "--print" << "%(.{id,title,url,channel,playlist_count,thumbnails})j"
      << resultsUrl(query) + "&sp=EgIQAw%253D%253D";
    return new SearchStream(a, false, this);
}

SearchStream *YtDlp::streamChannels(const QString &query) {
    QStringList a = baseArgs();
    a << "--flat-playlist" << "--playlist-end" << "150"
      << "--print" << "%(.{id,title,url,channel_follower_count,thumbnails})j"
      << resultsUrl(query) + "&sp=EgIQAg%253D%253D";
    return new SearchStream(a, true, this);
}

SearchStream *YtDlp::streamChannelVideos(const QString &channelUrl) {
    QString u = channelUrl;
    if (!u.contains("/videos"))
        u += "/videos";
    QStringList a = baseArgs();
    a << "--flat-playlist" << "--playlist-end" << QString::number(kMaxItems)
      << "--print" << "%(.{id,title,url,duration,channel,view_count})j" << u;
    return new SearchStream(a, false, this);
}

void YtDlp::fetchPlaylistItems(const QString &playlistUrl) {
    runJson(baseArgs() << "--flat-playlist" << "-J" << playlistUrl,
            [this](const QJsonDocument &d) { emit playlistItems(parseEntries(d)); });
}

void YtDlp::fetchFormats(const QString &videoUrl) {
    runJson(baseArgs() << "-J" << videoUrl, [this, videoUrl](const QJsonDocument &d) {
        QSet<int> heights;
        const QJsonArray fmts = d.object().value("formats").toArray();
        for (const auto &v : fmts) {
            const QJsonObject o = v.toObject();
            int h = o.value("height").toInt(0);
            if (h >= 144 && o.value("vcodec").toString() != "none")
                heights.insert(h);
        }
        QList<int> sorted = heights.values();
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        QVector<FormatOption> opts;
        for (int h : sorted)
            opts.push_back({QString::number(h) + "p", h, false});
        opts.push_back({T("Audio only (MP3)", "Somente áudio (MP3)"), 0, true});
        emit formatsReady(videoUrl, opts);
    });
}

void YtDlp::resolveStreamUrl(const QString &videoUrl) {
    // Playback quality cap chosen in Settings (0 = maximum, else a height limit).
    int cap = appSettings().value("playbackQuality", 1080).toInt();
    QString fmt;
    if (cap <= 0)
        fmt = "bv*+ba/b";
    else
        // Prefer split video+audio (goes up to the real cap) over the muxed
        // "best" stream, which YouTube limits to ~360p.
        fmt = QString("bv*[height<=%1]+ba/b[height<=%1]/best[height<=%1]/b").arg(cap);
    auto *p = new QProcess(this);
    hideConsole(p);
    p->setProgram(exePath());
    p->setArguments(baseArgs() << "-f" << fmt << "--print" << "height:%(height)s"
                               << "-g" << videoUrl);
    connect(p, &QProcess::finished, this, [this, p, videoUrl](int code, QProcess::ExitStatus) {
        const QStringList lines = QString::fromUtf8(p->readAllStandardOutput())
                                      .split('\n', Qt::SkipEmptyParts);
        QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
        p->deleteLater();
        int height = 0;
        QStringList urls;
        for (const QString &ln : lines) {
            QString l = ln.trimmed();
            if (l.startsWith("height:")) height = l.mid(7).toInt();
            else if (l.startsWith("http")) urls << l;
        }
        if (code != 0 || urls.isEmpty()) {
            emit error(err.isEmpty() ? T("Could not get the stream.",
                                         "Não foi possível obter o stream.") : err);
            return;
        }
        emit streamUrlReady(videoUrl, urls[0], urls.size() > 1 ? urls[1] : QString(), height);
    });
    p->start();
}

// ---------------- DownloadJob ----------------

DownloadJob::DownloadJob(const QString &url, const QString &title, int height,
                         bool audioOnly, bool isLive, const QString &browser,
                         const QString &outDir, QObject *parent)
    : QObject(parent), m_url(url), m_title(title), m_browser(browser), m_outDir(outDir),
      m_height(height), m_audioOnly(audioOnly), m_live(isLive) {}

void DownloadJob::start() {
    m_proc = new QProcess(this);
    hideConsole(m_proc);
    m_proc->setProgram(YtDlp::exePath());
    QStringList args{"--no-warnings", "--newline", "--progress",
                     "-o", m_outDir + "/%(title)s.%(ext)s"};
    if (!m_browser.isEmpty())
        args << "--cookies-from-browser" << m_browser;
    if (m_audioOnly) {
        args << "-x" << "--audio-format" << "mp3";
    } else if (m_height > 0) {
        QString h = QString::number(m_height);
        args << "-f" << QString("bv*[height<=%1]+ba/b[height<=%1]/b").arg(h)
             << "--merge-output-format" << "mp4";
    }
    if (m_live)
        args << "--hls-use-mpegts";
    args << m_url;
    m_proc->setArguments(args);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_proc->canReadLine()) {
            QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
            static const QRegularExpression re(
                R"(\[download\]\s+([\d.]+)%(?:.*?at\s+(\S+))?(?:.*?ETA\s+(\S+))?)");
            auto m = re.match(line);
            if (m.hasMatch())
                emit progress(m.captured(1).toDouble(), m.captured(2), m.captured(3));
            else if (!line.isEmpty())
                emit statusText(line);
        }
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        // Stopping a live recording kills the process; the file is still kept.
        emit finished(code == 0 || m_live);
    });
    m_proc->start();
    emit statusText(m_live ? T("Recording live stream...", "Gravando transmissão...")
                           : T("Starting download...", "Iniciando download..."));
}

void DownloadJob::stop() {
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
}
