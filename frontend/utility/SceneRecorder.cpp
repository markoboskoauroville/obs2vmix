/******************************************************************************
    obs2vmix: record one scene to its own file, in the background

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "SceneRecorder.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <util/platform.h>
#include <util/config-file.h>

#include <QDateTime>
#include <QString>

#include <filesystem>

extern const char *get_simple_output_encoder(const char *name);
extern bool EncoderAvailable(const char *encoder);

static const uint64_t LOW_DISK_BYTES = 1024ULL * 1024ULL * 1024ULL; /* 1 GB: the line turns red */

/* ---------------------------------------------------------------------- */

SceneRecorder::SceneRecorder(obs_source_t *scene_, QObject *parent) : QObject(parent), scene(OBSGetWeakRef(scene_))
{
	const char *name = obs_source_get_name(scene_);
	sceneName = name ? name : "scene";
}

SceneRecorder::~SceneRecorder()
{
	Teardown();
}

bool SceneRecorder::Active() const
{
	return output && obs_output_active(output);
}

OBSSource SceneRecorder::Scene() const
{
	return OBSGetStrongRef(scene);
}

/* ---------------------------------------------------------------------- */
/* what Settings -> Output says about recordings                           */

static std::string safeFileName(const std::string &in)
{
	std::string out;
	for (char c : in) {
		switch (c) {
		case '/':
		case '\\':
		case ':':
		case '*':
		case '?':
		case '"':
		case '<':
		case '>':
		case '|':
			out.push_back('_');
			break;
		default:
			out.push_back(c);
		}
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
		out.pop_back();
	return out.empty() ? "scene" : out;
}

static OBSDataAutoRelease profileJson(const char *file)
{
	OBSBasic *main = OBSBasic::Get();
	const OBSProfile &profile = main->GetCurrentProfile();
	std::filesystem::path p = profile.path / std::filesystem::u8path(file);
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(p.u8string().c_str(), "bak");
	if (!data)
		data = obs_data_create();
	return data;
}

bool SceneRecorder::BuildVideoEncoder()
{
	OBSBasic *main = OBSBasic::Get();
	config_t *config = main->Config();
	const char *mode = config_get_string(config, "Output", "Mode");
	bool advanced = mode && strcmp(mode, "Advanced") == 0;

	std::string encoderId;
	OBSDataAutoRelease settings;

	if (advanced) {
		const char *recEnc = config_get_string(config, "AdvOut", "RecEncoder");
		if (!recEnc || strcmp(recEnc, "none") == 0) {
			encoderId = config_get_string(config, "AdvOut", "Encoder");
			settings = profileJson("streamEncoder.json");
		} else {
			encoderId = recEnc;
			settings = profileJson("recordEncoder.json");
		}
	} else {
		const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");
		bool stream = !quality || strcmp(quality, "Stream") == 0;
		bool hq = quality && strcmp(quality, "HQ") == 0;
		const char *encName =
			config_get_string(config, "SimpleOutput", stream ? "StreamEncoder" : "RecEncoder");
		encoderId = get_simple_output_encoder(encName ? encName : "");
		settings = obs_data_create();

		if (stream) {
			obs_data_set_string(settings, "rate_control", "CBR");
			obs_data_set_int(settings, "bitrate", config_get_int(config, "SimpleOutput", "VBitrate"));
		} else {
			/* the same quality rules as the simple output's recording presets */
			int q = hq ? 16 : 23;
			if (encoderId == "obs_x264") {
				obs_data_set_string(settings, "rate_control", "CRF");
				obs_data_set_int(settings, "crf", q);
				obs_data_set_bool(settings, "use_bufsize", true);
				obs_data_set_string(settings, "profile", "high");
				obs_data_set_string(settings, "preset", "veryfast");
			} else if (encoderId.rfind("com.apple", 0) == 0) {
				obs_data_set_string(settings, "rate_control", "CRF");
				obs_data_set_int(settings, "quality", hq ? 70 : 50);
			} else if (encoderId.find("qsv") != std::string::npos) {
				obs_data_set_string(settings, "rate_control", "CQP");
				obs_data_set_int(settings, "cqp", q);
			} else {
				/* nvenc, amf and the rest take CQP */
				obs_data_set_string(settings, "rate_control", "CQP");
				obs_data_set_int(settings, "cqp", q);
			}
		}
	}

	if (encoderId.empty() || !EncoderAvailable(encoderId.c_str())) {
		encoderId = "obs_x264";
		settings = obs_data_create();
		obs_data_set_string(settings, "rate_control", "CRF");
		obs_data_set_int(settings, "crf", 23);
		obs_data_set_string(settings, "preset", "veryfast");
	}

	std::string name = "obs2vmix_video_" + sceneName;
	videoEncoder = obs_video_encoder_create(encoderId.c_str(), name.c_str(), settings, nullptr);
	if (!videoEncoder) {
		error = "Could not create the video encoder " + encoderId;
		return false;
	}
	obs_encoder_set_video(videoEncoder, video);
	return true;
}

bool SceneRecorder::BuildAudioEncoder()
{
	OBSBasic *main = OBSBasic::Get();
	config_t *config = main->Config();
	const char *mode = config_get_string(config, "Output", "Mode");
	bool advanced = mode && strcmp(mode, "Advanced") == 0;

	/* the main mix: track 1, or the first recording track of the advanced output */
	size_t mixer = 0;
	if (advanced) {
		int tracks = (int)config_get_int(config, "AdvOut", "RecTracks");
		for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
			if (tracks & (1 << i)) {
				mixer = (size_t)i;
				break;
			}
		}
	}

	const char *id = EncoderAvailable("CoreAudio_AAC") ? "CoreAudio_AAC" : "ffmpeg_aac";
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", 192);
	obs_data_set_string(settings, "rate_control", "CBR");

	std::string name = "obs2vmix_audio_" + sceneName;
	audioEncoder = obs_audio_encoder_create(id, name.c_str(), settings, mixer, nullptr);
	if (!audioEncoder) {
		error = "Could not create the audio encoder";
		return false;
	}
	obs_encoder_set_audio(audioEncoder, obs_get_audio());
	return true;
}

