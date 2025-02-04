/*
    Copyright (C) 2017-2018 Carl Hetherington <cth@carlh.net>

    This file is part of DCP-o-matic.

    DCP-o-matic is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DCP-o-matic is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DCP-o-matic.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "ffmpeg_encoder.h"
#include "film.h"
#include "job.h"
#include "player.h"
#include "player_video.h"
#include "log.h"
#include "image.h"
#include "cross.h"
#include "butler.h"
#include "compose.hpp"
#include <iostream>

#include "i18n.h"

using std::string;
using std::runtime_error;
using std::cout;
using std::pair;
using std::list;
using std::map;
using boost::shared_ptr;
using boost::bind;
using boost::weak_ptr;

FFmpegEncoder::FFmpegEncoder (
	shared_ptr<const Film> film,
	weak_ptr<Job> job,
	boost::filesystem::path output,
	ExportFormat format,
	bool mixdown_to_stereo,
	bool split_reels,
	int x264_crf
	)
	: Encoder (film, job)
	, _history (1000)
{
	int const files = split_reels ? film->reels().size() : 1;
	for (int i = 0; i < files; ++i) {

		boost::filesystem::path filename = output;
		string extension = boost::filesystem::extension (filename);
		filename = boost::filesystem::change_extension (filename, "");

		if (files > 1) {
			/// TRANSLATORS: _reel%1 here is to be added to an export filename to indicate
			/// which reel it is.  Preserve the %1; it will be replaced with the reel number.
			filename = filename.string() + String::compose(_("_reel%1"), i + 1);
		}

		_file_encoders.push_back (
			FileEncoderSet (
				_film->frame_size(),
				_film->video_frame_rate(),
				_film->audio_frame_rate(),
				mixdown_to_stereo ? 2 : film->audio_channels(),
				format,
				x264_crf,
				_film->three_d(),
				filename,
				extension
				)
			);
	}

	_player->set_always_burn_open_subtitles ();
	_player->set_play_referenced ();

	int const ch = film->audio_channels ();

	AudioMapping map;
	if (mixdown_to_stereo) {
		_output_audio_channels = 2;
		map = AudioMapping (ch, 2);
		float const overall_gain = 2 / (4 + sqrt(2));
		float const minus_3dB = 1 / sqrt(2);
		map.set (dcp::LEFT,   0, overall_gain);
		map.set (dcp::RIGHT,  1, overall_gain);
		map.set (dcp::CENTRE, 0, overall_gain * minus_3dB);
		map.set (dcp::CENTRE, 1, overall_gain * minus_3dB);
		map.set (dcp::LS,     0, overall_gain);
		map.set (dcp::RS,     1, overall_gain);
	} else {
		_output_audio_channels = ch;
		map = AudioMapping (ch, ch);
		for (int i = 0; i < ch; ++i) {
			map.set (i, i, 1);
		}
	}

	_butler.reset (new Butler(_player, map, _output_audio_channels, bind(&PlayerVideo::force, _1, FFmpegFileEncoder::pixel_format(format)), true, false));
}

void
FFmpegEncoder::go ()
{
	{
		shared_ptr<Job> job = _job.lock ();
		DCPOMATIC_ASSERT (job);
		job->sub (_("Encoding"));
	}

	Waker waker;

	list<DCPTimePeriod> reel_periods = _film->reels ();
	list<DCPTimePeriod>::const_iterator reel = reel_periods.begin ();
	list<FileEncoderSet>::iterator encoder = _file_encoders.begin ();

	DCPTime const video_frame = DCPTime::from_frames (1, _film->video_frame_rate ());
	int const audio_frames = video_frame.frames_round(_film->audio_frame_rate());
	float* interleaved = new float[_output_audio_channels * audio_frames];
	shared_ptr<AudioBuffers> deinterleaved (new AudioBuffers (_output_audio_channels, audio_frames));
	int const gets_per_frame = _film->three_d() ? 2 : 1;
	for (DCPTime i; i < _film->length(); i += video_frame) {

		if (_file_encoders.size() > 1 && !reel->contains(i)) {
			/* Next reel and file */
			++reel;
			++encoder;
			DCPOMATIC_ASSERT (reel != reel_periods.end());
			DCPOMATIC_ASSERT (encoder != _file_encoders.end());
		}

		for (int j = 0; j < gets_per_frame; ++j) {
			Butler::Error e;
			pair<shared_ptr<PlayerVideo>, DCPTime> v = _butler->get_video (&e);
			if (!v.first) {
				throw ProgrammingError(__FILE__, __LINE__, String::compose("butler returned no video; error was %1", static_cast<int>(e)));
			}
			shared_ptr<FFmpegFileEncoder> fe = encoder->get (v.first->eyes());
			if (fe) {
				fe->video(v.first, v.second);
			}
		}

		_history.event ();

		{
			boost::mutex::scoped_lock lm (_mutex);
			_last_time = i;
		}

		shared_ptr<Job> job = _job.lock ();
		if (job) {
			job->set_progress (float(i.get()) / _film->length().get());
		}

		waker.nudge ();

		_butler->get_audio (interleaved, audio_frames);
		/* XXX: inefficient; butler interleaves and we deinterleave again */
		float* p = interleaved;
		for (int j = 0; j < audio_frames; ++j) {
			for (int k = 0; k < _output_audio_channels; ++k) {
				deinterleaved->data(k)[j] = *p++;
			}
		}
		encoder->audio (deinterleaved);
	}
	delete[] interleaved;

	BOOST_FOREACH (FileEncoderSet i, _file_encoders) {
		i.flush ();
	}

	_butler->rethrow ();
}

