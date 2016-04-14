#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QVector>

class QLocalServer;
class QLocalSocket;
class QFile;

class LogHandler : public QObject
{
    Q_OBJECT
private:
	explicit LogHandler(QString servername,QObject *parent = 0);
    ~LogHandler();

    static void _localLogProc(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    void localLogProc(int type, QString &msg);
    void remoteLogProc(int type, const QString &msg);

    void writeLog(int type, const QString &msg);

public:
    static LogHandler * CreateInstance(QObject *parent = 0, QString name = QString("none"));
    static void ReleaseInstance();

signals:
    void dolog(int type, const QString &msg);

private slots:
    void newClient();
    void rcvClient();
    void svrClose();

public slots:

private:
    QLocalServer * m_localServer;
    QLocalSocket * m_localSocket;
    QFile * m_pFile;

    static LogHandler * m_instance;
    static QString m_instName;
};

#endif // LOGHANDLER_H
