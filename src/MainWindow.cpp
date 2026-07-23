#include "MainWindow.h"
#include "VlcPlayer.h"
#include "Settings.h"
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QBoxLayout>
#include <QTimer>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QStyle>
#include <algorithm>

static QString elide(const QString &s, int n = 60) {
    return s.length() > n ? s.left(n - 1) + "…" : s;
}

static QString fmtViews(qint64 n) {
    if (n >= 1000000) return QString::asprintf("%.1fM", n / 1e6);
    if (n >= 1000) return QString::asprintf("%.0fK", n / 1e3);
    return QString::number(n);
}

// Reads the user's default https handler from the registry and maps it to the
// browser name yt-dlp expects for --cookies-from-browser.
static QString detectDefaultBrowser() {
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell\\Associations"
                "\\UrlAssociations\\https\\UserChoice", QSettings::NativeFormat);
    QString id = s.value("ProgId").toString().toLower();
    if (id.contains("brave")) return "brave";
    if (id.contains("edge") || id.contains("msedge")) return "edge";
    if (id.contains("firefox") || id.contains("mozilla")) return "firefox";
    if (id.contains("opera")) return "opera";
    if (id.contains("vivaldi")) return "vivaldi";
    if (id.contains("chrome")) return "chrome";
    return QString();
}

MainWindow::MainWindow() {
    setWindowTitle("Zeta Player");
    setWindowIcon(QIcon(":/icon_256.png"));
    resize(1280, 780);
    m_yt = new YtDlp(this);
    m_nam = new QNetworkAccessManager(this);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // top navigation bar
    auto *nav = new QWidget;
    nav->setObjectName("navbar");
    auto *navLay = new QHBoxLayout(nav);
    navLay->setContentsMargins(18, 10, 18, 10);
    auto *logo = new QLabel("⚡ Zeta Player");
    logo->setObjectName("logo");
    auto *btnMain = new QPushButton(T("Videos", "Vídeos"));
    auto *btnPl = new QPushButton("Playlists");
    auto *btnCh = new QPushButton(T("Channels", "Canais"));
    auto *btnCfg = new QPushButton(T("Settings", "Config"));
    for (auto *b : {btnMain, btnPl, btnCh, btnCfg}) {
        b->setObjectName("navBtn");
        b->setCheckable(true);
    }
    btnMain->setChecked(true);
    navLay->addWidget(logo);
    navLay->addSpacing(24);
    navLay->addWidget(btnMain);
    navLay->addWidget(btnPl);
    navLay->addWidget(btnCh);
    navLay->addWidget(btnCfg);
    navLay->addStretch();
    m_status = new QLabel(T("Ready", "Pronto"));
    m_status->setObjectName("statusLabel");
    navLay->addWidget(m_status);
    root->addWidget(nav);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildMainPage());     // 0
    m_pages->addWidget(buildPlaylistPage()); // 1
    m_pages->addWidget(buildChannelPage());  // 2
    m_pages->addWidget(buildConfigPage());   // 3
    root->addWidget(m_pages, 1);
    setCentralWidget(central);

    QList<QPushButton *> navBtns{btnMain, btnPl, btnCh, btnCfg};
    auto switchTo = [this, navBtns](int idx) {
        m_pages->setCurrentIndex(idx);
        for (int i = 0; i < navBtns.size(); ++i)
            navBtns[i]->setChecked(i == idx);
    };
    for (int i = 0; i < navBtns.size(); ++i)
        connect(navBtns[i], &QPushButton::clicked, this, [switchTo, i] { switchTo(i); });

    connect(m_yt, &YtDlp::error, this, [this](const QString &e) {
        setStatus(T("Error: %1", "Erro: %1").arg(elide(e, 90)));
    });

    loadFavorites();

    applyTheme();
}

QWidget *MainWindow::makeResultRow(const SearchResult &r, RowKind kind) {
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(10);

    auto *thumb = new QLabel;
    thumb->setObjectName(kind == KindChannel ? "avatar" : "thumb");
    if (kind == KindChannel)
        thumb->setFixedSize(54, 54);
    else
        thumb->setFixedSize(96, 54);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setText(kind == KindChannel ? "◉" : (kind == KindPlaylist ? "≡" : "▶"));
    lay->addWidget(thumb);

    const QSize tsz = thumb->size();
    if (!r.thumbUrl.isEmpty()) {
        if (m_thumbCache.contains(r.thumbUrl)) {
            thumb->setPixmap(m_thumbCache.value(r.thumbUrl));
        } else {
            QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(r.thumbUrl)));
            QString url = r.thumbUrl;
            connect(reply, &QNetworkReply::finished, thumb, [this, reply, thumb, url, tsz] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError)
                    return;
                QPixmap px;
                if (px.loadFromData(reply->readAll())) {
                    px = px.scaled(tsz, Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
                    m_thumbCache.insert(url, px);
                    thumb->setPixmap(px);
                }
            });
        }
    }

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *title = new QLabel(elide(r.title, 64));
    title->setObjectName("rowTitle");
    title->setWordWrap(true);
    QString sub = r.uploader;
    if (kind == KindPlaylist && r.itemCount > 0)
        sub += (sub.isEmpty() ? "" : "  •  ") + T("%1 videos", "%1 vídeos").arg(r.itemCount);
    if (kind == KindVideo && !r.duration.isEmpty())
        sub += (sub.isEmpty() ? "" : "  •  ") + r.duration;
    if (kind == KindVideo && r.viewCount >= 0)
        sub += (sub.isEmpty() ? "" : "  •  ") + T("%1 views", "%1 views").arg(fmtViews(r.viewCount));
    auto *subL = new QLabel(sub);
    subL->setObjectName("rowSub");
    texts->addWidget(title);
    texts->addWidget(subL);
    texts->addStretch();
    lay->addLayout(texts, 1);
    return w;
}

