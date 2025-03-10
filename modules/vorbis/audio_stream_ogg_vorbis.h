/*************************************************************************/
/*  audio_stream_ogg_vorbis.h                                            */
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

#ifndef AUDIO_STREAM_OGG_VORBIS_H
#define AUDIO_STREAM_OGG_VORBIS_H

#include "core/variant/variant.h"
#include "modules/ogg/ogg_packet_sequence.h"
#include "servers/audio/audio_stream.h"
#include "thirdparty/libvorbis/vorbis/codec.h"

class AudioStreamOGGVorbis;

class AudioStreamPlaybackOGGVorbis : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackOGGVorbis, AudioStreamPlaybackResampled);

	uint32_t frames_mixed = 0;
	bool active = false;
	int loops = 0;

	enum {
		FADE_SIZE = 256
	};
	AudioFrame loop_fade[FADE_SIZE];
	int loop_fade_remaining = FADE_SIZE;

	vorbis_info info;
	vorbis_comment comment;
	vorbis_dsp_state dsp_state;
	vorbis_block block;

	bool info_is_allocated = false;
	bool comment_is_allocated = false;
	bool dsp_state_is_allocated = false;
	bool block_is_allocated = false;

	bool ready = false;

	bool have_samples_left = false;
	bool have_packets_left = false;

	friend class AudioStreamOGGVorbis;

	Ref<OGGPacketSequence> vorbis_data;
	Ref<OGGPacketSequencePlayback> vorbis_data_playback;
	Ref<AudioStreamOGGVorbis> vorbis_stream;

	int _mix_frames(AudioFrame *p_buffer, int p_frames);
	int _mix_frames_vorbis(AudioFrame *p_buffer, int p_frames);

	// Allocates vorbis data structures. Returns true upon success, false on failure.
	bool _alloc_vorbis();

protected:
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override;

public:
	virtual void start(float p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override;

	virtual int get_loop_count() const override; //times it looped

	virtual float get_playback_position() const override;
	virtual void seek(float p_time) override;

	virtual void tag_used_streams() override;

	AudioStreamPlaybackOGGVorbis() {}
	~AudioStreamPlaybackOGGVorbis();
};

class AudioStreamOGGVorbis : public AudioStream {
	GDCLASS(AudioStreamOGGVorbis, AudioStream);
	OBJ_SAVE_TYPE(AudioStream); // Saves derived classes with common type so they can be interchanged.
	RES_BASE_EXTENSION("oggvorbisstr");

	friend class AudioStreamPlaybackOGGVorbis;

	int channels = 1;
	float length = 0.0;
	bool loop = false;
	float loop_offset = 0.0;

	// Performs a seek to the beginning of the stream, should not be called during playback!
	// Also causes allocation and deallocation.
	void maybe_update_info();

	Ref<OGGPacketSequence> packet_sequence;

	double bpm = 0;
	int beat_count = 0;
	int bar_beats = 4;

protected:
	static void _bind_methods();

public:
	void set_loop(bool p_enable);
	virtual bool has_loop() const override;

	void set_loop_offset(float p_seconds);
	float get_loop_offset() const;

	void set_bpm(double p_bpm);
	virtual double get_bpm() const override;

	void set_beat_count(int p_beat_count);
	virtual int get_beat_count() const override;

	void set_bar_beats(int p_bar_beats);
	virtual int get_bar_beats() const override;

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override;

	void set_packet_sequence(Ref<OGGPacketSequence> p_packet_sequence);
	Ref<OGGPacketSequence> get_packet_sequence() const;

	virtual float get_length() const override; //if supported, otherwise return 0

	virtual bool is_monophonic() const override;

	AudioStreamOGGVorbis();
	virtual ~AudioStreamOGGVorbis();
};

#endif // AUDIO_STREAM_OGG_VORBIS_H
