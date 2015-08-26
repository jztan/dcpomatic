/*
    Copyright (C) 2014 Carl Hetherington <cth@carlh.net>

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

/** @file  src/dcp_decoder.h
 *  @brief A decoder of existing DCPs.
 */

#include "video_decoder.h"
#include "audio_decoder.h"
#include "subtitle_decoder.h"

namespace dcp {
	class Reel;
}

class DCPContent;
struct dcp_subtitle_within_dcp_test;

class DCPDecoder : public VideoDecoder, public AudioDecoder, public SubtitleDecoder
{
public:
	DCPDecoder (boost::shared_ptr<const DCPContent>);

private:
	friend struct dcp_subtitle_within_dcp_test;

	bool pass ();
	void seek (ContentTime t, bool accurate);

	std::list<ContentTimePeriod> image_subtitles_during (ContentTimePeriod, bool starting) const;
	std::list<ContentTimePeriod> text_subtitles_during (ContentTimePeriod, bool starting) const;

	ContentTime _next;
	std::list<boost::shared_ptr<dcp::Reel> > _reels;
	std::list<boost::shared_ptr<dcp::Reel> >::iterator _reel;
	boost::shared_ptr<const DCPContent> _dcp_content;
};
