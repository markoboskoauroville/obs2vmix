/******************************************************************************
    obs2vmix: the master FX chain

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MasterChain.hpp"

#include <algorithm>
#include <cstring>

/* "…/Foo.vst" -> "Foo" */
static std::string plugin_name_fallback(const std::string &path)
{
	size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);
	return name;
}

MasterChain::MasterChain()
{
	scratch.resize((size_t)MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES, 0.0f);
}

MasterChain::~MasterChain()
{
	Remove();
	for (Slot &s : slots)
		s.plugin.reset();
}

void MasterChain::Install()
{
	if (installed)
		return;
	obs_set_master_audio_processor(MasterChain::Hook, this);
	installed = true;
}

void MasterChain::Remove()
{
	if (!installed)
		return;
	/* the call returns only after the audio thread left the processor */
	obs_set_master_audio_processor(nullptr, nullptr);
	installed = false;
}

/* ---------------------------------------------------------------------- */
/* slots                                                                   */

bool MasterChain::IsEmpty(int i) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return i < 0 || i >= SLOTS || !slots[i].plugin;
}

std::string MasterChain::SlotPath(int i) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return (i >= 0 && i < SLOTS) ? slots[i].path : std::string();
}

std::string MasterChain::SlotName(int i) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return (i >= 0 && i < SLOTS) ? slots[i].name : std::string();
}

bool MasterChain::SlotOn(int i) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return (i >= 0 && i < SLOTS) ? slots[i].on : false;
}

float MasterChain::SlotMix(int i) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return (i >= 0 && i < SLOTS) ? slots[i].mix : 1.0f;
}

VSTPlugin *MasterChain::Plugin(int i)
{
	std::lock_guard<std::mutex> lock(mutex);
	return (i >= 0 && i < SLOTS) ? slots[i].plugin.get() : nullptr;
}

/* loading happens outside the lock: a plugin can take a while to come up
 * and the audio thread must not wait for it */
bool MasterChain::SetPlugin(int i, const std::string &path, const std::string &name)
{
	if (i < 0 || i >= SLOTS)
		return false;
	if (path.empty()) {
		ClearSlot(i);
		return true;
	}

	auto plugin = std::make_unique<VSTPlugin>(nullptr);
	plugin->setDisplayName("OBS2vMix FX rack", "slot " + std::to_string(i + 1));
	plugin->loadEffectFromPath(path);
	if (!plugin->vstLoaded()) {
		blog(LOG_WARNING, "[obs2vmix] rack slot %d: could not load '%s'", i + 1, path.c_str());
		return false;
	}

	std::unique_ptr<VSTPlugin> old;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old = std::move(slots[i].plugin);
		slots[i].plugin = std::move(plugin);
		slots[i].path = path;
		slots[i].name = name.empty() ? plugin_name_fallback(path) : name;
	}
	old.reset();
	return true;
}

void MasterChain::ClearSlot(int i)
{
	if (i < 0 || i >= SLOTS)
		return;
	std::unique_ptr<VSTPlugin> old;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old = std::move(slots[i].plugin);
		slots[i].path.clear();
		slots[i].name.clear();
		slots[i].on = true;
		slots[i].mix = 1.0f;
	}
	old.reset();
}

void MasterChain::SetOn(int i, bool on)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (i >= 0 && i < SLOTS)
		slots[i].on = on;
}

void MasterChain::SetMix(int i, float mix)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (i >= 0 && i < SLOTS)
		slots[i].mix = std::clamp(mix, 0.0f, 1.0f);
}

void MasterChain::Move(int from, int to)
{
	if (from < 0 || from >= SLOTS || to < 0 || to >= SLOTS || from == to)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	Slot tmp = std::move(slots[from]);
	if (from < to) {
		for (int i = from; i < to; i++)
			slots[i] = std::move(slots[i + 1]);
	} else {
		for (int i = from; i > to; i--)
			slots[i] = std::move(slots[i - 1]);
	}
	slots[to] = std::move(tmp);
}

/* ---------------------------------------------------------------------- */
/* presets                                                                 */