float
FFmpegEncoder::current_rate () const
{
	return _history.rate ();
}

Frame
FFmpegEncoder::frames_done () const
{
	boost::mutex::scoped_lock lm (_mutex);
	return _last_time.frames_round (_film->video_frame_rate ());
}

FFmpegEncoder::FileEncoderSet::FileEncoderSet (
	dcp::Size video_frame_size,
	int video_frame_rate,
	int audio_frame_rate,
	int channels,
	ExportFormat format,
	int x264_crf,
	bool three_d,
	boost::filesystem::path output,
	string extension
	)
{
	if (three_d) {
		/// TRANSLATORS: L here is an abbreviation for "left", to indicate the left-eye part of a 3D export
		_encoders[EYES_LEFT] = shared_ptr<FFmpegFileEncoder>(
			new FFmpegFileEncoder(video_frame_size, video_frame_rate, audio_frame_rate, channels, format, x264_crf, String::compose("%1_%2%3", output.string(), _("L"), extension))
			);
		/// TRANSLATORS: R here is an abbreviation for "left", to indicate the left-eye part of a 3D export
		_encoders[EYES_RIGHT] = shared_ptr<FFmpegFileEncoder>(
			new FFmpegFileEncoder(video_frame_size, video_frame_rate, audio_frame_rate, channels, format, x264_crf, String::compose("%1_%2%3", output.string(), _("R"), extension))
			);
	} else {
		_encoders[EYES_BOTH]  = shared_ptr<FFmpegFileEncoder>(
			new FFmpegFileEncoder(video_frame_size, video_frame_rate, audio_frame_rate, channels, format, x264_crf, String::compose("%1%2", output.string(), extension))
			);
	}
}

shared_ptr<FFmpegFileEncoder>
FFmpegEncoder::FileEncoderSet::get (Eyes eyes) const
{
	if (_encoders.size() == 1) {
		/* We are doing a 2D export... */
		if (eyes == EYES_LEFT) {
			/* ...but we got some 3D data; put the left eye into the output... */
			eyes = EYES_BOTH;
		} else if (eyes == EYES_RIGHT) {
			/* ...and ignore the right eye.*/
			return shared_ptr<FFmpegFileEncoder>();
		}
	}

	map<Eyes, boost::shared_ptr<FFmpegFileEncoder> >::const_iterator i = _encoders.find (eyes);
	DCPOMATIC_ASSERT (i != _encoders.end());
	return i->second;
}

void
FFmpegEncoder::FileEncoderSet::flush ()
{
	for (map<Eyes, boost::shared_ptr<FFmpegFileEncoder> >::iterator i = _encoders.begin(); i != _encoders.end(); ++i) {
		i->second->flush ();
	}
}

void
FFmpegEncoder::FileEncoderSet::audio (shared_ptr<AudioBuffers> a)
{
	for (map<Eyes, boost::shared_ptr<FFmpegFileEncoder> >::iterator i = _encoders.begin(); i != _encoders.end(); ++i) {
		i->second->audio (a);
	}
}
