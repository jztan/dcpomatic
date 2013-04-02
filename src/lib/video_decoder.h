/*
    Copyright (C) 2012 Carl Hetherington <cth@carlh.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef DVDOMATIC_VIDEO_DECODER_H
#define DVDOMATIC_VIDEO_DECODER_H

#include "video_source.h"
#include "decoder.h"

class VideoContent;

class VideoDecoder : public VideoSource, public virtual Decoder
{
public:
	VideoDecoder (boost::shared_ptr<const Film>);

	/** @return video frames per second, or 0 if unknown */
	virtual float frames_per_second () const = 0;
	/** @return native size in pixels */
	virtual libdcp::Size native_size () const = 0;
	/** @return length according to our content's header */
	virtual ContentVideoFrame video_length () const = 0;

	virtual int time_base_numerator () const = 0;
	virtual int time_base_denominator () const = 0;
	virtual int sample_aspect_ratio_numerator () const = 0;
	virtual int sample_aspect_ratio_denominator () const = 0;

	void set_progress (Job *) const;
	
	int video_frame () const {
		return _video_frame;
	}

	double last_source_time () const {
		return _last_source_time;
	}

protected:
	
	virtual PixelFormat pixel_format () const = 0;

	void emit_video (boost::shared_ptr<Image>, double);
	void emit_subtitle (boost::shared_ptr<TimedSubtitle>);
	bool have_last_video () const;
	void repeat_last_video ();

private:
	void signal_video (boost::shared_ptr<Image>, bool, boost::shared_ptr<Subtitle>);

	int _video_frame;
	double _last_source_time;
	
	boost::shared_ptr<TimedSubtitle> _timed_subtitle;

	boost::shared_ptr<Image> _last_image;
	boost::shared_ptr<Subtitle> _last_subtitle;
};

#endif
