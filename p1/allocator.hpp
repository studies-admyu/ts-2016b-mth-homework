#include <stdexcept>
#include <string>

enum class AllocErrorType {
    InvalidFree,
    NoMemory,
};

class AllocError: std::runtime_error {
private:
    AllocErrorType type;

public:
    AllocError(AllocErrorType _type, std::string message):
            runtime_error(message),
            type(_type)
    {}

    AllocErrorType getType() const { return type; }
};

class Allocator;

class Pointer {

friend class Allocator;

public:
    Pointer();
    Pointer(const Pointer& p);
    ~Pointer();
    Pointer& operator=(const Pointer& p);

    void *get() const;

private:
    struct PointerImpl;
    PointerImpl* impl;
};

class Allocator {
public:
    Allocator(void *base, size_t size);
    ~Allocator();
    
    Pointer alloc(size_t N);
    void realloc(Pointer &p, size_t N);
    void free(Pointer &p);

    void defrag();
    std::string dump();

private:
    struct AllocatorImpl;
    AllocatorImpl* impl;
};

