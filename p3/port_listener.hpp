#pragma once

#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501
#endif

#include <list>

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

#include "session.hpp"

struct Destination {
	std::string host;
	unsigned short port;
};

class PortListener: public boost::noncopyable
{
public:
	PortListener(boost::asio::io_service& io_service, unsigned short port, const std::list<Destination>* destinations);
	~PortListener();

	boost::asio::io_service* get_sevice();

	void stop_listening();
	void terminate_sessions();

	void handle_session_close(boost::shared_ptr<Session> session);
private:
    void handle_accept(const boost::system::error_code& err, boost::shared_ptr<Session> session);
	void accept();

	boost::asio::ip::tcp::acceptor _acceptor;
	std::list<Destination> _destinations;
	std::list<boost::shared_ptr<Session>> _sessions;
};
