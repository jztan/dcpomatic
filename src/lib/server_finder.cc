/*
    Copyright (C) 2013 Carl Hetherington <cth@carlh.net>

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

#include <libcxml/cxml.h>
#include "server_finder.h"
#include "exceptions.h"
#include "util.h"
#include "config.h"
#include "cross.h"
#include "ui_signaller.h"

using std::string;
using std::stringstream;
using std::list;
using boost::shared_ptr;
using boost::scoped_array;

ServerFinder* ServerFinder::_instance = 0;

ServerFinder::ServerFinder ()
	: _disabled (false)
	, _broadcast_thread (0)
	, _listen_thread (0)
{
	_broadcast_thread = new boost::thread (boost::bind (&ServerFinder::broadcast_thread, this));
	_listen_thread = new boost::thread (boost::bind (&ServerFinder::listen_thread, this));
}

void
ServerFinder::broadcast_thread ()
{
	boost::system::error_code error;
	boost::asio::io_service io_service;
	boost::asio::ip::udp::socket socket (io_service);
	socket.open (boost::asio::ip::udp::v4(), error);
	if (error) {
		throw NetworkError ("failed to set up broadcast socket");
	}

        socket.set_option (boost::asio::ip::udp::socket::reuse_address (true));
        socket.set_option (boost::asio::socket_base::broadcast (true));
	
        boost::asio::ip::udp::endpoint end_point (boost::asio::ip::address_v4::broadcast(), Config::instance()->server_port_base() + 1);            

	while (1) {
		string const data = DCPOMATIC_HELLO;
		socket.send_to (boost::asio::buffer (data.c_str(), data.size() + 1), end_point);
		dcpomatic_sleep (10);
	}
}

void
ServerFinder::listen_thread ()
{
	while (1) {
		shared_ptr<Socket> sock (new Socket (10));

		try {
			sock->accept (Config::instance()->server_port_base() + 1);
		} catch (std::exception& e) {
			continue;
		}

		uint32_t length = sock->read_uint32 ();
		scoped_array<char> buffer (new char[length]);
		sock->read (reinterpret_cast<uint8_t*> (buffer.get()), length);
		
		stringstream s (buffer.get());
		shared_ptr<cxml::Document> xml (new cxml::Document ("ServerAvailable"));
		xml->read_stream (s);

		boost::mutex::scoped_lock lm (_mutex);

		string const ip = sock->socket().remote_endpoint().address().to_string ();
		list<ServerDescription>::const_iterator i = _servers.begin();
		while (i != _servers.end() && i->host_name() != ip) {
			++i;
		}

		if (i == _servers.end ()) {
			ServerDescription sd (ip, xml->number_child<int> ("Threads"));
			_servers.push_back (sd);
			ui_signaller->emit (boost::bind (boost::ref (ServerFound), sd));
		}
	}
}

void
ServerFinder::connect (boost::function<void (ServerDescription)> fn)
{
	if (_disabled) {
		return;
	}
	
	boost::mutex::scoped_lock lm (_mutex);

	/* Emit the current list of servers */
	for (list<ServerDescription>::iterator i = _servers.begin(); i != _servers.end(); ++i) {
		fn (*i);
	}

	ServerFound.connect (fn);
}

ServerFinder*
ServerFinder::instance ()
{
	if (!_instance) {
		_instance = new ServerFinder ();
	}

	return _instance;
}

	
       
