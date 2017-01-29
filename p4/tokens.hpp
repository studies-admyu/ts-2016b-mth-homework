#pragma once

#include <exception>
#include <list>
#include <string>

class Token
{
public:
    virtual ~Token();

protected:
    Token();
};

class Command: public Token
{
public:
    class BadCommand: public std::exception
    {
    public:
        BadCommand(size_t pos);
        ~BadCommand();

        size_t pos() const;
    private:
        size_t _pos;
    };

public:
    Command(std::string name, std::list<std::string> args, std::string input_file, std::string output_file);
    ~Command();

    static Command* from_std_string(std::string command_string);

    std::string name() const;
    std::list<std::string> args() const;
    std::string input_filename() const;
    std::string output_filename() const;

private:
    std::string _name;
    std::list<std::string> _args;

    std::string _input_file;
    std::string _output_file;
};

class Operator: public Token
{
public:
    enum OperatorType {
        OPERATOR_AND = 0,
        OPERATOR_OR,
        OPERATOR_CONVEYOR,
        OPERATOR_BACKGROUND,
        OPERATOR_SEPARATOR
    };

public:
    Operator(OperatorType type);
    ~Operator();

    OperatorType get_type() const;

private:
    OperatorType _type;
};

class BadShellLine: public std::exception
{
public:
    BadShellLine(size_t pos);
    ~BadShellLine();

    size_t pos() const;

private:
    size_t _pos;
};

std::list<Token*> parse_shell_line(std::string line);
