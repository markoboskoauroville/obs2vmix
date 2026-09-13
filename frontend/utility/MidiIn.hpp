/******************************************************************************
    obs2vmix: MIDI input for the FX rack

    A small input layer, one file per platform (CoreMIDI, WinMM, ALSA):
    every MIDI input is opened, control changes and note-ons are delivered
    as Qt signals on the UI thread. That is all the rack needs for learn
    mode and for driving its controls.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <QObject>
#include <QStringList>

class MidiIn : public QObject {
	Q_OBJECT

public:
	explicit MidiIn(QObject *parent = nullptr);
	~MidiIn();

	/* opens every input; false when the platform has none (or no MIDI at all) */
	bool Start();
	void Stop();
	bool Running() const { return running; }
	QStringList Devices() const { return names; }

	/* true when this build can talk to MIDI hardware */
	static bool Available();

signals:
	void ControlChange(int controller, int value);
	void NoteOn(int note, int velocity);

public:
	/* called from the platform's MIDI thread */
	void DeliverMessage(unsigned char status, unsigned char data1, unsigned char data2);

private:
	bool PlatformStart();
	void PlatformStop();

	QStringList names;
	void *impl = nullptr;
	bool running = false;
};
