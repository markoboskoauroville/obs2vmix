/******************************************************************************
    obs2vmix: MIDI input on Linux (ALSA sequencer)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MidiIn.hpp"

#include <util/base.h>

#ifdef OBS2VMIX_HAVE_ALSA

#include <alsa/asoundlib.h>
#include <poll.h>

#include <atomic>
#include <thread>
#include <vector>

struct MidiInImpl {
	snd_seq_t *seq = nullptr;
	int port = -1;
	std::atomic<bool> stop{false};
	std::thread thread;
};

bool MidiIn::Available()
{
	return true;
}

static void readLoop(MidiInImpl *m, MidiIn *self)
{
	int nfds = snd_seq_poll_descriptors_count(m->seq, POLLIN);
	std::vector<struct pollfd> fds((size_t)(nfds > 0 ? nfds : 1));
	snd_seq_poll_descriptors(m->seq, fds.data(), (unsigned)fds.size(), POLLIN);

	while (!m->stop.load()) {
		int r = poll(fds.data(), (nfds_t)fds.size(), 200);
		if (r <= 0)
			continue;
		snd_seq_event_t *ev = nullptr;
		while (snd_seq_event_input_pending(m->seq, 1) > 0 && snd_seq_event_input(m->seq, &ev) >= 0 && ev) {
			if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
				self->DeliverMessage((unsigned char)(0xB0 | (ev->data.control.channel & 0x0F)),
						     (unsigned char)ev->data.control.param,
						     (unsigned char)ev->data.control.value);
			} else if (ev->type == SND_SEQ_EVENT_NOTEON) {
				self->DeliverMessage((unsigned char)(0x90 | (ev->data.note.channel & 0x0F)),
						     ev->data.note.note, ev->data.note.velocity);
			}
		}
	}
}

bool MidiIn::PlatformStart()
{
	MidiInImpl *m = new MidiInImpl();
	if (snd_seq_open(&m->seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
		blog(LOG_WARNING, "[obs2vmix] ALSA sequencer: cannot open");
		delete m;
		return false;
	}
	snd_seq_set_client_name(m->seq, "OBS2vMix");
	m->port = snd_seq_create_simple_port(m->seq, "FX rack", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
					     SND_SEQ_PORT_TYPE_APPLICATION);
	if (m->port < 0) {
		snd_seq_close(m->seq);
		delete m;
		return false;
	}

	/* subscribe to every readable port that is not ours */
	int self_client = snd_seq_client_id(m->seq);
	snd_seq_client_info_t *cinfo;
	snd_seq_port_info_t *pinfo;
	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);
	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(m->seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);
		if (client == self_client || client == SND_SEQ_CLIENT_SYSTEM)
			continue;
		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(m->seq, pinfo) >= 0) {
			unsigned caps = snd_seq_port_info_get_capability(pinfo);
			if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) !=
			    (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
				continue;
			if (snd_seq_connect_from(m->seq, m->port, client, snd_seq_port_info_get_port(pinfo)) == 0)
				names << QString::fromUtf8(snd_seq_port_info_get_name(pinfo));
		}
	}

	m->thread = std::thread(readLoop, m, this);
	impl = m;
	blog(LOG_INFO, "[obs2vmix] ALSA sequencer: %d MIDI input(s)", (int)names.size());
	return true;
}

void MidiIn::PlatformStop()
{
	MidiInImpl *m = static_cast<MidiInImpl *>(impl);
	if (!m)
		return;
	m->stop = true;
	if (m->thread.joinable())
		m->thread.join();
	if (m->seq)
		snd_seq_close(m->seq);
	delete m;
	impl = nullptr;
}

#else /* no ALSA at build time */

bool MidiIn::Available()
{
	return false;
}

bool MidiIn::PlatformStart()
{
	blog(LOG_INFO, "[obs2vmix] built without ALSA: no MIDI input");
	return false;
}

void MidiIn::PlatformStop() {}

#endif
