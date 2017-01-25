#pragma once

#define BUFFER_SIZE 4096

#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501
#endif

#include <string>

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

class PortListener;

class Session: public boost::enable_shared_from_this<Session>
{
public:
	Session(PortListener& listener);
	~Session();

	boost::asio::ip::tcp::socket* server_socket();
	boost::asio::ip::tcp::socket* client_socket();
	
	void client_start(const std::string& host, unsigned short port);
	void terminate();
private:
	void termination_routine();

	void handle_server_connect(const boost::system::error_code& err);
	void handle_receive_from_client(const boost::system::error_code& err, size_t size);
	void handle_send_to_client(const boost::system::error_code& err);
	void handle_receive_from_server(const boost::system::error_code& err, size_t size);
	void handle_send_to_server(const boost::system::error_code& err);

	void receive_from_client();
	void send_to_client(size_t size);
	void receive_from_server();
	void send_to_server(size_t size);

	PortListener* _listener;

	boost::asio::ip::tcp::socket _server_socket;
	boost::asio::ip::tcp::socket _client_socket;

	unsigned char _server_read_buffer[BUFFER_SIZE];
	unsigned char _client_read_buffer[BUFFER_SIZE];

	bool _is_to_be_terminated;
	bool _client_is_connected;
	bool _client_sent_eof;
	bool _server_is_connected;
	bool _server_sent_eof;
};
