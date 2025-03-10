/*************************************************************************/
/*  movie_writer.cpp                                                     */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "movie_writer.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "servers/display_server.h"

MovieWriter *MovieWriter::writers[MovieWriter::MAX_WRITERS];
uint32_t MovieWriter::writer_count = 0;

void MovieWriter::add_writer(MovieWriter *p_writer) {
	ERR_FAIL_COND(writer_count == MAX_WRITERS);
	writers[writer_count++] = p_writer;
}

MovieWriter *MovieWriter::find_writer_for_file(const String &p_file) {
	for (int32_t i = writer_count - 1; i >= 0; i--) { // More recent last, to have override ability.
		if (writers[i]->handles_file(p_file)) {
			return writers[i];
		}
	}
	return nullptr;
}

uint32_t MovieWriter::get_audio_mix_rate() const {
	uint32_t ret = 0;
	if (GDVIRTUAL_REQUIRED_CALL(_get_audio_mix_rate, ret)) {
		return ret;
	}
	return 48000;
}
AudioServer::SpeakerMode MovieWriter::get_audio_speaker_mode() const {
	AudioServer::SpeakerMode ret = AudioServer::SPEAKER_MODE_STEREO;
	if (GDVIRTUAL_REQUIRED_CALL(_get_audio_speaker_mode, ret)) {
		return ret;
	}
	return AudioServer::SPEAKER_MODE_STEREO;
}

Error MovieWriter::write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
	Error ret = OK;
	if (GDVIRTUAL_REQUIRED_CALL(_write_begin, p_movie_size, p_fps, p_base_path, ret)) {
		return ret;
	}
	return ERR_UNCONFIGURED;
}

Error MovieWriter::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
	Error ret = OK;
	if (GDVIRTUAL_REQUIRED_CALL(_write_frame, p_image, p_audio_data, ret)) {
		return ret;
	}
	return ERR_UNCONFIGURED;
}

void MovieWriter::write_end() {
	GDVIRTUAL_REQUIRED_CALL(_write_end);
}

bool MovieWriter::handles_file(const String &p_path) const {
	bool ret = false;
	if (GDVIRTUAL_REQUIRED_CALL(_handles_file, p_path, ret)) {
		return ret;
	}
	return false;
}

void MovieWriter::get_supported_extensions(List<String> *r_extensions) const {
	Vector<String> exts;
	if (GDVIRTUAL_REQUIRED_CALL(_get_supported_extensions, exts)) {
		for (int i = 0; i < exts.size(); i++) {
			r_extensions->push_back(exts[i]);
		}
	}
}

void MovieWriter::begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
	project_name = GLOBAL_GET("application/config/name");
	mix_rate = get_audio_mix_rate();
	AudioDriverDummy::get_dummy_singleton()->set_mix_rate(mix_rate);
	AudioDriverDummy::get_dummy_singleton()->set_speaker_mode(AudioDriver::SpeakerMode(get_audio_speaker_mode()));
	fps = p_fps;
	if ((mix_rate % fps) != 0) {
		WARN_PRINT("MovieWriter's audio mix rate (" + itos(mix_rate) + ") can not be divided by the recording FPS (" + itos(fps) + "). Audio may go out of sync over time.");
	}

	audio_channels = AudioDriverDummy::get_dummy_singleton()->get_channels();
	audio_mix_buffer.resize(mix_rate * audio_channels / fps);

	write_begin(p_movie_size, p_fps, p_base_path);
}

void MovieWriter::_bind_methods() {
	ClassDB::bind_static_method("MovieWriter", D_METHOD("add_writer", "writer"), &MovieWriter::add_writer);

	GDVIRTUAL_BIND(_get_audio_mix_rate)
	GDVIRTUAL_BIND(_get_audio_speaker_mode)

	GDVIRTUAL_BIND(_handles_file, "path")

	GDVIRTUAL_BIND(_write_begin, "movie_size", "fps", "base_path")
	GDVIRTUAL_BIND(_write_frame, "frame_image", "audio_frame_block")
	GDVIRTUAL_BIND(_write_end)

	GLOBAL_DEF("editor/movie_writer/mix_rate", 48000);
	ProjectSettings::get_singleton()->set_custom_property_info("editor/movie_writer/mix_rate", PropertyInfo(Variant::INT, "editor/movie_writer/mix_rate", PROPERTY_HINT_RANGE, "8000,192000,1,suffix:Hz"));
	GLOBAL_DEF("editor/movie_writer/speaker_mode", 0);
	ProjectSettings::get_singleton()->set_custom_property_info("editor/movie_writer/speaker_mode", PropertyInfo(Variant::INT, "editor/movie_writer/speaker_mode", PROPERTY_HINT_ENUM, "Stereo,3.1,5.1,7.1"));
	GLOBAL_DEF("editor/movie_writer/mjpeg_quality", 0.75);
	ProjectSettings::get_singleton()->set_custom_property_info("editor/movie_writer/mjpeg_quality", PropertyInfo(Variant::FLOAT, "editor/movie_writer/mjpeg_quality", PROPERTY_HINT_RANGE, "0.01,1.0,0.01"));
	// used by the editor
	GLOBAL_DEF_BASIC("editor/movie_writer/movie_file", "");
	GLOBAL_DEF_BASIC("editor/movie_writer/disable_vsync", false);
	GLOBAL_DEF_BASIC("editor/movie_writer/fps", 60);
	ProjectSettings::get_singleton()->set_custom_property_info("editor/movie_writer/fps", PropertyInfo(Variant::INT, "editor/movie_writer/fps", PROPERTY_HINT_RANGE, "1,300,1,suffix:FPS"));
}

void MovieWriter::set_extensions_hint() {
	RBSet<String> found;
	for (uint32_t i = 0; i < writer_count; i++) {
		List<String> extensions;
		writers[i]->get_supported_extensions(&extensions);
		for (const String &ext : extensions) {
			found.insert(ext);
		}
	}

	String ext_hint;

	for (const String &S : found) {
		if (ext_hint != "") {
			ext_hint += ",";
		}
		ext_hint += "*." + S;
	}
	ProjectSettings::get_singleton()->set_custom_property_info("editor/movie_writer/movie_file", PropertyInfo(Variant::STRING, "editor/movie_writer/movie_file", PROPERTY_HINT_GLOBAL_SAVE_FILE, ext_hint));
}

void MovieWriter::add_frame(const Ref<Image> &p_image) {
	const int movie_time_seconds = Engine::get_singleton()->get_frames_drawn() / fps;
	const String movie_time = vformat("%s:%s:%s",
			String::num(movie_time_seconds / 3600).pad_zeros(2),
			String::num((movie_time_seconds % 3600) / 60).pad_zeros(2),
			String::num(movie_time_seconds % 60).pad_zeros(2));

#ifdef DEBUG_ENABLED
	DisplayServer::get_singleton()->window_set_title(vformat("MovieWriter: Frame %d (time: %s) - %s (DEBUG)", Engine::get_singleton()->get_frames_drawn(), movie_time, project_name));
#else
	DisplayServer::get_singleton()->window_set_title(vformat("MovieWriter: Frame %d (time: %s) - %s", Engine::get_singleton()->get_frames_drawn(), movie_time, project_name));
#endif

	AudioDriverDummy::get_dummy_singleton()->mix_audio(mix_rate / fps, audio_mix_buffer.ptr());
	write_frame(p_image, audio_mix_buffer.ptr());
}

void MovieWriter::end() {
	write_end();
}
