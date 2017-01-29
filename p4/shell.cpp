#include <fstream>
#include <iostream>

#include "tokens.hpp"

int main(int argc, char* argv[])
{
    //std::istream& input_stream = std::cin;
    std::ifstream input_file_stream("test.sh");
    std::istream& input_stream = input_file_stream;

    std::string shell_line;
    while (std::getline(input_stream, shell_line)) {
        auto parsed_line = parse_shell_line(shell_line);
        while (parsed_line.size() > 0) {
            Token* token = parsed_line.front();
            parsed_line.pop_front();
            delete token;
        }
    }

    return 0;
}
