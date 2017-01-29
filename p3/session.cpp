#include "session.hpp"

#include <iostream>
#include <stdexcept>

#include <boost/bind.hpp>

#include "port_listener.hpp"

Session::Session(PortListener& listener):
    _listener(&listener), _server_socket(*listener.get_sevice()), _client_socket(*listener.get_sevice()),
    _is_to_be_terminated(false), _client_is_connected(false), _server_is_connected(false)
{
	
}

Session::~Session()
{

}

void Session::termination_routine(boost::shared_ptr<Session> session)
{
    if (!session->_is_to_be_terminated) {
        session->_listener->handle_session_close(session);
        session->_is_to_be_terminated = true;
	}

    if (session->_client_is_connected) {
        session->_client_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        session->_client_socket.close();
        session->_client_is_connected = false;
    }

    if (session->_server_is_connected) {
        session->_server_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        session->_server_socket.close();
        session->_server_is_connected = false;
    }
}

void Session::handle_server_connect(const boost::system::error_code& err, boost::shared_ptr<Session> session)
{
	if (!err) {
        session->_server_is_connected = true;
        session->receive_from_client();
        session->receive_from_server();
    } else if (err == boost::asio::error::connection_refused) {
        Session::termination_routine(session);
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "Session::handle_server_connect (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
	}
}

void Session::handle_receive_from_client(const boost::system::error_code& err, boost::shared_ptr<Session> session, size_t size)
{
    if (!err) {
		if (size > 0) {
            session->send_to_server(size);
        } else {
            session->receive_from_client();
		}
    } else if (
        (err == boost::asio::error::eof) || (err == boost::asio::error::connection_reset)
    ) {
        session->termination_routine(session);
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "Session::handle_receive_from_client (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
    }
}

void Session::handle_send_to_client(const boost::system::error_code& err, boost::shared_ptr<Session> session)
{
	if (!err) {
        session->receive_from_server();
    } else if (
       (err == boost::asio::error::eof) || (err == boost::asio::error::connection_reset)
    ) {
       session->termination_routine(session);
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "Session::handle_send_to_client (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
    }
}

void Session::handle_receive_from_server(const boost::system::error_code& err, boost::shared_ptr<Session> session, size_t size)
{
    if (!err) {
		if (size > 0) {
            session->send_to_client(size);
		} else {
            session->receive_from_server();
		}
    } else if (
        (err == boost::asio::error::eof) || (err == boost::asio::error::connection_reset)
    ) {
        session->termination_routine(session);
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "Session::handle_receive_from_server (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
    }
}

void Session::handle_send_to_server(const boost::system::error_code& err, boost::shared_ptr<Session> session)
{
	if (!err) {
        session->receive_from_client();
    } else if (
        (err == boost::asio::error::eof) || (err == boost::asio::error::connection_reset)
    ) {
        session->termination_routine(session);
    } else if (err != boost::asio::error::operation_aborted) {
        std::cerr << "Session::handle_send_to_server (" << err.value() << "): " << err.message() << std::endl;
        throw std::runtime_error(std::string("Error: ") + err.message());
    }
}

void Session::receive_from_client()
{
    this->_client_socket.async_receive(boost::asio::buffer(this->_client_read_buffer, BUFFER_SIZE), boost::bind(&Session::handle_receive_from_client, _1, this->shared_from_this(), _2));
}

void Session::send_to_client(size_t size)
{
    this->_client_socket.async_send(boost::asio::buffer(this->_server_read_buffer, size), boost::bind(&Session::handle_send_to_client, _1, this->shared_from_this()));
}

void Session::receive_from_server()
{
    this->_server_socket.async_receive(boost::asio::buffer(this->_server_read_buffer, BUFFER_SIZE), boost::bind(&Session::handle_receive_from_server, _1, this->shared_from_this(), _2));
}

void Session::send_to_server(size_t size)
{
    this->_server_socket.async_send(boost::asio::buffer(this->_client_read_buffer, size), boost::bind(&Session::handle_send_to_server, _1, this->shared_from_this()));
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
    this->_server_socket.async_connect(
		boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(host), port),
        boost::bind(&Session::handle_server_connect, _1, this->shared_from_this())
	);
}

void Session::terminate()
{
	this->_is_to_be_terminated = true;
    this->termination_routine(this->shared_from_this());
}
