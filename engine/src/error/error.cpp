#include "noted/engine/error/error.hpp"

#include <format>

namespace noted {

auto Error::format() const -> std::string {
    std::string out = std::format("[{}] {} @ {}:{}",
        to_string(code), message, where.file_name(), where.line());
    const Error* c = cause.get();
    while (c != nullptr) {
        out += std::format("\n  caused by [{}] {} @ {}:{}",
            to_string(c->code), c->message, c->where.file_name(), c->where.line());
        c = c->cause.get();
    }
    return out;
}

}  // namespace noted