void MainWindow::fillList(QListWidget *list, const QVector<SearchResult> &results,
                          RowKind kind, bool append) {
    if (!append)
        list->clear();
    for (const auto &r : results) {
        auto *item = new QListWidgetItem(list);
        item->setData(Qt::UserRole, r.url);
        item->setData(Qt::UserRole + 1, r.title);
        QWidget *row = makeResultRow(r, kind);
        item->setSizeHint(QSize(row->sizeHint().width(), qMax(row->sizeHint().height(), 66)));
        list->setItemWidget(item, row);
    }
}

QWidget *MainWindow::buildMainPage() {
    auto *page = new QWidget;
    auto *lay = new QHBoxLayout(page);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(16);

    // ---- left column: player + actions ----
    auto *left = new QVBoxLayout;
    left->setSpacing(10);

    m_player = new VlcPlayer;
    m_player->setObjectName("playerSurface");
    left->addWidget(m_player, 1);

    auto *titleRow = new QHBoxLayout;
    m_selTitle = new QLabel(T("No video selected", "Nenhum vídeo selecionado"));
    m_selTitle->setObjectName("selTitle");
    m_qualityBadge = new QLabel;
    m_qualityBadge->setObjectName("qualityBadge");
    m_qualityBadge->hide();
    titleRow->addWidget(m_selTitle, 1);
    titleRow->addWidget(m_qualityBadge);
    left->addLayout(titleRow);

    // player controls
    auto *controls = new QHBoxLayout;
    m_playPauseBtn = new QPushButton("⏸");
    m_playPauseBtn->setObjectName("iconBtn");
    auto *stopBtn = new QPushButton("⏹");
    stopBtn->setObjectName("iconBtn");
    m_seek = new QSlider(Qt::Horizontal);
    m_timeLabel = new QLabel("0:00 / 0:00");
    m_timeLabel->setObjectName("timeLabel");
    auto *vol = new QSlider(Qt::Horizontal);
    vol->setFixedWidth(90);
    vol->setRange(0, 100);
    vol->setValue(80);
    controls->addWidget(m_playPauseBtn);
    controls->addWidget(stopBtn);
    controls->addWidget(m_seek, 1);
    controls->addWidget(m_timeLabel);
    controls->addWidget(new QLabel("🔊"));
    controls->addWidget(vol);
    left->addLayout(controls);

    connect(m_playPauseBtn, &QPushButton::clicked, m_player, &VlcPlayer::togglePause);
    connect(stopBtn, &QPushButton::clicked, m_player, &VlcPlayer::stop);
    connect(vol, &QSlider::valueChanged, m_player, &VlcPlayer::setVolume);
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        if (m_player->lengthMs() > 0)
            m_player->setPositionMs(qint64(m_seek->value()) * m_player->lengthMs() / 1000);
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        qint64 len = m_player->lengthMs(), pos = m_player->positionMs();
        if (!m_seeking && len > 0) {
            m_seek->setRange(0, 1000);
            m_seek->setValue(int(pos * 1000 / len));
        }
        auto fmt = [](qint64 ms) {
            qint64 s = ms / 1000;
            return s >= 3600 ? QString::asprintf("%lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60)
                             : QString::asprintf("%lld:%02lld", s / 60, s % 60);
        };
        m_timeLabel->setText(fmt(pos) + " / " + fmt(len));
        m_playPauseBtn->setText(m_player->isPlaying() ? "⏸" : "▶");
    });
    timer->start(500);

    // action row: Baixar / Gravar / Login
    auto *actions = new QHBoxLayout;
    auto *downloadBtn = new QPushButton(T("⬇  Download", "⬇  Baixar"));
    downloadBtn->setObjectName("accentBtn");
    auto *recordBtn = new QPushButton(T("⏺  Record live", "⏺  Gravar live"));
    recordBtn->setObjectName("recordBtn");
    m_loginBtn = new QPushButton(T("🔑  Log in", "🔑  Login"));
    m_loginBtn->setObjectName("loginBtn");
    actions->addWidget(downloadBtn);
    actions->addWidget(recordBtn);
    actions->addWidget(m_loginBtn);
    actions->addStretch();
    left->addLayout(actions);
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        QString br = detectDefaultBrowser();
        if (br.isEmpty()) {
            setStatus(T("Could not detect default browser — using Chrome cookies.",
                        "Navegador padrão não detectado — usando cookies do Chrome."));
            br = "chrome";
        }
        m_loginBrowser = br;
        m_yt->setBrowserForCookies(br);
        QDesktopServices::openUrl(QUrl(
            "https://accounts.google.com/ServiceLogin?continue=https://www.youtube.com/"));
        QString nice = br.left(1).toUpper() + br.mid(1);
        m_loginBtn->setText("✓ " + T("Logged in", "Logado") + " (" + nice + ")");
        m_loginBtn->setObjectName("loggedBtn");
        m_loginBtn->setStyleSheet("");            // re-evaluate against new objectName
        m_loginBtn->style()->unpolish(m_loginBtn);
        m_loginBtn->style()->polish(m_loginBtn);
        setStatus(T("Sign in in your browser — cookies are used automatically after.",
                    "Faça login no navegador — os cookies serão usados automaticamente."));
    });

    // bottom: qualities + downloads
    auto *bottom = new QHBoxLayout;
    auto *qBox = new QVBoxLayout;
    auto *qLabel = new QLabel(T("Available qualities", "Qualidades disponíveis"));
    qLabel->setObjectName("sectionLabel");
    m_qualityList = new QListWidget;
    m_qualityList->setFixedHeight(140);
    qBox->addWidget(qLabel);
    qBox->addWidget(m_qualityList);
    auto *dBox = new QVBoxLayout;
    auto *dLabel = new QLabel(T("Downloads / Recordings", "Downloads / Gravações"));
    dLabel->setObjectName("sectionLabel");
    m_downloadsList = new QListWidget;
    m_downloadsList->setFixedHeight(140);
    dBox->addWidget(dLabel);
    dBox->addWidget(m_downloadsList);
    bottom->addLayout(qBox, 1);
    bottom->addLayout(dBox, 2);
    left->addLayout(bottom);

    connect(downloadBtn, &QPushButton::clicked, this, [this] {
        if (m_selUrl.isEmpty()) {
            setStatus(T("Select a video first.", "Selecione um vídeo primeiro."));
            return;
        }
        auto *it = m_qualityList->currentItem();
        int h = it ? it->data(Qt::UserRole).toInt() : 0;
        bool audio = it && it->data(Qt::UserRole + 1).toBool();
        addDownload(m_selUrl, m_selName, h, audio, false);
    });
    connect(recordBtn, &QPushButton::clicked, this, [this] {
        if (m_selUrl.isEmpty()) {
            setStatus(T("Select a live stream first.", "Selecione uma live primeiro."));
            return;
        }
        addDownload(m_selUrl, m_selName, 0, false, true);
    });

    // ---- right column: search ----
    auto *right = new QVBoxLayout;
    right->setSpacing(10);
    auto *searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(T("Search videos...", "Buscar vídeos..."));
    auto *searchBtn = new QPushButton("🔍");
    searchBtn->setObjectName("iconBtn");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(searchBtn);
    right->addLayout(searchRow);
    m_results = new QListWidget;
    right->addWidget(m_results, 1);

    auto doSearch = [this] {
        QString q = m_searchEdit->text().trimmed();
        if (q.isEmpty()) return;
        setStatus(T("Searching...", "Buscando..."));
        m_results->clear();
        m_vCount = 0;
        if (m_vStream) { m_vStream->stop(); m_vStream->deleteLater(); }
        m_vStream = m_yt->streamVideos(q);
        connect(m_vStream, &SearchStream::chunk, this, [this](const QVector<SearchResult> &r) {
            fillList(m_results, r, KindVideo, true);
            m_vCount += r.size();
            setStatus(T("%1 results…", "%1 resultados…").arg(m_vCount));
        });
        connect(m_vStream, &SearchStream::finished, this, [this] {
            setStatus(T("%1 results", "%1 resultados").arg(m_vCount));
        });
    };
    connect(searchBtn, &QPushButton::clicked, this, doSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, doSearch);

    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        m_selUrl = it->data(Qt::UserRole).toString();
        m_selName = it->data(Qt::UserRole + 1).toString();
        m_selTitle->setText(elide(m_selName, 80));
        m_qualityList->clear();
        setStatus(T("Loading qualities...", "Carregando qualidades..."));
        m_yt->fetchFormats(m_selUrl);
    });
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        playUrl(it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString());
    });

    connect(m_yt, &YtDlp::formatsReady, this,
            [this](const QString &url, const QVector<FormatOption> &f) {
        if (url != m_selUrl) return;
        showFormats(f);
        setStatus(T("Ready — double-click plays, Download saves.",
                    "Pronto — duplo clique reproduz, Baixar salva."));
    });
    connect(m_yt, &YtDlp::streamUrlReady, this,
            [this](const QString &, const QString &video, const QString &audio, int height) {
        m_player->play(video, audio);
        if (height > 0) {
            m_qualityBadge->setText(QString::number(height) + "p");
            m_qualityBadge->show();
        } else {
            m_qualityBadge->hide();
        }
        setStatus(T("Playing: %1", "Reproduzindo: %1").arg(elide(m_selName, 60)));
    });

    lay->addLayout(left, 5);
    lay->addLayout(right, 3);
    return page;
}

