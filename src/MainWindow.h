#pragma once
#include <QMainWindow>
#include <QHash>
#include <QPixmap>
#include "YtDlp.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QPushButton;
class QLabel;
class QSlider;
class QComboBox;
class QCheckBox;
class QNetworkAccessManager;
class VlcPlayer;

enum RowKind { KindVideo, KindPlaylist, KindChannel };

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private:
    // pages
    QWidget *buildMainPage();
    QWidget *buildPlaylistPage();
    QWidget *buildChannelPage();
    QWidget *buildConfigPage();
    void applyTheme();

    void showFormats(const QVector<FormatOption> &formats);
    void addDownload(const QString &url, const QString &title, int height,
                     bool audioOnly, bool live);
    void playUrl(const QString &url, const QString &title);
    void setStatus(const QString &text);
    QString pickDownloadDir(); // honors "always ask" setting; empty = cancelled
    QWidget *makeResultRow(const SearchResult &r, RowKind kind);
    void fillList(QListWidget *list, const QVector<SearchResult> &results,
                  RowKind kind, bool append);
    void loadFavorites();
    void saveFavorites();

    YtDlp *m_yt;
    QStackedWidget *m_pages;
    QNetworkAccessManager *m_nam;
    QHash<QString, QPixmap> m_thumbCache;

    // main page
    VlcPlayer *m_player;
    QSlider *m_seek;
    QLabel *m_timeLabel;
    QPushButton *m_playPauseBtn;
    QLineEdit *m_searchEdit;
    QListWidget *m_results;
    QListWidget *m_qualityList;
    QListWidget *m_downloadsList;
    QPushButton *m_loginBtn;
    QString m_loginBrowser;
    QLabel *m_status;
    QLabel *m_selTitle;
    QLabel *m_qualityBadge;

    // playlist page
    QLineEdit *m_plSearchEdit;
    QListWidget *m_plResults;
    QListWidget *m_plItems;
    QLabel *m_plTitle;
    QString m_plCurrentUrl;

    // channels page
    QLineEdit *m_chSearchEdit;
    QListWidget *m_chResults;
    QListWidget *m_chFavorites;
    QListWidget *m_chVideos;
    QLabel *m_chTitle;
    QComboBox *m_chSortCombo;
    QString m_chSelUrl, m_chSelName; // channel currently highlighted (for favoriting)

    // streaming search state (results appear live; no paging)
    SearchStream *m_vStream = nullptr;   // video search
    SearchStream *m_pStream = nullptr;   // playlist search
    SearchStream *m_cStream = nullptr;   // channel search
    SearchStream *m_chvStream = nullptr; // channel videos
    int m_vCount = 0, m_pCount = 0, m_cCount = 0;

    // channel videos: collected set + client-side sort/render
    QVector<SearchResult> m_chAllVideos;
    int m_chSort = 0;                 // 0 recent, 1 most viewed, 2 oldest
    bool m_chRenderPending = false;
    void renderChannelVideos();       // rebuild list from m_chAllVideos, sorted

    // selection state
    QString m_selUrl;
    QString m_selName;
    bool m_seeking = false;
};
