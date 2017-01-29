#include "tokens.hpp"

Token::Token()
{

}

Token::~Token()
{

}

Command::BadCommand::BadCommand(size_t pos):
    _pos(pos)
{

}

Command::BadCommand::~BadCommand()
{

}

size_t Command::BadCommand::pos() const
{
    return this->_pos;
}

Command::Command(std::string name, std::list<std::string> args, std::string input_file, std::string output_file):
    _name(name), _args(args), _input_file(input_file), _output_file(output_file)
{

}

Command::~Command()
{

}

Command *Command::from_std_string(std::string command_string)
{
    std::list<std::string> words;
    std::string word;

    bool quoted = false;

    for (size_t i = 0; i < command_string.size(); ++i) {
        if ((command_string[i] == '\'') || (command_string[i] == '"')) {
            quoted = !quoted;
        } else if ((command_string[i] == '<') && !quoted) {
            if (word.size() > 0) {
                words.push_back(word);
                word.clear();
            }
            words.push_back("<");
        } else if ((command_string[i] == '>') && !quoted) {
            if (word.size() > 0) {
                words.push_back(word);
                word.clear();
            }
            words.push_back(">");
        } else if (((command_string[i] != ' ') && (command_string[i] != '\t') && (command_string[i] != '\n')) || quoted) {
            word.push_back(command_string[i]);
        } else {
            if (word.size() > 0) {
                words.push_back(word);
                word.clear();
            }
        }
    }
    if (word.size() > 0) {
        words.push_back(word);
        word.clear();
    }

    std::string name;
    std::list<std::string> args;
    std::string input_file;
    std::string output_file;

    auto out_operand = words.end();
    auto in_operand = words.end();

    for (auto i_word = words.begin(); i_word != words.end(); ++i_word) {
        if ((*i_word) != std::string("<") && (*i_word) != std::string(">")) {
            if (i_word == words.begin()) {
               name = *i_word;
            } else if (in_operand != words.end()) {
                if (std::next(in_operand) != i_word) {
                    throw BadCommand(0);
                }
                input_file = *i_word;
                in_operand = words.end();
            } else if (out_operand != words.end()) {
                if (std::next(out_operand) != i_word) {
                    throw BadCommand(0);
                }
                output_file = *i_word;
                out_operand = words.end();
            } else {
                args.push_back(*i_word);
            }
        } else {
            if ((*i_word) == std::string("<")) {
                if (in_operand != words.end()) {
                    throw BadCommand(0);
                }
                in_operand = i_word;
            } else {
                if (out_operand != words.end()) {
                    throw BadCommand(0);
                }
                out_operand = i_word;
            }
        }
    }

    return new Command(name, args, input_file, output_file);
}

std::string Command::name() const
{
    return this->_name;
}

std::list<std::string> Command::args() const
{
    return this->_args;
}

std::string Command::input_filename() const
{
    return this->_input_file;
}

std::string Command::output_filename() const
{
    return this->_output_file;
}

Operator::Operator(OperatorType type):
    _type(type)
{

}

Operator::~Operator()
{

}

Operator::OperatorType Operator::get_type() const
{
    return this->_type;
}

size_t min_size_t(size_t a, size_t b)
{
    if (a == std::string::npos) {
        return b;
    } else if (b == std::string::npos) {
        return a;
    } else {
        return (a < b) ? a: b;
    }
}

BadShellLine::BadShellLine(size_t pos):
    _pos(pos)
{

}

BadShellLine::~BadShellLine()
{

}

size_t BadShellLine::pos() const {
    return this->_pos;
}

