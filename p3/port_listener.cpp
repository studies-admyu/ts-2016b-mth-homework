#include "port_listener.hpp"

#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

std::random_device rd;

PortListener::PortListener(boost::asio::io_service& io_service, unsigned short port, const std::list<Destination>* destinations):
	_acceptor(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
	_destinations(*destinations)
{
	this->accept();
}

PortListener::~PortListener()
{

}

void PortListener::handle_accept(const boost::system::error_code& err, boost::shared_ptr<Session> session)
{
	if (!err) {
		std::uniform_int_distribution<int> rand_dist(0, this->_destinations.size() - 1);
		auto dest_iterator = this->_destinations.cbegin();

		std::advance(dest_iterator, rand_dist(rd));

		this->_sessions.push_back(session);
		session->client_start(dest_iterator->host, dest_iterator->port);
		this->accept();
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "PortListener::handle_accept (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
    }
}

void PortListener::accept()
{
	boost::shared_ptr<Session> new_session = boost::make_shared<Session>(*this);
	this->_acceptor.async_accept(*new_session->client_socket(), boost::bind(&PortListener::handle_accept, this, _1, new_session));
}

boost::asio::io_service* PortListener::get_sevice()
{
	return &(this->_acceptor.get_io_service());
}

void PortListener::stop_listening()
{
	this->_acceptor.cancel();
}

void PortListener::terminate_sessions()
{
	for ( auto session : this->_sessions ) {
		session->terminate();
	}
	this->_sessions.clear();
}

void PortListener::handle_session_close(boost::shared_ptr<Session> session)
{
	for (auto isession = this->_sessions.begin(); isession != this->_sessions.end(); ++isession) {
		if (*isession == session) {
			this->_sessions.erase(isession);
			break;
		}
	}
}
