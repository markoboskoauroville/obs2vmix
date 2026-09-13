/******************************************************************************
    obs2vmix: the master FX chain

    Eight VST 2.x slots in signal order, run over libobs's mixed tracks
    through obs_set_master_audio_processor(): the audio every output and
    encoder reads. Live = track 1 only; Live + Record = also the tracks the
    recording uses. The plugins are hosted by obs-vst's VSTPlugin class,
    outside of any filter.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.hpp>
#include <VSTPlugin.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class MasterChain {
public:
	static constexpr int SLOTS = 8;

	enum class Apply { Live = 0, LiveAndRecord = 1 };

	struct Slot {
		std::string path;
		std::string name;
		bool on = true;
		float mix = 1.0f;
		std::unique_ptr<VSTPlugin> plugin;
	};

	MasterChain();
	~MasterChain();

	/* the libobs hook */
	void Install();
	void Remove();

	/* slots (UI thread) */
	bool IsEmpty(int i) const;
	std::string SlotPath(int i) const;
	std::string SlotName(int i) const;
	bool SlotOn(int i) const;
	float SlotMix(int i) const;
	VSTPlugin *Plugin(int i);

	bool SetPlugin(int i, const std::string &path, const std::string &name);
	void ClearSlot(int i);
	void SetOn(int i, bool on);
	void SetMix(int i, float mix);
	void Move(int from, int to);

	void SetApply(Apply a) { apply = (int)a; }
	Apply GetApply() const { return (Apply)apply.load(); }
	void SetBypass(bool b) { bypass = b; }
	bool GetBypass() const { return bypass.load(); }
	void SetRecordTracks(uint32_t mask) { recordTracks = mask; }

	/* presets: the whole rack incl. every plugin's state */
	OBSDataAutoRelease Save() const;
	void Load(obs_data_t *data);

private:
	static void Hook(void *param, size_t mix_idx, float **data, size_t channels, size_t frames,
			 uint32_t sample_rate);
	void Process(size_t mix_idx, float **data, size_t channels, size_t frames);

	mutable std::mutex mutex;
	std::array<Slot, SLOTS> slots;
	std::atomic<int> apply{0};
	std::atomic<bool> bypass{false};
	std::atomic<uint32_t> recordTracks{1};
	std::vector<float> scratch;
	bool installed = false;
};