QWidget *MainWindow::buildPlaylistPage() {
    auto *page = new QWidget;
    auto *lay = new QHBoxLayout(page);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(16);

    // left: playlist search
    auto *left = new QVBoxLayout;
    auto *searchRow = new QHBoxLayout;
    m_plSearchEdit = new QLineEdit;
    m_plSearchEdit->setPlaceholderText(T("Search playlists...", "Buscar playlists..."));
    auto *searchBtn = new QPushButton("🔍");
    searchBtn->setObjectName("iconBtn");
    searchRow->addWidget(m_plSearchEdit, 1);
    searchRow->addWidget(searchBtn);
    left->addLayout(searchRow);
    m_plResults = new QListWidget;
    left->addWidget(m_plResults, 1);

    auto doSearch = [this] {
        QString q = m_plSearchEdit->text().trimmed();
        if (q.isEmpty()) return;
        setStatus(T("Searching playlists...", "Buscando playlists..."));
        m_plResults->clear();
        m_pCount = 0;
        if (m_pStream) { m_pStream->stop(); m_pStream->deleteLater(); }
        m_pStream = m_yt->streamPlaylists(q);
        connect(m_pStream, &SearchStream::chunk, this, [this](const QVector<SearchResult> &r) {
            fillList(m_plResults, r, KindPlaylist, true);
            m_pCount += r.size();
            setStatus(T("%1 playlists…", "%1 playlists…").arg(m_pCount));
        });
        connect(m_pStream, &SearchStream::finished, this, [this] {
            setStatus(T("%1 playlists", "%1 playlists").arg(m_pCount));
        });
    };
    connect(searchBtn, &QPushButton::clicked, this, doSearch);
    connect(m_plSearchEdit, &QLineEdit::returnPressed, this, doSearch);

    // right: playlist contents
    auto *right = new QVBoxLayout;
    m_plTitle = new QLabel(T("Pick a playlist on the left", "Escolha uma playlist à esquerda"));
    m_plTitle->setObjectName("selTitle");
    right->addWidget(m_plTitle);
    m_plItems = new QListWidget;
    m_plItems->setSelectionMode(QAbstractItemView::ExtendedSelection);
    right->addWidget(m_plItems, 1);

    auto *btnRow = new QHBoxLayout;
    auto *plPlay = new QPushButton(T("▶  Watch selected", "▶  Ver selecionado"));
    auto *plDownloadSel = new QPushButton(T("⬇  Download selected", "⬇  Baixar selecionados"));
    plDownloadSel->setObjectName("accentBtn");
    auto *plDownloadAll = new QPushButton(T("⬇  Download entire playlist",
                                            "⬇  Baixar playlist completa"));
    btnRow->addWidget(plPlay);
    btnRow->addWidget(plDownloadSel);
    btnRow->addWidget(plDownloadAll);
    right->addLayout(btnRow);

    connect(m_plResults, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        m_plCurrentUrl = it->data(Qt::UserRole).toString();
        m_plTitle->setText(elide(it->data(Qt::UserRole + 1).toString(), 80));
        m_plItems->clear();
        setStatus(T("Loading playlist videos...", "Carregando vídeos da playlist..."));
        m_yt->fetchPlaylistItems(m_plCurrentUrl);
    });
    connect(m_yt, &YtDlp::playlistItems, this, [this](const QVector<SearchResult> &r) {
        fillList(m_plItems, r, KindVideo, false);
        setStatus(T("%1 videos in playlist", "%1 vídeos na playlist").arg(r.size()));
    });
    connect(m_plItems, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_pages->setCurrentIndex(0);
        playUrl(it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString());
    });
    connect(plPlay, &QPushButton::clicked, this, [this] {
        auto sel = m_plItems->selectedItems();
        if (sel.isEmpty()) { setStatus(T("Select a video.", "Selecione um vídeo.")); return; }
        m_pages->setCurrentIndex(0);
        playUrl(sel.first()->data(Qt::UserRole).toString(),
                sel.first()->data(Qt::UserRole + 1).toString());
    });
    connect(plDownloadSel, &QPushButton::clicked, this, [this] {
        auto sel = m_plItems->selectedItems();
        if (sel.isEmpty()) {
            setStatus(T("Select videos to download.", "Selecione vídeos para baixar."));
            return;
        }
        for (auto *it : sel)
            addDownload(it->data(Qt::UserRole).toString(),
                        it->data(Qt::UserRole + 1).toString(), 1080, false, false);
        m_pages->setCurrentIndex(0);
    });
    connect(plDownloadAll, &QPushButton::clicked, this, [this] {
        if (m_plCurrentUrl.isEmpty()) {
            setStatus(T("Pick a playlist.", "Escolha uma playlist."));
            return;
        }
        addDownload(m_plCurrentUrl, m_plTitle->text(), 1080, false, false);
        m_pages->setCurrentIndex(0);
    });

    lay->addLayout(left, 1);
    lay->addLayout(right, 1);
    return page;
}

