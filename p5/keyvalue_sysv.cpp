#include "routines.hpp"

#define SERVER_PORT 6201

int main(int argc, char* argv[])
{
    return master_loop(SERVER_PORT);
}
