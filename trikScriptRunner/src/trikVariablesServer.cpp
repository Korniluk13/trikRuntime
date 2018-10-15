#include "trikVariablesServer.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegExp>
#include <QtNetwork/QTcpSocket>

using namespace trikScriptRunner;

TrikVariablesServer::TrikVariablesServer() :
	mTcpServer(new QTcpServer(this))
{
	connect(mTcpServer.data(), SIGNAL(newConnection()), this, SLOT(onNewConnection()));
	mTcpServer->listen(QHostAddress::LocalHost, port);
}

void TrikVariablesServer::sendHTTPResponse(const QJsonObject &json)
{
	QByteArray jsonBytes = QJsonDocument(json).toJson();

	// TODO: Create other way for endline constant, get rid of define
#define NL "\r\n"
	QString header = "HTTP/1.0 200 OK" NL
					 "Connection: close" NL
					 "Content-type: text/plain, charset=us-ascii" NL
					 "Content-length: " + QString::number(jsonBytes.size()) + NL
					 NL;
#undef NL

	mCurrentConnection->write(header.toLatin1());
	mCurrentConnection->write(jsonBytes);
	mCurrentConnection->close();
}

void TrikVariablesServer::onNewConnection()
{
	// TODO: Object from nextPendingConnection is a child of QTcpServer, so it will be automatically
	// deleted when QTcpServer is destroyed. Maybe it may sense to call "deleteLater" explicitly,
	// to avoid wasting memory.
	mCurrentConnection = mTcpServer->nextPendingConnection();
	connect(mCurrentConnection, SIGNAL(readyRead()), this, SLOT(processHTTPRequest()));
}

void TrikVariablesServer::processHTTPRequest()
{
	// TODO: Make sure, that different connections aren't intersected using mutex or
	// support multiple connections simultaneously
	QStringList list;
	while (mCurrentConnection->canReadLine())
	{
		QString data = QString(mCurrentConnection->readLine());
		list.append(data);
	}

	const QString cleanString = list.join("").remove(QRegExp("[\\n\\t\\r]"));
	const QStringList words = cleanString.split(QRegExp("\\s+"), QString::SkipEmptyParts);

	if (words[1] == "/web/") {
		emit getVariables("web");
	}
}