/* ---------------------------------------------------------------------- */

bool SceneRecorder::Start()
{
	OBSSource src = OBSGetStrongRef(scene);
	if (!src) {
		error = "The scene is gone";
		return false;
	}
	if (Active())
		return true;

	OBSBasic *main = OBSBasic::Get();
	config_t *config = main->Config();
	const char *mode = config_get_string(config, "Output", "Mode");
	bool advanced = mode && strcmp(mode, "Advanced") == 0;

	/* folder, container, muxer */
	const char *dir = main->GetCurrentOutputPath();
	if (!dir || !*dir) {
		error = "No recording folder is set (Settings -> Output)";
		return false;
	}
	directory = dir;

	const char *format = config_get_string(config, advanced ? "AdvOut" : "SimpleOutput", "RecFormat2");
	if (!format || !*format)
		format = "mkv";
	const char *muxId = "ffmpeg_muxer";
	if (strcmp(format, "hybrid_mp4") == 0)
		muxId = "mp4_output";
	else if (strcmp(format, "hybrid_mov") == 0)
		muxId = "mov_output";

	std::string ext = GetFormatExt(format);
	if (ext.empty())
		ext = "mkv";

	/* <scene> <date> <time>.<ext>, never over an existing file */
	QDateTime now = QDateTime::currentDateTime();
	std::string base = safeFileName(sceneName) + " " + now.toString("yyyy-MM-dd HH-mm-ss").toStdString();
	std::string candidate = directory + "/" + base + "." + ext;
	for (int n = 2; os_file_exists(candidate.c_str()) && n < 100; n++)
		candidate = directory + "/" + base + " (" + std::to_string(n) + ")." + ext;
	path = candidate;

	/* this scene's own render */
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		error = "No video";
		return false;
	}
	fpsNominal = ovi.fps_den > 0 ? (double)ovi.fps_num / (double)ovi.fps_den : 30.0;

	view = obs_view_create();
	obs_view_set_source(view, 0, src);
	video = obs_view_add2(view, &ovi);
	if (!video) {
		error = "Could not add a video mix for the scene";
		Teardown();
		return false;
	}

	if (!BuildVideoEncoder() || !BuildAudioEncoder()) {
		Teardown();
		return false;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "path", path.c_str());
	const char *mux = config_get_string(config, advanced ? "AdvOut" : "SimpleOutput",
					    advanced ? "RecMuxerCustom" : "MuxerCustom");
	if (strncmp(format, "fragmented", 10) == 0 && (!mux || !strstr(mux, "movflags"))) {
		std::string frag = "movflags=frag_keyframe+empty_moov+delay_moov";
		if (mux && *mux) {
			frag += " ";
			frag += mux;
		}
		obs_data_set_string(settings, "muxer_settings", frag.c_str());
	} else if (mux) {
		obs_data_set_string(settings, "muxer_settings", mux);
	}

	std::string name = "obs2vmix_record_" + sceneName;
	output = obs_output_create(muxId, name.c_str(), settings, nullptr);
	if (!output) {
		error = std::string("Could not create the output ") + muxId;
		Teardown();
		return false;
	}

	obs_output_set_media(output, video, obs_get_audio());
	obs_output_set_video_encoder(output, videoEncoder);
	obs_output_set_audio_encoder(output, audioEncoder, 0);

	stopSignal.Connect(obs_output_get_signal_handler(output), "stop", SceneRecorder::OnStop, this);

	startNs = os_gettime_ns();
	lastPollNs = startNs;
	lastFrames = 0;
	laggedAtStart = obs_get_lagged_frames();
	stopped = false;

	if (!obs_output_start(output)) {
		const char *err = obs_output_get_last_error(output);
		error = err && *err ? err : "The output did not start";
		stopped = true;
		Teardown();
		return false;
	}

	blog(LOG_INFO, "[obs2vmix] recording scene '%s' to '%s'", sceneName.c_str(), path.c_str());
	return true;
}