QWidget *MainWindow::buildChannelPage() {
    auto *page = new QWidget;
    auto *lay = new QHBoxLayout(page);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(16);

    // ---- left column: search + results + favorites ----
    auto *left = new QVBoxLayout;
    left->setSpacing(8);

    auto *searchRow = new QHBoxLayout;
    m_chSearchEdit = new QLineEdit;
    m_chSearchEdit->setPlaceholderText(T("Search channels...", "Buscar canais..."));
    auto *searchBtn = new QPushButton("🔍");
    searchBtn->setObjectName("iconBtn");
    searchRow->addWidget(m_chSearchEdit, 1);
    searchRow->addWidget(searchBtn);
    left->addLayout(searchRow);

    auto *resLabel = new QLabel(T("Results", "Resultados"));
    resLabel->setObjectName("sectionLabel");
    left->addWidget(resLabel);
    m_chResults = new QListWidget;
    left->addWidget(m_chResults, 3);

    auto *favBtn = new QPushButton(T("★  Save to favorites", "★  Salvar nos favoritos"));
    favBtn->setObjectName("accentBtn");
    left->addWidget(favBtn);

    auto *favLabel = new QLabel(T("Favorites", "Favoritos"));
    favLabel->setObjectName("sectionLabel");
    left->addWidget(favLabel);
    m_chFavorites = new QListWidget;
    left->addWidget(m_chFavorites, 2);
    auto *unfavBtn = new QPushButton(T("✕  Remove favorite", "✕  Remover favorito"));
    unfavBtn->setObjectName("smallBtn");
    left->addWidget(unfavBtn);

    // ---- right column: videos of the selected channel ----
    auto *right = new QVBoxLayout;
    auto *titleRow = new QHBoxLayout;
    m_chTitle = new QLabel(T("Select a channel to see its videos",
                             "Selecione um canal para ver os vídeos"));
    m_chTitle->setObjectName("selTitle");
    titleRow->addWidget(m_chTitle, 1);
    auto *sortLabel = new QLabel(T("Order:", "Ordem:"));
    sortLabel->setObjectName("rowSub");
    m_chSortCombo = new QComboBox;
    m_chSortCombo->addItem(T("Most recent", "Mais recentes"));
    m_chSortCombo->addItem(T("Most viewed", "Mais vistos"));
    m_chSortCombo->addItem(T("Oldest", "Mais antigos"));
    titleRow->addWidget(sortLabel);
    titleRow->addWidget(m_chSortCombo);
    right->addLayout(titleRow);
    m_chVideos = new QListWidget;
    m_chVideos->setSelectionMode(QAbstractItemView::ExtendedSelection);
    right->addWidget(m_chVideos, 1);
    connect(m_chSortCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_chSort = i;
        renderChannelVideos();
    });

    auto *btnRow = new QHBoxLayout;
    auto *watchBtn = new QPushButton(T("▶  Watch selected", "▶  Ver selecionado"));
    auto *dlBtn = new QPushButton(T("⬇  Download selected", "⬇  Baixar selecionados"));
    dlBtn->setObjectName("accentBtn");
    btnRow->addWidget(watchBtn);
    btnRow->addWidget(dlBtn);
    btnRow->addStretch();
    right->addLayout(btnRow);

    // ---- channel search (streaming, unlimited) ----
    auto doSearch = [this] {
        QString q = m_chSearchEdit->text().trimmed();
        if (q.isEmpty()) return;
        setStatus(T("Searching channels...", "Buscando canais..."));
        m_chResults->clear();
        m_cCount = 0;
        if (m_cStream) { m_cStream->stop(); m_cStream->deleteLater(); }
        m_cStream = m_yt->streamChannels(q);
        connect(m_cStream, &SearchStream::chunk, this, [this](const QVector<SearchResult> &r) {
            fillList(m_chResults, r, KindChannel, true);
            m_cCount += r.size();
            setStatus(T("%1 channels…", "%1 canais…").arg(m_cCount));
        });
        connect(m_cStream, &SearchStream::finished, this, [this] {
            setStatus(T("%1 channels", "%1 canais").arg(m_cCount));
        });
    };
    connect(searchBtn, &QPushButton::clicked, this, doSearch);
    connect(m_chSearchEdit, &QLineEdit::returnPressed, this, doSearch);

    // clicking a channel (result or favorite) streams all of its videos
    auto openChannel = [this](QListWidgetItem *it) {
        m_chSelUrl = it->data(Qt::UserRole).toString();
        m_chSelName = it->data(Qt::UserRole + 1).toString();
        m_chTitle->setText(T("Videos — %1", "Vídeos — %1").arg(elide(m_chSelName, 50)));
        m_chVideos->clear();
        m_chAllVideos.clear();
        setStatus(T("Loading channel videos...", "Carregando vídeos do canal..."));
        if (m_chvStream) { m_chvStream->stop(); m_chvStream->deleteLater(); }
        m_chvStream = m_yt->streamChannelVideos(m_chSelUrl);
        connect(m_chvStream, &SearchStream::chunk, this, [this](const QVector<SearchResult> &r) {
            m_chAllVideos += r;
            if (m_chSort == 0) {
                // Most recent: stream is already newest-first — just append live.
                fillList(m_chVideos, r, KindVideo, true);
            } else if (!m_chRenderPending) {
                // Other orders need the whole set; re-render on a throttle.
                m_chRenderPending = true;
                QTimer::singleShot(500, this, [this] {
                    m_chRenderPending = false;
                    renderChannelVideos();
                });
            }
            setStatus(T("%1 videos…", "%1 vídeos…").arg(m_chAllVideos.size()));
        });
        connect(m_chvStream, &SearchStream::finished, this, [this] {
            renderChannelVideos();
            setStatus(T("%1 videos", "%1 vídeos").arg(m_chAllVideos.size()));
        });
    };
    connect(m_chResults, &QListWidget::itemClicked, this, openChannel);
    connect(m_chFavorites, &QListWidget::itemClicked, this, openChannel);

    // favorites
    connect(favBtn, &QPushButton::clicked, this, [this] {
        if (m_chSelUrl.isEmpty()) {
            setStatus(T("Select a channel first.", "Selecione um canal primeiro."));
            return;
        }
        for (int i = 0; i < m_chFavorites->count(); ++i)
            if (m_chFavorites->item(i)->data(Qt::UserRole).toString() == m_chSelUrl) {
                setStatus(T("Already in favorites.", "Já está nos favoritos."));
                return;
            }
        auto *item = new QListWidgetItem("★  " + m_chSelName, m_chFavorites);
        item->setData(Qt::UserRole, m_chSelUrl);
        item->setData(Qt::UserRole + 1, m_chSelName);
        saveFavorites();
        setStatus(T("Saved to favorites.", "Salvo nos favoritos."));
    });
    connect(unfavBtn, &QPushButton::clicked, this, [this] {
        auto *it = m_chFavorites->currentItem();
        if (!it) return;
        delete it;
        saveFavorites();
        setStatus(T("Removed from favorites.", "Removido dos favoritos."));
    });

    // watch / download recent videos
    connect(m_chVideos, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_pages->setCurrentIndex(0);
        playUrl(it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString());
    });
    connect(watchBtn, &QPushButton::clicked, this, [this] {
        auto sel = m_chVideos->selectedItems();
        if (sel.isEmpty()) { setStatus(T("Select a video.", "Selecione um vídeo.")); return; }
        m_pages->setCurrentIndex(0);
        playUrl(sel.first()->data(Qt::UserRole).toString(),
                sel.first()->data(Qt::UserRole + 1).toString());
    });
    connect(dlBtn, &QPushButton::clicked, this, [this] {
        auto sel = m_chVideos->selectedItems();
        if (sel.isEmpty()) {
            setStatus(T("Select videos to download.", "Selecione vídeos para baixar."));
            return;
        }
        for (auto *it : sel)
            addDownload(it->data(Qt::UserRole).toString(),
                        it->data(Qt::UserRole + 1).toString(), 1080, false, false);
    });

    lay->addLayout(left, 2);
    lay->addLayout(right, 3);
    return page;
}

