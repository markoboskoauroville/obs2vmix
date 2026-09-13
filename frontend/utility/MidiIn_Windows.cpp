/******************************************************************************
    obs2vmix: MIDI input on Windows (WinMM)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MidiIn.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <util/base.h>

#include <vector>

struct MidiInImpl {
	std::vector<HMIDIIN> handles;
};

bool MidiIn::Available()
{
	return true;
}

static void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
{
	if (msg != MIM_DATA)
		return;
	MidiIn *self = reinterpret_cast<MidiIn *>(instance);
	self->DeliverMessage((unsigned char)(param1 & 0xFF), (unsigned char)((param1 >> 8) & 0x7F),
			     (unsigned char)((param1 >> 16) & 0x7F));
}

bool MidiIn::PlatformStart()
{
	UINT count = midiInGetNumDevs();
	if (count == 0)
		return false;

	MidiInImpl *m = new MidiInImpl();
	for (UINT i = 0; i < count; i++) {
		HMIDIIN h = nullptr;
		if (midiInOpen(&h, i, (DWORD_PTR)midiInProc, (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
			continue;
		if (midiInStart(h) != MMSYSERR_NOERROR) {
			midiInClose(h);
			continue;
		}
		MIDIINCAPSW caps = {};
		if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
			names << QString::fromWCharArray(caps.szPname);
		else
			names << QStringLiteral("MIDI input %1").arg(i + 1);
		m->handles.push_back(h);
	}

	if (m->handles.empty()) {
		delete m;
		return false;
	}
	impl = m;
	blog(LOG_INFO, "[obs2vmix] WinMM: %d MIDI input(s)", (int)m->handles.size());
	return true;
}

void MidiIn::PlatformStop()
{
	MidiInImpl *m = static_cast<MidiInImpl *>(impl);
	if (!m)
		return;
	for (HMIDIIN h : m->handles) {
		midiInStop(h);
		midiInReset(h);
		midiInClose(h);
	}
	delete m;
	impl = nullptr;
}
