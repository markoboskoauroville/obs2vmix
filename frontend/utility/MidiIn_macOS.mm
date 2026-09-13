/******************************************************************************
    obs2vmix: MIDI input on macOS (CoreMIDI)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MidiIn.hpp"

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

#include <util/base.h>

struct MidiInImpl {
	MIDIClientRef client = 0;
	MIDIPortRef port = 0;
};

bool MidiIn::Available()
{
	return true;
}

static QString sourceName(MIDIEndpointRef src)
{
	CFStringRef str = nullptr;
	if (MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &str) != noErr || !str)
		return QStringLiteral("MIDI input");
	QString name = QString::fromCFString(str);
	CFRelease(str);
	return name;
}

bool MidiIn::PlatformStart()
{
	MidiInImpl *m = new MidiInImpl();

	if (MIDIClientCreate(CFSTR("OBS2vMix"), nullptr, nullptr, &m->client) != noErr) {
		blog(LOG_WARNING, "[obs2vmix] CoreMIDI: no client");
		delete m;
		return false;
	}

	MidiIn *self = this;
	OSStatus st = MIDIInputPortCreateWithProtocol(
		m->client, CFSTR("OBS2vMix in"), kMIDIProtocol_1_0, &m->port,
		^(const MIDIEventList *evtlist, void *) {
			const MIDIEventPacket *packet = &evtlist->packet[0];
			for (UInt32 p = 0; p < evtlist->numPackets; p++) {
				for (UInt32 w = 0; w < packet->wordCount; w++) {
					UInt32 word = packet->words[w];
					/* MIDI 1.0 channel voice message in a UMP word */
					if ((word >> 28) == 2) {
						self->DeliverMessage((word >> 16) & 0xFF, (word >> 8) & 0x7F,
								     word & 0x7F);
					}
				}
				packet = MIDIEventPacketNext(packet);
			}
		});
	if (st != noErr) {
		blog(LOG_WARNING, "[obs2vmix] CoreMIDI: no input port");
		MIDIClientDispose(m->client);
		delete m;
		return false;
	}

	ItemCount n = MIDIGetNumberOfSources();
	for (ItemCount i = 0; i < n; i++) {
		MIDIEndpointRef src = MIDIGetSource(i);
		if (MIDIPortConnectSource(m->port, src, nullptr) == noErr)
			names << sourceName(src);
	}

	impl = m;
	blog(LOG_INFO, "[obs2vmix] CoreMIDI: %d input(s)", (int)names.size());
	return true;
}

void MidiIn::PlatformStop()
{
	MidiInImpl *m = static_cast<MidiInImpl *>(impl);
	if (!m)
		return;
	if (m->port)
		MIDIPortDispose(m->port);
	if (m->client)
		MIDIClientDispose(m->client);
	delete m;
	impl = nullptr;
}
