#include "loghandler.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QtDebug>
#include <QtGlobal>
#include <QMessageLogContext>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QCoreApplication>
#include <QDir>
#include <QMessageBox>

// singleton
QMutex _g_LogHandler_Mutex;

LogHandler::LogHandler(QString servername,QObject *parent) :
    QObject(parent) ,
    m_localServer(nullptr) ,
    m_localSocket(nullptr) ,
    m_pFile(nullptr)
{
    QString strSvrName = servername+"_log";

    // try connect a exist server, if not, create a server
    m_localSocket = new QLocalSocket(this);
    m_localSocket->connectToServer(strSvrName, QIODevice::WriteOnly);
    if (m_localSocket->waitForConnected(500)) {
        connect(m_localSocket, SIGNAL(disconnected()), this, SLOT(svrClose()));
    } else {
        // no server, this instace become to a server
        delete m_localSocket;
        m_localSocket = nullptr;

        // create server
        m_localServer = new QLocalServer(this);
        connect(m_localServer, SIGNAL(newConnection()), this, SLOT(newClient()));
        if (!m_localServer->listen(strSvrName)) {
            if (m_localServer->serverError() == QAbstractSocket::AddressInUseError
                && QFile::exists(m_localServer->serverName())) {
                QFile::remove(m_localServer->serverName());
                m_localServer->listen(strSvrName);
            }
        }

		QString filepath = QCoreApplication::applicationDirPath() + "/datalog";

		//查看这个是不是要建立文件夹
		QDir *temp = new QDir;
		if (!temp->exists(filepath))
			temp->mkdir(filepath);

        // log file
        m_pFile = new QFile(filepath+"/"+strSvrName+".txt");
        if(!m_pFile->open(QIODevice::WriteOnly | QIODevice::Append))
        {
            delete m_pFile;
            m_pFile = nullptr;
        }
    }

    // reward all local msg to the function
    qInstallMessageHandler(_localLogProc);

    if (m_localSocket != nullptr) {
        QString msg = " New log client, instance name: " + m_instName;
        localLogProc(1, msg);
    }
    if (m_localServer != nullptr) {
        QString msg = " Create log server, instance name: " + m_instName;
        localLogProc(1, msg);
    }
}

LogHandler::~LogHandler()
{
    if (m_localSocket != nullptr) {
        QString msg = " Shutdown log client, instance name: " + m_instName;
        localLogProc(1, msg);
    }
    if (m_localServer != nullptr) {
        QString msg = " Shutdown log server, instance name: " + m_instName;
        localLogProc(1, msg);
    }

    // reward all local msg to the function
    qInstallMessageHandler(nullptr);

    if (m_localSocket != nullptr)
    {
        disconnect(m_localSocket, SIGNAL(disconnected()), this, SLOT(svrClose()));
        m_localSocket->disconnectFromServer();
        if (!m_localSocket->waitForDisconnected(500))
            m_localSocket->abort();
        m_localSocket->deleteLater();
        m_localSocket = nullptr;
    }

    // delete server
    if (m_localServer != nullptr)
    {
        if (m_localServer->isListening())
            disconnect(m_localServer, SIGNAL(newConnection()), this, SLOT(newClient()));
        m_localServer->deleteLater();
        m_localServer = nullptr;
    }

    if (m_pFile != nullptr)
    {
        m_pFile->flush();
        m_pFile->close();
        m_pFile->deleteLater();
        m_pFile = nullptr;
    }
}

void LogHandler::_localLogProc(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (m_instance == nullptr)
        return;

    int iType = 0;
    switch (type) {
    case QtMsgType::QtDebugMsg: iType = 1; break;
    case QtMsgType::QtWarningMsg: iType = 2; break;
    case QtMsgType::QtCriticalMsg: iType = 3; break;
    case QtMsgType::QtFatalMsg: iType = 4; break;
    default: iType = 1; break;
    }

    //QString strInfo = QString(" %1(%2)<%3>| %4").arg(context.file).arg(context.line).arg(context.function).arg(msg);
	QString strInfo = msg;
    m_instance->localLogProc(iType, strInfo);
}

void LogHandler::localLogProc(int type, QString &msg)
{
    emit dolog(type, msg);

    if (m_localSocket != nullptr)
    {
        QChar * pMsg = msg.data();
        pMsg[0] = QChar(type);
        // send log to server
        QDataStream stream(m_localSocket);
        stream << msg;
        m_localSocket->waitForBytesWritten(100);
    } else if (m_localServer != nullptr)
        writeLog(type, msg);
}

void LogHandler::remoteLogProc(int type, const QString &msg)
{
    writeLog(type, msg);
}

void LogHandler::writeLog(int type, const QString &msg)
{
	
    // write log
    if (m_pFile != nullptr) {
        QTextStream stream(m_pFile);
        stream << QDateTime::currentDateTime().toString("hh:mm:ss| ");

        switch (type) {
        case 1: stream << "Debug| "; break;
        case 2: stream << "Warning| "; break;
        case 3: stream << "Critical| "; break;
        case 4: stream << "Fatal| "; break;
        default: stream << "Unknow| "; break;
        }

        stream << msg << endl;
    }
}

LogHandler * LogHandler::m_instance = nullptr;
QString LogHandler::m_instName = "none";
LogHandler * LogHandler::CreateInstance(QObject *parent, QString name)
{
    if (m_instance == nullptr) {
        QMutexLocker locker(&_g_LogHandler_Mutex);
        if (m_instance == nullptr) {
            m_instName = name;
            m_instance = new LogHandler(name,parent);
        }
    }
    return m_instance;
}

void LogHandler::ReleaseInstance()
{
    if (m_instance != nullptr) {
        QMutexLocker locker(&_g_LogHandler_Mutex);
        if (m_instance != nullptr) {
            delete m_instance;
            m_instance = nullptr;
        }
    }
}

void LogHandler::newClient()
{
    if (m_localServer == nullptr)
        return;

    QLocalSocket * skt = m_localServer->nextPendingConnection();
    if (skt == nullptr)
        return;
    connect(skt, SIGNAL(readyRead()), this, SLOT(rcvClient()));
    connect(skt, SIGNAL(disconnected()), skt, SLOT(deleteLater()));
}

void LogHandler::rcvClient()
{
    QLocalSocket * skt = static_cast<QLocalSocket *>(sender());
    if (skt != nullptr) {
        QDataStream stream(skt);
        int type;
        QString msg;
        stream >> msg;
        QChar * pMsg = msg.data();
        type = pMsg[0].unicode();
        pMsg[0] = QChar(' ');

        remoteLogProc(type, msg);
    }
}

void LogHandler::svrClose()
{
    if (m_localSocket == nullptr)
        return;

    disconnect(m_localSocket, SIGNAL(disconnected()), this, SLOT(svrClose()));

    m_localSocket->deleteLater();
    m_localSocket = nullptr;
}