void MainWindow::renderChannelVideos() {
    QVector<SearchResult> v = m_chAllVideos;
    if (m_chSort == 1) { // most viewed
        std::stable_sort(v.begin(), v.end(), [](const SearchResult &a, const SearchResult &b) {
            return a.viewCount > b.viewCount;
        });
    } else if (m_chSort == 2) { // oldest first (stream is newest-first)
        std::reverse(v.begin(), v.end());
    }
    int scroll = m_chVideos->verticalScrollBar()->value();
    fillList(m_chVideos, v, KindVideo, false);
    m_chVideos->verticalScrollBar()->setValue(scroll);
}

void MainWindow::loadFavorites() {
    const QStringList favs = appSettings().value("channelFavorites").toStringList();
    for (const QString &f : favs) {
        int sep = f.indexOf('\t');
        if (sep < 0) continue;
        QString name = f.left(sep), url = f.mid(sep + 1);
        auto *item = new QListWidgetItem("★  " + name, m_chFavorites);
        item->setData(Qt::UserRole, url);
        item->setData(Qt::UserRole + 1, name);
    }
}

void MainWindow::saveFavorites() {
    QStringList favs;
    for (int i = 0; i < m_chFavorites->count(); ++i) {
        auto *it = m_chFavorites->item(i);
        favs << it->data(Qt::UserRole + 1).toString() + "\t" + it->data(Qt::UserRole).toString();
    }
    appSettings().setValue("channelFavorites", favs);
}