OBSDataAutoRelease MasterChain::Save() const
{
	OBSDataAutoRelease data = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();

	std::lock_guard<std::mutex> lock(mutex);
	for (const Slot &s : slots) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "path", s.path.c_str());
		obs_data_set_string(item, "name", s.name.c_str());
		obs_data_set_bool(item, "on", s.on);
		obs_data_set_double(item, "mix", s.mix);
		if (s.plugin && s.plugin->vstLoaded()) {
			obs_data_set_int(item, "program", s.plugin->getProgram());
			obs_data_set_string(item, "chunk", s.plugin->getChunk().c_str());
		}
		obs_data_array_push_back(arr, item);
	}
	obs_data_set_array(data, "slots", arr);
	obs_data_set_int(data, "apply", apply.load());
	obs_data_set_bool(data, "bypass", bypass.load());
	return data;
}

void MasterChain::Load(obs_data_t *data)
{
	if (!data)
		return;

	OBSDataArrayAutoRelease arr = obs_data_get_array(data, "slots");
	size_t count = arr ? obs_data_array_count(arr) : 0;

	for (int i = 0; i < SLOTS; i++) {
		if ((size_t)i >= count) {
			ClearSlot(i);
			continue;
		}
		OBSDataAutoRelease item = obs_data_array_item(arr, (size_t)i);
		std::string path = obs_data_get_string(item, "path");
		std::string name = obs_data_get_string(item, "name");
		if (path.empty()) {
			ClearSlot(i);
			continue;
		}
		if (SlotPath(i) != path)
			SetPlugin(i, path, name);
		VSTPlugin *plugin = Plugin(i);
		if (plugin) {
			if (obs_data_has_user_value(item, "program"))
				plugin->setProgram((int)obs_data_get_int(item, "program"));
			const char *chunk = obs_data_get_string(item, "chunk");
			if (chunk && *chunk)
				plugin->setChunk(chunk);
		}
		SetOn(i, obs_data_get_bool(item, "on"));
		obs_data_set_default_double(item, "mix", 1.0);
		SetMix(i, (float)obs_data_get_double(item, "mix"));
	}

	apply = (int)obs_data_get_int(data, "apply");
	bypass = obs_data_get_bool(data, "bypass");
}

/* ---------------------------------------------------------------------- */
/* the audio thread                                                        */

void MasterChain::Hook(void *param, size_t mix_idx, float **data, size_t channels, size_t frames, uint32_t)
{
	static_cast<MasterChain *>(param)->Process(mix_idx, data, channels, frames);
}

void MasterChain::Process(size_t mix_idx, float **data, size_t channels, size_t frames)
{
	if (bypass.load())
		return;

	bool wanted = mix_idx == 0;
	if (!wanted && apply.load() == (int)Apply::LiveAndRecord)
		wanted = (recordTracks.load() & (1u << mix_idx)) != 0;
	if (!wanted)
		return;

	/* never stall the audio thread behind a plugin being loaded */
	std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
	if (!lock.owns_lock())
		return;

	channels = std::min<size_t>(channels, MAX_AUDIO_CHANNELS);
	frames = std::min<size_t>(frames, AUDIO_OUTPUT_FRAMES);

	for (Slot &s : slots) {
		if (!s.plugin || !s.on || !s.plugin->vstLoaded())
			continue;
		float mix = s.mix;
		if (mix <= 0.0f)
			continue;

		bool blend = mix < 1.0f;
		if (blend) {
			for (size_t c = 0; c < channels; c++)
				if (data[c])
					memcpy(&scratch[c * AUDIO_OUTPUT_FRAMES], data[c], frames * sizeof(float));
		}

		struct obs_audio_data ad = {};
		for (size_t c = 0; c < channels; c++)
			ad.data[c] = (uint8_t *)data[c];
		ad.frames = (uint32_t)frames;
		s.plugin->process(&ad);

		if (blend) {
			float dryGain = 1.0f - mix;
			for (size_t c = 0; c < channels; c++) {
				if (!data[c])
					continue;
				const float *dry = &scratch[c * AUDIO_OUTPUT_FRAMES];
				for (size_t f = 0; f < frames; f++)
					data[c][f] = dry[f] * dryGain + data[c][f] * mix;
			}
		}
	}
}
