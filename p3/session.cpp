#include "session.hpp"

#include <stdexcept>

#include <boost/bind.hpp>

#include "port_listener.hpp"

Session::Session(PortListener& listener):
	_listener(&listener), _client_socket(*listener.get_sevice()), _server_socket(*listener.get_sevice()),
	_is_to_be_terminated(false), _client_is_connected(false), _server_is_connected(false),
	_server_sent_eof(false), _client_sent_eof(false)
{
	
}

Session::~Session()
{

}

void Session::termination_routine()
{
	if (!this->_is_to_be_terminated) {
		this->_listener->handle_session_close(this->shared_from_this());
		this->_is_to_be_terminated = true;
	}

	if (this->_client_is_connected) {
		this->_client_socket.close();
		this->_client_is_connected = false;
	}

	if (this->_server_is_connected) {
		this->_server_socket.close();
		this->_server_is_connected = false;
	}
}

void Session::handle_server_connect(const boost::system::error_code& err)
{
	if (!err) {
		this->_server_is_connected = true;
		this->receive_from_client();
		this->receive_from_server();
	} else {
		if (!this->_is_to_be_terminated) {
			this->termination_routine();
		}
	}
}

void Session::handle_receive_from_client(const boost::system::error_code& err, size_t size)
{
	if ((!err) || (err == boost::asio::error::eof)) {
		if (err == boost::asio::error::eof) {
			this->_client_sent_eof = true;
		}

		if (size > 0) {
			this->send_to_server(size);
		} else {
			if (!this->_client_sent_eof) {
				this->receive_from_client();
			}
		}
	} else if (err == boost::asio::error::connection_reset) {
		this->_client_is_connected = false;
		this->termination_routine();
	} else {
		throw std::runtime_error(std::string("Boost exception: ") + err.message());
	}
}

void Session::handle_send_to_client(const boost::system::error_code& err)
{
	if (!err) {
		if (!this->_server_sent_eof) {
			this->receive_from_server();
		}
	} else if (err == boost::asio::error::connection_reset) {
		this->_client_is_connected = false;
		this->termination_routine();
	} else {
		throw std::runtime_error(std::string("Boost exception: ") + err.message());
	}
}

void Session::handle_receive_from_server(const boost::system::error_code& err, size_t size)
{
	if ((!err) || (err == boost::asio::error::eof)) {
		if (err == boost::asio::error::eof) {
			this->_server_sent_eof = true;
		}

		if (size > 0) {
			this->send_to_client(size);
		} else {
			if (!this->_server_sent_eof) {
				this->receive_from_server();
			}
		}
	} else if (err == boost::asio::error::connection_reset) {
		this->_server_is_connected = false;
		this->termination_routine();
	} else {
		throw std::runtime_error(std::string("Boost exception: ") + err.message());
	}
}

void Session::handle_send_to_server(const boost::system::error_code& err)
{
	if (!err) {
		if (!this->_client_sent_eof) {
			this->receive_from_client();
		}
	} else if (err == boost::asio::error::connection_reset) {
		this->_server_is_connected = false;
		this->termination_routine();
	} else {
		throw std::runtime_error(std::string("Boost exception: ") + err.message());
	}
}

void Session::receive_from_client()
{
	this->_server_socket.async_receive(boost::asio::buffer(this->_client_read_buffer, BUFFER_SIZE), boost::bind(&Session::handle_receive_from_client, this, _1, _2));
}

void Session::send_to_client(size_t size)
{
	this->_client_socket.async_send(boost::asio::buffer(this->_server_read_buffer, size), boost::bind(&Session::handle_send_to_client, this, _1));
}

void Session::receive_from_server()
{
	this->_server_socket.async_receive(boost::asio::buffer(this->_server_read_buffer, BUFFER_SIZE), boost::bind(&Session::handle_receive_from_server, this, _1, _2));
}

void Session::send_to_server(size_t size)
{
	this->_server_socket.async_send(boost::asio::buffer(this->_client_read_buffer, size), boost::bind(&Session::handle_send_to_server, this, _1));
}

boost::asio::ip::tcp::socket* Session::server_socket()
{
	return &(this->_server_socket);
}

boost::asio::ip::tcp::socket* Session::client_socket()
{
	return &(this->_client_socket);
}

void Session::client_start(const std::string& host, unsigned short port)
{
	this->_client_is_connected = true;
	this->_client_socket.async_connect(
		boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(host), port),
		boost::bind(&Session::handle_server_connect, this, _1)
	);
}

void Session::terminate()
{
	this->_is_to_be_terminated = true;
	this->termination_routine();
}
