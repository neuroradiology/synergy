/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2015 Synergy Si, Std.
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WEBCLIENT_H
#define WEBCLIENT_H

#include <QString>
#include <QStringList>
#include <QObject>

#include "CoreInterface.h"

class QMessageBox;
class QWidget;
class QStringList;

class WebClient : public QObject
{
	Q_OBJECT

public:
	int getEdition(const QString& email,
			const QString& password,
			QMessageBox& message,
			QWidget* w);
	void setEmail(QString& e) { m_Email = e; }
	void setPassword(QString& p) { m_Password = p; }
	QStringList& getPluginList() { return m_PluginList; }

public slots:
	void queryPluginList();

signals:
	void error(QString e);
	void queryPluginDone();

private:
	QString request(const QString& email,
			const QString& password,
			QStringList& args);

private:
	QString m_Email;
	QString m_Password;
	QStringList m_PluginList;
	CoreInterface m_CoreInterface;
};

#endif // WEBCLIENT_H