QWidget *MainWindow::buildConfigPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(40, 30, 40, 30);
    outer->setSpacing(18);

    auto *dlHeader = new QLabel(T("Downloads", "Downloads"));
    dlHeader->setObjectName("sectionLabel");
    outer->addWidget(dlHeader);

    auto *dirRow = new QHBoxLayout;
    auto *dirLabel = new QLabel(T("Default download folder:", "Pasta padrão de download:"));
    auto *dirEdit = new QLineEdit(YtDlp::downloadsDir());
    dirEdit->setReadOnly(true);
    auto *browseBtn = new QPushButton(T("Browse...", "Escolher..."));
    dirRow->addWidget(dirLabel);
    dirRow->addWidget(dirEdit, 1);
    dirRow->addWidget(browseBtn);
    outer->addLayout(dirRow);

    auto *askCheck = new QCheckBox(T("Always ask where to save when downloading",
                                     "Sempre perguntar onde salvar ao baixar"));
    askCheck->setChecked(appSettings().value("alwaysAsk", false).toBool());
    outer->addWidget(askCheck);

    auto *pbHeader = new QLabel(T("Playback", "Reprodução"));
    pbHeader->setObjectName("sectionLabel");
    outer->addSpacing(10);
    outer->addWidget(pbHeader);

    auto *pbRow = new QHBoxLayout;
    auto *pbLabel = new QLabel(T("Play videos up to:", "Reproduzir vídeos até:"));
    auto *pbCombo = new QComboBox;
    pbCombo->addItem(T("Maximum quality", "Qualidade máxima"), 0);
    pbCombo->addItem("2160p (4K)", 2160);
    pbCombo->addItem("1440p (2K)", 1440);
    pbCombo->addItem("1080p (Full HD)", 1080);
    pbCombo->addItem("720p (HD)", 720);
    pbCombo->addItem("480p", 480);
    pbCombo->addItem("360p", 360);
    int curCap = appSettings().value("playbackQuality", 1080).toInt();
    int pbIdx = pbCombo->findData(curCap);
    pbCombo->setCurrentIndex(pbIdx >= 0 ? pbIdx : 0);
    auto *pbNote = new QLabel(T("Videos stream at this quality or the closest below it.",
                                "Os vídeos rodam nesta qualidade ou na mais próxima abaixo."));
    pbNote->setObjectName("rowSub");
    pbRow->addWidget(pbLabel);
    pbRow->addWidget(pbCombo);
    pbRow->addWidget(pbNote);
    pbRow->addStretch();
    outer->addLayout(pbRow);
    connect(pbCombo, &QComboBox::currentIndexChanged, this, [pbCombo](int i) {
        appSettings().setValue("playbackQuality", pbCombo->itemData(i).toInt());
    });

    auto *langHeader = new QLabel(T("Language", "Idioma"));
    langHeader->setObjectName("sectionLabel");
    outer->addSpacing(10);
    outer->addWidget(langHeader);

    auto *langRow = new QHBoxLayout;
    auto *langCombo = new QComboBox;
    langCombo->addItem("English", "en");
    langCombo->addItem("Português (Brasil)", "pt");
    langCombo->setCurrentIndex(appSettings().value("lang", "en").toString() == "pt" ? 1 : 0);
    auto *langNote = new QLabel(T("Restart the app to apply the language.",
                                  "Reinicie o app para aplicar o idioma."));
    langNote->setObjectName("rowSub");
    langRow->addWidget(langCombo);
    langRow->addWidget(langNote);
    langRow->addStretch();
    outer->addLayout(langRow);
    outer->addStretch();

    auto *creditsHeader = new QLabel(T("Credits", "Créditos"));
    creditsHeader->setObjectName("sectionLabel");
    outer->addWidget(creditsHeader);
    auto *credits = new QLabel(
        "Zeta Player — " + T("developed by", "desenvolvido por") +
        " <b>Dev DuckFace</b><br>"
        "<a style='color:#7c9bff; text-decoration:none;' "
        "href='https://github.com/DevDuckFace'>github.com/DevDuckFace</a>");
    credits->setObjectName("credits");
    credits->setTextFormat(Qt::RichText);
    credits->setOpenExternalLinks(true);
    credits->setTextInteractionFlags(Qt::TextBrowserInteraction);
    outer->addWidget(credits);

    connect(browseBtn, &QPushButton::clicked, this, [this, dirEdit] {
        QString d = QFileDialog::getExistingDirectory(
            this, T("Choose default download folder", "Escolha a pasta padrão de download"),
            dirEdit->text());
        if (!d.isEmpty()) {
            appSettings().setValue("downloadDir", d);
            dirEdit->setText(d);
        }
    });
    connect(askCheck, &QCheckBox::toggled, this, [](bool on) {
        appSettings().setValue("alwaysAsk", on);
    });
    connect(langCombo, &QComboBox::currentIndexChanged, this, [langCombo](int i) {
        appSettings().setValue("lang", langCombo->itemData(i).toString());
    });
    return page;
}