std::list<Token*> shell_line_lexical_parser(std::string line)
{
    std::list<Token*> result_tokens;

    bool quoted = false;
    Operator::OperatorType last_operator_type;

    size_t first_not_proceeded = 0;
    size_t last_background_and_operator_pos = line.find_first_of('&');
    size_t last_conveyor_or_operator_pos = line.find_first_of('|');
    size_t last_separator_operator_pos = line.find_first_of(';');
    size_t last_quotes_pos = line.find_first_of("\'\"");

    size_t last_operator = min_size_t(last_background_and_operator_pos, last_conveyor_or_operator_pos);
    last_operator = min_size_t(last_operator, last_separator_operator_pos);
    last_operator = min_size_t(last_operator, last_quotes_pos);
    while (last_operator != std::string::npos) {
        if (quoted) {
            if ((line[last_operator] == '\'') || (line[last_operator] == '"')) {
                quoted = false;
            }
        } else {
            if ((line[last_operator] == '\'') || (line[last_operator] == '"')) {
                quoted = true;
            } else if (line[last_operator] == '&') {
                if (last_operator + 1 == line.size()) {
                    last_operator_type = Operator::OPERATOR_BACKGROUND;
                } else if (line.find_last_not_of("&|;", last_operator + 1) == last_operator + 1) {
                    last_operator_type = Operator::OPERATOR_BACKGROUND;
                } else if (line[last_operator + 1] == '&') {
                    last_operator_type = Operator::OPERATOR_AND;
                } else {
                    throw BadShellLine(last_operator);
                }
            } else if (line[last_operator] == '|') {
                if (last_operator + 1 == line.size()) {
                    last_operator_type = Operator::OPERATOR_CONVEYOR;
                } else if (line.find_last_not_of("&|;", last_operator + 1) == last_operator + 1) {
                    last_operator_type = Operator::OPERATOR_CONVEYOR;
                } else if (line[last_operator + 1] == '|') {
                    last_operator_type = Operator::OPERATOR_OR;
                } else {
                    throw BadShellLine(last_operator);
                }
            } else if (line[last_operator] == ';') {
                if (last_operator + 1 == line.size()) {
                    last_operator_type = Operator::OPERATOR_SEPARATOR;
                } else if (line.find_last_not_of("&|;", last_operator + 1) == last_operator + 1) {
                    last_operator_type = Operator::OPERATOR_SEPARATOR;
                } else {
                    throw BadShellLine(last_operator);
                }
            }

            if (!quoted) {
                result_tokens.push_back(Command::from_std_string(
                    line.substr(first_not_proceeded, last_operator - first_not_proceeded)
                ));
                result_tokens.push_back(new Operator(last_operator_type));

                switch (last_operator_type) {
                    case Operator::OPERATOR_AND:
                    case Operator::OPERATOR_OR:
                        first_not_proceeded = last_operator + 2;
                        break;

                    case Operator::OPERATOR_CONVEYOR:
                    case Operator::OPERATOR_BACKGROUND:
                    case Operator::OPERATOR_SEPARATOR:
                        first_not_proceeded = last_operator + 1;
                        break;

                    default:
                        throw BadShellLine(last_operator);
                        break;
                }
            }
        }

        if ((line[last_operator] == '\'') || (line[last_operator] == '"') || quoted) {
            last_background_and_operator_pos = line.find_first_of('&', last_operator + 1);
            last_conveyor_or_operator_pos = line.find_first_of('|', last_operator + 1);
            last_separator_operator_pos = line.find_first_of(';', last_operator + 1);
            last_quotes_pos = line.find_first_of("'\"", last_operator + 1);
        } else {
            last_background_and_operator_pos = line.find_first_of('&', first_not_proceeded);
            last_conveyor_or_operator_pos = line.find_first_of('|', first_not_proceeded);
            last_separator_operator_pos = line.find_first_of(';', first_not_proceeded);
            last_quotes_pos = line.find_first_of("\'\"", first_not_proceeded);
        }

        last_operator = min_size_t(last_background_and_operator_pos, last_conveyor_or_operator_pos);
        last_operator = min_size_t(last_operator, last_separator_operator_pos);
        last_operator = min_size_t(last_operator, last_quotes_pos);
    }

    if (quoted) {
        throw BadShellLine(line.size());
    }

    if (first_not_proceeded < line.size()) {
        result_tokens.push_back(Command::from_std_string(
            line.substr(first_not_proceeded, line.size() - first_not_proceeded)
        ));
    }

    return result_tokens;
}

void shell_line_remove_empty(std::list<Token*>& tokens)
{
    auto token = tokens.begin();
    while (token != tokens.end()) {
        Command* command = dynamic_cast<Command*>(*token);
        if (command) {
            if (command->name().empty()) {
                token = tokens.erase(token, std::next(token));
                delete command;
                continue;
            }
        }
        ++token;
    }
}

bool shell_line_syntax_valid(const std::list<Token*>& tokens)
{
    bool last_was_operator = true;

    for (auto token = tokens.cbegin(); token != tokens.cend(); ++token) {
        auto operator_ptr = dynamic_cast<const Operator*>(*token);
        if (operator_ptr) {
            if (operator_ptr->get_type() != Operator::OPERATOR_BACKGROUND) {
                if (std::next(token) == tokens.cend()) {
                    return false;
                }
                if ( last_was_operator || (dynamic_cast<const Operator*>(*std::next(token))) ) {
                    return false;
                }
            } else {
                if ( last_was_operator || (std::next(token) != tokens.cend()) ) {
                    return false;
                }
            }
        } else {
            if (!last_was_operator) {
                return false;
            }
        }
        last_was_operator = (operator_ptr);
    }
    return true;
}

std::list<Token *> parse_shell_line(std::string line)
{
    auto tokens = shell_line_lexical_parser(line);
    shell_line_remove_empty(tokens);
    if (!shell_line_syntax_valid(tokens)) {
        throw BadShellLine(0);
    }
    return tokens;
}
