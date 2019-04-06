#include "appconfig.h"

#include <QApplication>

#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QProcessEnvironment>
#include <QFont>
#include <QFontInfo>
#include <QMetaEnum>
#include <QFontDatabase>
#include <QSaveFile>

#include <QtDebug>

class AppConfig::Priv_t
{
public:
    QJsonObject global;
    QJsonObject local;
};

#define CFG_GLOBAL (priv->global)
#define CFG_LOCAL (priv->local)

const QJsonValue& valueOrDefault(const QJsonValue& v, const QJsonValue& d)
{
    return v.isUndefined()? d : v;
}

const QJsonObject& objectOrDefault(const QJsonObject& v, const QJsonObject& d)
{
    return v.empty()? d : v;
}

static QByteArray readEntireFile(const QString& path, const QByteArray& ifFail = QByteArray())
{
    QFile f(path);
    if (f.open(QFile::ReadOnly))
        return QTextStream(&f).readAll().toUtf8();
    else
        return ifFail;
}

static bool writeEntireFile(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QFile::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

static QString globalConfigFilePath() { return QDir::home().absoluteFilePath(".embedded_ide-config.json"); }

const QString DEFAULT_GLOBAL_RES = ":/default-global.json";
const QString DEFAULT_LOCAL_RES = ":/default-local.json";
const QString DEFAULT_LOCAL_TEMPLATE =
#ifdef Q_OS_UNIX
        "../share/embedded-ide/embedded-ide.hardconf";
#else
        "default-local.json";
#endif

static QString defaultLocalTemplateConfig()
{
    return QDir(QApplication::applicationDirPath()).absoluteFilePath(DEFAULT_LOCAL_TEMPLATE);
}

static void addResourcesFont()
{
    for(const auto& fontPath: QDir(":/fonts/").entryInfoList({ "*.ttf" }))
        QFontDatabase::addApplicationFont(fontPath.absoluteFilePath());
}
/*
static bool completeToLeft(QJsonObject& a, const QJsonObject& b)
{
    bool hasReplace = false;
    for(const auto& k: b.keys()) {
        auto v = a.value(k);
        if (v.isUndefined() || v.isNull()) {
            v = b.value(k);
            hasReplace = true;
        }
        if (v.isObject()) {
            auto o = v.toObject();
            hasReplace |= completeToLeft(o, b.value(k).toObject());
            v = o;
        }
        a.insert(k, v);
    }
    return hasReplace;
}
*/

static bool completeToLeft(QJsonObject& left, const QJsonObject& right)
{
    bool modify = false;
    for(const auto& k: right.keys()) {
        if (left.contains(k)) {
            auto obj = left[k].toObject();
            if (completeToLeft(obj, left[k].toObject())) {
                modify = true;
                left.insert(k, obj);
            }
        } else {
            modify = true;
            left.insert(k, right[k]);
        }
    }
    return modify;
}

AppConfig::AppConfig() : QObject(QApplication::instance()), priv(new Priv_t)
{
    adjustEnv();
    load();
    adjustEnv();
}

AppConfig::~AppConfig()
{
    delete priv;
}

AppConfig &AppConfig::instance()
{
    static AppConfig *singleton = nullptr;
    if (!singleton)
        singleton = new AppConfig;
    return *singleton;
}

void AppConfig::adjustEnv()
{
    qputenv("APPLICATION_DIR_PATH", QApplication::applicationDirPath().toLocal8Bit());
    qputenv("APPLICATION_FILE_PATH", QApplication::applicationFilePath().toLocal8Bit());
    qputenv("WORKSPACE_PATH", workspacePath().toLocal8Bit());
    qputenv("WORKSPACE_PROJECT_PATH", projectsPath().toLocal8Bit());
    qputenv("WORKSPACE_TEMPLATE_PATH", templatesPath().toLocal8Bit());
    qputenv("WORKSPACE_CONFIG_FILE", localConfigFilePath().toLocal8Bit());

#ifdef Q_OS_WIN
    QChar PATH_SEP = ';';
#else
    QChar PATH_SEP = ':';
#endif
    auto env = QProcessEnvironment::systemEnvironment();
    auto old = env.value("PATH");
    auto extras = additionalPaths().join(PATH_SEP);
    auto path = QString("%1%2%3").arg(extras).arg(PATH_SEP).arg(old);
    qputenv("PATH", path.toLocal8Bit());

    addResourcesFont();
}

QString AppConfig::replaceWithEnv(const QString &str)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString copy(str);
    for(const auto& k: env.keys())
        copy.replace(QString("${%1}").arg(k), env.value(k));
    return copy;
}