void SceneRecorder::Stop(bool force)
{
	if (!output)
		return;
	if (force)
		obs_output_force_stop(output);
	else
		obs_output_stop(output);
}

void SceneRecorder::OnStop(void *data, calldata_t *cd)
{
	SceneRecorder *self = static_cast<SceneRecorder *>(data);
	int code = (int)calldata_int(cd, "code");
	self->stopped = true;
	QMetaObject::invokeMethod(
		self, [self, code]() { emit self->Stopped(self, code); }, Qt::QueuedConnection);
}

void SceneRecorder::Teardown()
{
	stopSignal.Disconnect();

	if (output) {
		if (obs_output_active(output))
			obs_output_force_stop(output);
		output = nullptr;
	}
	videoEncoder = nullptr;
	audioEncoder = nullptr;

	if (view) {
		if (video)
			obs_view_remove(view);
		obs_view_set_source(view, 0, nullptr);
		obs_view_destroy(view);
		view = nullptr;
		video = nullptr;
	}
}

/* ---------------------------------------------------------------------- */

SceneRecorder::Stats SceneRecorder::Poll()
{
	Stats s;
	if (!output || !video)
		return s;

	uint64_t now = os_gettime_ns();
	s.elapsedMs = (now - startNs) / 1000000ULL;

	s.freeBytes = os_get_free_disk_space(directory.c_str());
	s.lowDisk = s.freeBytes < LOW_DISK_BYTES;

	uint32_t frames = video_output_get_total_frames(video);
	double dt = (double)(now - lastPollNs) / 1000000000.0;
	if (dt >= 0.5) {
		s.fps = (double)(frames - lastFrames) / dt;
		lastFrames = frames;
		lastPollNs = now;
		lastFps = s.fps;
	} else {
		s.fps = lastFps;
	}
	if (s.elapsedMs < 1500)
		s.fps = fpsNominal;

	uint32_t lagged = obs_get_lagged_frames();
	s.dropped = video_output_get_skipped_frames(video) + (lagged >= laggedAtStart ? lagged - laggedAtStart : 0);
	return s;
}
