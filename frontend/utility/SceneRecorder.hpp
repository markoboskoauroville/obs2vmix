/******************************************************************************
    obs2vmix: record one scene to its own file, in the background

    Every recording is its own render of the scene (an obs_view with the
    scene in channel 0 and its own video mix), its own encoders and an
    ffmpeg muxer, independent of what is on Source or Record. Audio is the
    main mix.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.hpp>

#include <QObject>

#include <atomic>
#include <cstdint>
#include <string>

class SceneRecorder : public QObject {
	Q_OBJECT

public:
	explicit SceneRecorder(obs_source_t *scene, QObject *parent = nullptr);
	~SceneRecorder();

	/* false when it could not start; Error() says why */
	bool Start();
	void Stop(bool force = false);

	bool Active() const;
	OBSSource Scene() const;
	const std::string &Error() const { return error; }
	const std::string &Path() const { return path; }

	struct Stats {
		uint64_t elapsedMs = 0;
		uint64_t freeBytes = 0;
		double fps = 0.0;
		uint32_t dropped = 0;
		bool lowDisk = false;
	};

	/* call at a few Hz from a timer; fps is measured between calls */
	Stats Poll();

signals:
	/* emitted on the UI thread once the output has stopped (any reason) */
	void Stopped(SceneRecorder *recorder, int code);

private:
	static void OnStop(void *data, calldata_t *cd);
	bool BuildVideoEncoder();
	bool BuildAudioEncoder();
	void Teardown();

	OBSWeakSource scene;
	std::string sceneName;
	std::string error;
	std::string path;
	std::string directory;

	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	OBSEncoderAutoRelease videoEncoder;
	OBSEncoderAutoRelease audioEncoder;
	OBSOutputAutoRelease output;
	OBSSignal stopSignal;

	double fpsNominal = 30.0;
	uint64_t startNs = 0;
	uint64_t lastPollNs = 0;
	uint32_t lastFrames = 0;
	uint32_t laggedAtStart = 0;
	double lastFps = 0.0;
	std::atomic<bool> stopped{true};
};
