#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QObject>
#include <QProcess>

#include <functional>

class ProcessManager : public QObject
{
    Q_OBJECT
public:
    typedef std::function<void (QProcess *, const QString&)> outputHandler_t;
    typedef std::function<void (QProcess *, int, QProcess::ExitStatus)> terminationHandler_t;
    typedef std::function<void (QProcess *, QProcess::ProcessError)> errorHandler_t;

    explicit ProcessManager(QObject *parent = nullptr);
    virtual ~ProcessManager();

    QProcess *processFor(const QString& name);

    void setTerminationHandler(const QString& name, terminationHandler_t func);
    void setErrorHandler(const QString& name, errorHandler_t func);
    void setStderrInterceptor(const QString& name, outputHandler_t func);
    void setStdoutInterceptor(const QString& name, outputHandler_t func);

    bool isRunning(const QString& name) { return processFor(name)->state() == QProcess::Running; }
    void start(const QString& name, const QString& command, const QStringList& args = {}, const QHash<QString, QString> &extraEnv = {}, const QString& workingDir = QString());
    bool terminate(const QString& name, bool canKill = false, int timeout = 3000);
signals:

public slots:

private:
    class Priv_t;
    Priv_t *priv;
};

#endif // PROCESSMANAGER_H