QByteArray AppConfig::readEntireTextFile(const QString &path)
{
    return readEntireFile(path);
}

QIODevice *AppConfig::writeEntireTextFile(const QString& text, const QString& path)
{
    auto *f = new QFile(path, QApplication::instance());
    if (f->open(QFile::WriteOnly)) {
        f->write(text.toLocal8Bit());
    }
    f->deleteLater();
    return f;
}

QString AppConfig::workspacePath() const
{
    QJsonValue defaultPath = QDir::home().absoluteFilePath(".embedded_ide-workspace");
    return replaceWithEnv(valueOrDefault(CFG_GLOBAL.value("workspacePath"), defaultPath).toString());
}

const QString& AppConfig::ensureExist(const QString& d)
{
    if (!QDir(d).exists())
        QDir::root().mkpath(d);
    return d;
}

QString AppConfig::projectsPath() const { return ensureExist(QDir(workspacePath()).absoluteFilePath("projects")); }

QString AppConfig::templatesPath() const { return ensureExist(QDir(workspacePath()).absoluteFilePath("templates")); }

QString AppConfig::localConfigFilePath() const { return QDir(ensureExist(workspacePath())).absoluteFilePath("config.json"); }

QHash<QString, QString> AppConfig::externalTools() const
{
    QHash<QString, QString> map;
    auto tools = CFG_LOCAL["externalTools"].toObject();
    for(auto it=tools.begin(); it != tools.end(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

QFileInfoList AppConfig::recentProjects() const
{
    QFileInfoList list;
    for(const auto& e: QDir(projectsPath()).entryInfoList(QDir::Dirs)) {
        QFileInfo info(QDir(e.absoluteFilePath()).absoluteFilePath("Makefile"));
        if (info.isFile())
            list.append(info);
    }
    auto history = CFG_LOCAL["history"].toArray();
    for(const auto e: history) {
        QFileInfo info(e.toString());
        if (info.exists() && !list.contains(info))
            list.append(info);
    }
    return list;
}

QStringList AppConfig::additionalPaths(bool raw) const
{
    QStringList paths;
    if (raw)
        for(const auto e: CFG_LOCAL.value("additionalPaths").toArray())
            paths.append(e.toString());
    else
        for(const auto e: CFG_LOCAL.value("additionalPaths").toArray())
            paths.append(replaceWithEnv(e.toString()));
    return paths;
}

QString AppConfig::templatesUrl() const
{
    return CFG_LOCAL.value("templates").toObject().value("url").toString();
}

QString AppConfig::editorStyle() const
{
    return valueOrDefault(CFG_LOCAL.value("editor").toObject().value("style"), "Default").toString();
}

QFont AppConfig::editorFont() const
{
    auto ed = CFG_LOCAL.value("editor").toObject();
    auto f = ed.value("font").toObject();
    auto name = f.value("name").toString();
    auto size = f.value("size").toInt(-1);
    return QFont(name, size);
}

bool AppConfig::editorSaveOnAction() const
{
    return CFG_LOCAL.value("editor").toObject().value("saveOnAction").toBool();
}

bool AppConfig::editorTabsToSpaces() const
{
    return CFG_LOCAL.value("editor").toObject().value("tabsOnSpaces").toBool();
}

int AppConfig::editorTabWidth() const
{
    return CFG_LOCAL.value("editor").toObject().value("tabWidth").toInt();
}

QString AppConfig::editorFormatterStyle() const
{
    return CFG_LOCAL.value("editor").toObject().value("formatterStyle").toString();
}

QString AppConfig::editorFormatterExtra() const
{
    return CFG_LOCAL.value("editor").toObject().value("formatterExtra").toString();
}

bool AppConfig::editorDetectIdent() const
{
    return CFG_LOCAL.value("editor").toObject().value("detectIdent").toBool();
}

QFont AppConfig::loggerFont() const
{
    auto ed = CFG_LOCAL.value("logger").toObject();
    auto f = ed.value("font").toObject();
    auto name = f.value("name").toString();
    auto size = f.value("size").toInt(-1);
    return QFont(name, size);
}

QString AppConfig::networkProxyHost() const
{
    return CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("host").toString();
}

QString AppConfig::networkProxyPort() const
{
    return CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("port").toString();
}

bool AppConfig::networkProxyUseCredentials() const
{
    return CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("useCredentials").toBool();
}

AppConfig::NetworkProxyType AppConfig::networkProxyType() const
{
    auto type = CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("type").toString();
    bool ok = false;
    auto t = NetworkProxyType(QMetaEnum::fromType<AppConfig::NetworkProxyType>().keyToValue(type.toLatin1().data(), &ok));
    return ok? t : NetworkProxyType::None;
}

QString AppConfig::networkProxyUsername() const
{
    return CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("user").toString();
}

QString AppConfig::networkProxyPassword() const
{
    return CFG_LOCAL.value("network").toObject().value("proxy").toObject().value("pass").toString();
}

bool AppConfig::projectTemplatesAutoUpdate() const
{
    return CFG_LOCAL.value("templates").toObject().value("autoUpdate").toBool();
}

bool AppConfig::useDevelopMode() const
{
    return CFG_LOCAL.value("useDevelopMode").toBool();
}

bool AppConfig::useDarkStyle() const
{
    return CFG_LOCAL.value("useDarkStyle").toBool();
}

QByteArray AppConfig::fileHash(const QString &filename)
{
    auto path = QDir(workspacePath()).filePath("hashes.json");
    auto o = QJsonDocument::fromJson(readEntireTextFile(path)).object();
    auto v = o.value(QFileInfo(filename).fileName());
    if (v.isUndefined())
        return QByteArray();
    return QByteArray::fromHex(v.toString().toLatin1());
}

void AppConfig::load()
{
    auto docBundle = QJsonDocument::fromJson(readEntireTextFile(DEFAULT_LOCAL_RES)).object();
    QJsonParseError err;
    auto docLocal = QJsonDocument::fromJson(readEntireTextFile(defaultLocalTemplateConfig()), &err).object();
    if (err.error != QJsonParseError::NoError) {
        qDebug() << err.errorString();
    }
    CFG_LOCAL = QJsonDocument::fromJson(readEntireTextFile(localConfigFilePath())).object();

    completeToLeft(docLocal, docBundle);
    if (completeToLeft(CFG_LOCAL, docLocal))
        save();

    projectsPath();
    templatesPath();

    emit configChanged(this);
}

void AppConfig::save()
{
    writeEntireFile(globalConfigFilePath(), QJsonDocument(CFG_GLOBAL).toJson());
    writeEntireFile(localConfigFilePath(), QJsonDocument(CFG_LOCAL).toJson());
    emit configChanged(this);
}

void AppConfig::setWorkspacePath(const QString &path)
{
    CFG_GLOBAL.insert("workspacePath", path);
}

void AppConfig::setExternalTools(const QHash<QString, QString> &tools)
{
    QJsonObject o;
    for (auto it=tools.begin(); it != tools.end(); ++it)
        o.insert(it.key(), it.value());
    CFG_LOCAL.insert("externalTools", o);
}

void AppConfig::appendToRecentProjects(const QString &path)
{
    if (!path.startsWith(projectsPath())) {
        QJsonArray history = CFG_LOCAL["history"].toArray();
        if (!history.contains(path))
            history.append(path);
        CFG_LOCAL["history"] = history;
    }
}

void AppConfig::setAdditionalPaths(const QStringList &paths)
{
    QJsonArray array;
    for(const auto& p: paths)
        array.append(p);
    CFG_LOCAL.insert("additionalPaths", array);
}

void AppConfig::setTemplatesUrl(const QString &url)
{
    auto t = CFG_LOCAL["templates"].toObject();
    t.insert("url", url);
    CFG_LOCAL["templates"] = t;
}

void AppConfig::setEditorStyle(const QString &name)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("style", name);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorFont(const QFont &f)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed["font"] = QJsonObject{
        { "name", f.family() },
        { "size", f.pointSize() }
    };
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorSaveOnAction(bool enable)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("saveOnAction", enable);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorTabsToSpaces(bool enable)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("tabsOnSpaces", enable);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorTabWidth(int n)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("tabWidth", n);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorFormatterStyle(const QString &name)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("formatterStyle", name);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorFormatterExtra(const QString &text)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("formatterExtra", text);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setEditorDetectIdent(bool enable)
{
    auto ed = CFG_LOCAL["editor"].toObject();
    ed.insert("detectIdent", enable);
    CFG_LOCAL["editor"] = ed;
}

void AppConfig::setLoggerFont(const QFont &f)
{
    auto log = CFG_LOCAL["logger"].toObject();
    log.insert("font", QJsonObject{
                   { "name", f.family() },
                   { "size", f.pointSize() }
               });
    CFG_LOCAL["logger"] = log;
}

void AppConfig::setNetworkProxyHost(const QString &name)
{
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("host", name);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setNetworkProxyPort(const QString &port)
{
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("port", port);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setNetworkProxyUseCredentials(bool use)
{
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("useCredentials", use);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setNetworkProxyType(AppConfig::NetworkProxyType type)
{
    auto typeName = QString(QMetaEnum::fromType<AppConfig::NetworkProxyType>().valueToKey(int(type)));
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("type", typeName);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setNetworkProxyUsername(const QString &user)
{
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("user", user);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setNetworkProxyPassword(const QString &pass)
{
    auto net = CFG_LOCAL["network"].toObject();
    auto proxy = net.value("proxy").toObject();
    proxy.insert("pass", pass);
    net["proxy"] = proxy;
    CFG_LOCAL["network"] = net;
}

void AppConfig::setProjectTemplatesAutoUpdate(bool en)
{
    auto t = CFG_LOCAL["templates"].toObject();
    t.insert("autoUpdate", en);
    CFG_LOCAL["templates"] = t;
}

void AppConfig::setUseDevelopMode(bool use)
{
    CFG_LOCAL.insert("useDevelopMode", use);
}

void AppConfig::setUseDarkStyle(bool use)
{
    CFG_LOCAL.insert("useDarkStyle", use);
}

void AppConfig::addHash(const QString &filename, const QByteArray &hash)
{
    auto path = QDir(workspacePath()).filePath("hashes.json");
    auto o = QJsonDocument::fromJson(readEntireTextFile(path)).object();
    o.insert(QFileInfo(filename).fileName(), QString(hash.toHex()));
    writeEntireFile(path, QJsonDocument(o).toJson());
}

void AppConfig::purgeHash()
{
    auto path = QDir(workspacePath()).filePath("hashes.json");
    auto o = QJsonDocument::fromJson(readEntireTextFile(path)).object();
    for(auto& k: o.keys())
        if (!QFileInfo(QDir(templatesPath()).filePath(k)).exists())
            o.remove(k);
    writeEntireFile(path, QJsonDocument(o).toJson());
}
