/******************************************************************************
    obs2vmix: MIDI input for the FX rack, the platform-independent part

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MidiIn.hpp"

MidiIn::MidiIn(QObject *parent) : QObject(parent) {}

MidiIn::~MidiIn()
{
	Stop();
}

bool MidiIn::Start()
{
	if (running)
		return true;
	names.clear();
	running = PlatformStart();
	return running;
}

void MidiIn::Stop()
{
	if (!running)
		return;
	PlatformStop();
	running = false;
}

/* the MIDI thread hands a channel-voice message here; the signals cross
 * to the UI thread on their own (queued, the receiver lives there) */
void MidiIn::DeliverMessage(unsigned char status, unsigned char data1, unsigned char data2)
{
	unsigned char type = status & 0xF0;
	if (type == 0xB0) {
		emit ControlChange(data1 & 0x7F, data2 & 0x7F);
	} else if (type == 0x90 && (data2 & 0x7F) > 0) {
		emit NoteOn(data1 & 0x7F, data2 & 0x7F);
	}
}
