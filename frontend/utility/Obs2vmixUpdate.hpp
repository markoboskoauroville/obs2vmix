/******************************************************************************
    obs2vmix: updating the app

    Help -> Update OBS2vMix opens the Terminal and runs `obs2vmix-update`
    (fetched fresh from the repository), which installs the newest release
    without a Gatekeeper dialog. At startup the newest release is looked up
    and offered when it is newer than this build.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <QString>

class QWidget;

namespace obs2vmix {

/* this build's version, from the newest obs2vmix-<x.y.z> tag */
QString CurrentVersion();

/* the release page and the terminal command */
QString ReleasesUrl();
QString UpdateCommand();

/* macOS: Terminal runs the updater; elsewhere false */
bool RunUpdaterInTerminal();

/* the command and the page, for Help */
void ShowUpdateHelp(QWidget *parent);

/* asks GitHub for the newest release; a dialog offers it when it is newer.
 * quiet: say nothing when there is nothing new or the lookup fails */
void CheckForUpdate(QWidget *parent, bool quiet);

} // namespace obs2vmix