void MainWindow::showFormats(const QVector<FormatOption> &formats) {
    m_qualityList->clear();
    for (const auto &f : formats) {
        auto *item = new QListWidgetItem(f.label, m_qualityList);
        item->setData(Qt::UserRole, f.height);
        item->setData(Qt::UserRole + 1, f.audioOnly);
    }
    if (m_qualityList->count() > 0)
        m_qualityList->setCurrentRow(0);
}

QString MainWindow::pickDownloadDir() {
    QString dir = YtDlp::downloadsDir();
    if (!appSettings().value("alwaysAsk", false).toBool())
        return dir;
    QString d = QFileDialog::getExistingDirectory(
        this, T("Choose where to save this download", "Escolha onde salvar este download"), dir);
    return d; // empty = user cancelled
}

void MainWindow::addDownload(const QString &url, const QString &title, int height,
                             bool audioOnly, bool live) {
    QString outDir = pickDownloadDir();
    if (outDir.isEmpty()) {
        setStatus(T("Download cancelled.", "Download cancelado."));
        return;
    }
    auto *job = new DownloadJob(url, title, height, audioOnly, live,
                                m_loginBrowser, outDir, this);
    auto *item = new QListWidgetItem(m_downloadsList);
    auto *row = new QWidget;
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 4, 8, 4);
    auto *label = new QLabel((live ? "⏺ " : "⬇ ") + elide(title, 34));
    auto *bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setFixedWidth(140);
    bar->setTextVisible(true);
    auto *cancel = new QPushButton(live ? T("Stop", "Parar") : "✕");
    cancel->setObjectName("smallBtn");
    lay->addWidget(label, 1);
    lay->addWidget(bar);
    lay->addWidget(cancel);
    item->setSizeHint(row->sizeHint());
    m_downloadsList->setItemWidget(item, row);

    connect(job, &DownloadJob::progress, bar, [bar](double p, const QString &, const QString &) {
        bar->setValue(int(p));
    });
    connect(job, &DownloadJob::finished, this, [this, bar, cancel, label, job](bool ok) {
        bar->setValue(ok ? 100 : bar->value());
        label->setText((ok ? "✅ " : "❌ ") + label->text().mid(2));
        cancel->hide();
        setStatus(ok ? T("Done: %1", "Concluído: %1").arg(elide(job->title(), 50))
                     : T("Failed: %1", "Falhou: %1").arg(elide(job->title(), 50)));
        job->deleteLater();
    });
    connect(cancel, &QPushButton::clicked, job, &DownloadJob::stop);
    job->start();
    setStatus(live ? T("Recording live...", "Gravando live...")
                   : T("Downloading: %1", "Baixando: %1").arg(elide(title, 50)));
}

