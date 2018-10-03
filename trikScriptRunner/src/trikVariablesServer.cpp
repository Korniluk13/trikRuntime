#include "trikVariablesServer.h"

#include <QTimer>
#include <QDebug>
#include <QtNetwork/QTcpSocket>
#include <QDataStream>
#include <QRegExp>

TrikVariablesServer::TrikVariablesServer() :
	mTcpServer(new QTcpServer(this))
{
	connect(mTcpServer, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
	bool res = mTcpServer->listen(QHostAddress::LocalHost, 10000);
	qDebug() << res;
}

void TrikVariablesServer::onNewConnection()
{
	mCurrentConnection = mTcpServer->nextPendingConnection();

	connect(mCurrentConnection, SIGNAL(readyRead()), this, SLOT(readData()));
	qDebug() << "new connection";
}

void TrikVariablesServer::readData()
{
	QStringList list;
	while (mCurrentConnection->canReadLine())
	{
		QString data = QString(mCurrentConnection->readLine());
		list.append(data);
	}

	QString resultString = list.join("").remove(QRegExp("[\\n\\t\\r]"));
	emit getVariables(resultString);
}
