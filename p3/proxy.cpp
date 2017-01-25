#include <fstream>
#include <sstream>
#include <iostream>
#include <list>
#include <string>

#include "server_loop.hpp"
#include "port_listener.hpp"

struct ConfigEntry {
	unsigned short source_port;
	std::list<Destination> destinations;
};

void parse_config_file(const char* filename, std::list<ConfigEntry>& config_entries)
{
	config_entries.clear();

	std::ifstream config_stream(filename);

	std::string config_line;
	ConfigEntry entry;

	while (std::getline(config_stream, config_line)) {
		size_t  last_index = 0;

		while ((last_index = config_line.find_first_of(':', last_index)) != std::string::npos) {
			config_line[last_index] = ' ';
		}

		std::istringstream line_stream(config_line);

		line_stream >> entry.source_port;

		Destination dest;

		while (line_stream >> dest.host) {
			line_stream >> dest.port;
			entry.destinations.push_back(dest);
		}

		config_entries.push_back(entry);
		entry.destinations.clear();
	}

	config_stream.close();
}

std::list<PortListener*> listeners;

void disconnect_all() {
	for ( auto listener : listeners ) {
		listener->stop_listening();
		listener->terminate_sessions();
	}
}

int main(int argc, char* argv[])
{
	if (argc < 2) {
		std::cerr << "ERROR: Need a config file name as a parameter" << std::endl;
		return 1;
	}

	std::list<ConfigEntry> config_entries;

	parse_config_file(argv[1], config_entries);

	boost::asio::io_service io_service;

	if (!init_serv_int(&disconnect_all)) {
		std::cerr << "Unable to allocate resources for the server loop" << std::endl;
		return 2;
	}

	for ( auto config_entry : config_entries ) {
		listeners.push_back(new PortListener(io_service, config_entry.source_port, &(config_entry.destinations)));
	}

	io_service.run();

	io_service.stop();

	while (listeners.size() > 0) {
		PortListener* listener = listeners.front();
		listeners.pop_front();
		delete listener;
	}

	cleanup_serv_int();

	return 0;
}