void MainWindow::playUrl(const QString &url, const QString &title) {
    m_selUrl = url;
    m_selName = title;
    m_selTitle->setText(elide(title, 80));
    m_qualityBadge->setText("…");
    m_qualityBadge->show();
    setStatus(T("Getting stream...", "Obtendo stream..."));
    m_yt->resolveStreamUrl(url);
}

void MainWindow::setStatus(const QString &text) {
    m_status->setText(text);
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(R"(
* { font-family: 'Segoe UI', sans-serif; font-size: 13px; color: #e8eaf0; }
QMainWindow, QWidget { background: #12141c; }
#navbar { background: #191c27; border-bottom: 1px solid #262a38; }
#logo { font-size: 17px; font-weight: 700; color: #7c9bff; }
#navBtn { background: transparent; border: none; padding: 8px 16px; border-radius: 8px;
          font-weight: 600; color: #9aa1b5; }
#navBtn:hover { background: #232736; color: #e8eaf0; }
#navBtn:checked { background: #2a3050; color: #7c9bff; }
#statusLabel { color: #8b93a7; font-size: 12px; }
#sectionLabel { color: #8b93a7; font-weight: 600; font-size: 12px; text-transform: uppercase; }
#selTitle { font-size: 15px; font-weight: 600; }
#qualityBadge { background: #3d5afe; color: #ffffff; border-radius: 6px;
                padding: 2px 9px; font-weight: 700; font-size: 12px; }
#timeLabel { color: #8b93a7; font-size: 12px; }
#rowTitle { font-weight: 600; background: transparent; }
#rowSub { color: #8b93a7; font-size: 12px; background: transparent; }
#credits { color: #c3c9db; font-size: 13px; }
#thumb { background: #0b0d13; border-radius: 6px; color: #3d4258; font-size: 18px; }
#avatar { background: #0b0d13; border-radius: 27px; color: #5b7bff; font-size: 22px; }
QLineEdit { background: #1b1e2b; border: 1px solid #2c3144; border-radius: 10px;
            padding: 9px 14px; selection-background-color: #3d5afe; }
QLineEdit:focus { border-color: #5b7bff; }
QListWidget { background: #171a25; border: 1px solid #262a38; border-radius: 12px; outline: none; }
QListWidget::item { border-radius: 8px; margin: 3px 5px; background: transparent; }
QListWidget::item:hover { background: #212536; }
QListWidget::item:selected { background: #2a3050; }
QListWidget::item:selected #rowTitle { color: #ffffff; }
QPushButton { background: #232736; border: 1px solid #2f3447; border-radius: 10px;
              padding: 9px 16px; font-weight: 600; }
QPushButton:hover { background: #2b3042; }
QPushButton:pressed { background: #1d2130; }
#accentBtn { background: #3d5afe; border: none; }
#accentBtn:hover { background: #536dfe; }
#recordBtn { background: #b3261e; border: none; }
#recordBtn:hover { background: #d33a31; }
#loginBtn { background: #2b3145; border: 1px solid #3a4160; }
#loginBtn:hover { background: #343b52; }
#loggedBtn { background: #1f7a34; border: none; color: #ffffff; }
#loggedBtn:hover { background: #248a3c; }
#iconBtn { padding: 8px 12px; font-size: 15px; }
#smallBtn { padding: 4px 10px; border-radius: 7px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px;
                       border: 1px solid #2f3447; background: #1b1e2b; }
QCheckBox::indicator:checked { background: #3d5afe; border-color: #3d5afe; }
QComboBox { background: #232736; border: 1px solid #2f3447; border-radius: 10px; padding: 8px 12px; }
QComboBox QAbstractItemView { background: #1b1e2b; border: 1px solid #2c3144;
                              selection-background-color: #2a3050; }
QSlider::groove:horizontal { height: 5px; background: #2c3144; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;
                             border-radius: 7px; background: #7c9bff; }
QSlider::sub-page:horizontal { background: #3d5afe; border-radius: 2px; }
QProgressBar { background: #2c3144; border: none; border-radius: 7px; height: 14px;
               text-align: center; font-size: 10px; }
QProgressBar::chunk { background: #3d5afe; border-radius: 7px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #333952; border-radius: 5px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
    )");
}
