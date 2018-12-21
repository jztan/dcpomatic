/*
    Copyright (C) 2018 Carl Hetherington <cth@carlh.net>

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

#include "i18n_hook.h"
#include "instant_i18n_dialog.h"
#include "wx_util.h"
#include "lib/cross.h"
#include <wx/wx.h>
#include <boost/bind.hpp>

using std::map;
using std::string;

map<string, string> I18NHook::_translations;

I18NHook::I18NHook (wxWindow* window)
	: _window (window)
{
	_window->Bind (wxEVT_MIDDLE_DOWN, bind(&I18NHook::handle, this, _1));
}

void
I18NHook::handle (wxMouseEvent& ev)
{
	wxString const original = get_text ();

	InstantI18NDialog* d = new InstantI18NDialog (_window, get_text());
	d->ShowModal();
	set_text (d->get());
	d->Destroy ();

	wxWindow* w = _window;
	while (w) {
		if (w->GetContainingSizer()) {
			w->GetContainingSizer()->Layout();
		}
		w = w->GetParent();
	}

	ev.Skip ();

	_translations[wx_to_std(original)] = wx_to_std(get_text());
}