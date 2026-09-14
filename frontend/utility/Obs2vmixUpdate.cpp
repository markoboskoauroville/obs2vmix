/******************************************************************************
    obs2vmix: updating the app

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "Obs2vmixUpdate.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVersionNumber>

namespace obs2vmix {

static const char *REPO = "markoboskoauroville/obs2vmix";
static const char *SCRIPT_URL =
	"https://raw.githubusercontent.com/markoboskoauroville/obs2vmix/obs2vmix/seam/tools/obs2vmix-update";

QString CurrentVersion()
{
	return QString::fromUtf8(OBS2VMIX_VERSION);
}

QString ReleasesUrl()
{
	return QString("https://github.com/%1/releases").arg(QString::fromUtf8(REPO));
}

QString UpdateCommand()
{
	return QStringLiteral("obs2vmix-update");
}

/* the line the Terminal runs: fetch the script fresh, then run it */
static QString terminalLine()
{
	return QString("mkdir -p $HOME/.local/bin && curl -fsSL %1 -o $HOME/.local/bin/obs2vmix-update && "
		       "chmod +x $HOME/.local/bin/obs2vmix-update && $HOME/.local/bin/obs2vmix-update")
		.arg(QString::fromUtf8(SCRIPT_URL));
}

bool RunUpdaterInTerminal()
{
#ifdef __APPLE__
	QString line = terminalLine();
	QStringList args;
	args << "-e" << QString("tell application \"Terminal\" to do script \"%1\"").arg(line) << "-e"
	     << "tell application \"Terminal\" to activate";
	return QProcess::startDetached("/usr/bin/osascript", args);
#else
	return false;
#endif
}

void ShowUpdateHelp(QWidget *parent)
{
	QString text = QTStr("obs2vmix.Update.HelpText")
			       .arg(CurrentVersion(), UpdateCommand(), ReleasesUrl());
	QMessageBox box(parent);
	box.setWindowTitle(QTStr("obs2vmix.Update.Help"));
	box.setTextFormat(Qt::RichText);
	box.setText(text);
	QPushButton *page = box.addButton(QTStr("obs2vmix.Update.OpenPage"), QMessageBox::ActionRole);
#ifdef __APPLE__
	QPushButton *run = box.addButton(QTStr("obs2vmix.Update.Now"), QMessageBox::AcceptRole);
#else
	QPushButton *run = nullptr;
#endif
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() == page)
		QDesktopServices::openUrl(QUrl(ReleasesUrl()));
	else if (run && box.clickedButton() == run)
		RunUpdaterInTerminal();
}

static QVersionNumber versionOf(const QString &tag)
{
	QString v = tag;
	if (v.startsWith("obs2vmix-"))
		v = v.mid(9);
	return QVersionNumber::fromString(v);
}

void CheckForUpdate(QWidget *parent, bool quiet)
{
	QNetworkAccessManager *nam = new QNetworkAccessManager(parent);
	QNetworkRequest req(QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(QString::fromUtf8(REPO))));
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("OBS2vMix/%1").arg(CurrentVersion()));
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setTransferTimeout(10000);

	QNetworkReply *reply = nam->get(req);
	QObject::connect(reply, &QNetworkReply::finished, parent, [reply, nam, parent, quiet]() {
		reply->deleteLater();
		nam->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			if (!quiet)
				OBSMessageBox::warning(parent, QTStr("obs2vmix.Update.Check"),
						       QTStr("obs2vmix.Update.CheckFailed").arg(reply->errorString()));
			return;
		}

		QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		QString tag = doc.object().value("tag_name").toString();
		QVersionNumber newest = versionOf(tag);
		QVersionNumber mine = versionOf(CurrentVersion());
		if (newest.isNull()) {
			if (!quiet)
				OBSMessageBox::warning(parent, QTStr("obs2vmix.Update.Check"),
						       QTStr("obs2vmix.Update.CheckFailed").arg(tag));
			return;
		}

		if (newest <= mine) {
			if (!quiet)
				OBSMessageBox::information(parent, QTStr("obs2vmix.Update.Check"),
							   QTStr("obs2vmix.Update.Newest").arg(mine.toString()));
			return;
		}

		QMessageBox box(parent);
		box.setWindowTitle(QTStr("obs2vmix.Update.Check"));
		box.setText(QTStr("obs2vmix.Update.Available").arg(newest.toString(), mine.toString()));
		box.setInformativeText(QTStr("obs2vmix.Update.AvailableInfo").arg(UpdateCommand()));
#ifdef __APPLE__
		QPushButton *now = box.addButton(QTStr("obs2vmix.Update.Now"), QMessageBox::AcceptRole);
#else
		QPushButton *now = nullptr;
#endif
		QPushButton *page = box.addButton(QTStr("obs2vmix.Update.OpenPage"), QMessageBox::ActionRole);
		box.addButton(QTStr("obs2vmix.Update.Later"), QMessageBox::RejectRole);
		box.exec();
		if (now && box.clickedButton() == now)
			RunUpdaterInTerminal();
		else if (box.clickedButton() == page)
			QDesktopServices::openUrl(QUrl(ReleasesUrl()));
	});
}

} // namespace obs2vmix
