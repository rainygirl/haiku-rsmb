#include "Core.h"
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace smb {
bool SafeName(const std::string& s)
{
    return !s.empty() && s != "." && s != ".." && s.find_first_of("/\\") == std::string::npos
        && s.find('\0') == std::string::npos;
}
static std::string Encode(const std::string& s)
{
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    }
    return out.str();
}
static std::string Decode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size() || !std::isxdigit((unsigned char)s[i + 1])
            || !std::isxdigit((unsigned char)s[i + 2]))
            throw std::runtime_error("Invalid percent escape in SMB address.");
        out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
        i += 2;
    }
    if (!SafeName(out))
        throw std::runtime_error("Invalid path component.");
    return out;
}
std::string Location::URL() const
{
    if (host.empty())
        return "smb://";
    std::string result = "smb://" + host;
    if (!share.empty())
        result += "/" + Encode(share);
    std::istringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/'))
        if (!part.empty())
            result += "/" + Encode(part);
    return result;
}
Location Location::Parse(const std::string& input)
{
    std::string s = input;
    if (s.compare(0, 6, "smb://") == 0)
        s.erase(0, 6);
    Location result;
    auto slash = s.find('/');
    result.host = s.substr(0, slash);
    if (result.host.empty() || result.host.find_first_of("@\\?# \t\r\n%") != std::string::npos)
        throw std::runtime_error("Enter a server name or IP address. Use the login fields for credentials.");
    if (slash == std::string::npos)
        return result;
    std::istringstream parts(s.substr(slash + 1));
    std::string part;
    while (std::getline(parts, part, '/')) {
        if (part.empty())
            continue;
        part = Decode(part);
        if (result.share.empty())
            result.share = part;
        else {
            if (!result.path.empty())
                result.path += '/';
            result.path += part;
        }
    }
    return result;
}
Location Location::Parent() const
{
    Location p = *this;
    if (p.path.empty())
        p.share.clear();
    else {
        auto slash = p.path.rfind('/');
        p.path = slash == std::string::npos ? "" : p.path.substr(0, slash);
    }
    return p;
}
Location Location::Child(const std::string& name) const
{
    if (!SafeName(name))
        throw std::runtime_error("Unsafe remote filename.");
    Location p = *this;
    if (p.share.empty())
        p.share = name;
    else {
        if (!p.path.empty())
            p.path += '/';
        p.path += name;
    }
    return p;
}
}
