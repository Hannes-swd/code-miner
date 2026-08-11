#include "json.h"

#include <cstdlib>

namespace
{

// Der Leser laeuft einmal durch den Text. pos zeigt immer auf das naechste
// Zeichen, das noch niemand angeschaut hat.
struct Reader
{
    const std::string& src;
    std::size_t        pos = 0;
    std::string        error;

    explicit Reader(const std::string& s) : src(s) {}

    bool fail(const char* what)
    {
        if (error.empty())
        {
            int line = 1;
            for (std::size_t i = 0; i < pos && i < src.size(); ++i)
                if (src[i] == '\n')
                    ++line;
            error = "Line " + std::to_string(line) + ": " + what;
        }
        return false;
    }

    void skip()
    {
        while (pos < src.size())
        {
            const char c = src[pos];

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',')
            {
                ++pos;
                continue;
            }

            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/')
            {
                while (pos < src.size() && src[pos] != '\n')
                    ++pos;
                continue;
            }

            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '*')
            {
                pos += 2;
                while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/'))
                    ++pos;
                pos += 2;
                continue;
            }

            return;
        }
    }

    bool value(JsonValue& out)
    {
        skip();
        if (pos >= src.size())
            return fail("something is missing here");

        const char c = src[pos];

        if (c == '{')
            return object(out);
        if (c == '[')
            return array(out);
        if (c == '"')
        {
            out.type = JsonValue::Type::String;
            return string(out.str);
        }

        if (src.compare(pos, 4, "true") == 0)
        {
            out.type = JsonValue::Type::Bool;
            out.flag = true;
            pos += 4;
            return true;
        }
        if (src.compare(pos, 5, "false") == 0)
        {
            out.type = JsonValue::Type::Bool;
            out.flag = false;
            pos += 5;
            return true;
        }
        if (src.compare(pos, 4, "null") == 0)
        {
            out.type = JsonValue::Type::Null;
            pos += 4;
            return true;
        }

        if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9'))
        {
            const std::size_t start = pos;
            while (pos < src.size() && (src[pos] == '-' || src[pos] == '+' || src[pos] == '.' ||
                                        src[pos] == 'e' || src[pos] == 'E' ||
                                        (src[pos] >= '0' && src[pos] <= '9')))
                ++pos;

            out.type = JsonValue::Type::Number;
            out.num  = std::atof(src.substr(start, pos - start).c_str());
            return true;
        }

        return fail("I do not understand this here");
    }

    bool string(std::string& out)
    {
        if (pos >= src.size() || src[pos] != '"')
            return fail("a text in quotation marks belongs here");

        ++pos;
        out.clear();

        while (pos < src.size() && src[pos] != '"')
        {
            if (src[pos] == '\\' && pos + 1 < src.size())
            {
                ++pos;
                switch (src[pos])
                {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                default: out += src[pos]; break;
                }
                ++pos;
                continue;
            }
            out += src[pos];
            ++pos;
        }

        if (pos >= src.size())
            return fail("the text never ends - is a \" missing?");

        ++pos;
        return true;
    }

    bool array(JsonValue& out)
    {
        out.type = JsonValue::Type::Array;
        ++pos;  // [

        for (;;)
        {
            skip();
            if (pos >= src.size())
                return fail("the list never ends - is a ] missing?");
            if (src[pos] == ']')
            {
                ++pos;
                return true;
            }

            JsonValue item;
            if (!value(item))
                return false;
            out.items.push_back(item);
        }
    }

    bool object(JsonValue& out)
    {
        out.type = JsonValue::Type::Object;
        ++pos;  // {

        for (;;)
        {
            skip();
            if (pos >= src.size())
                return fail("the brace is never closed - is a } missing?");
            if (src[pos] == '}')
            {
                ++pos;
                return true;
            }

            std::string name;
            if (!string(name))
                return false;

            skip();
            if (pos >= src.size() || src[pos] != ':')
                return fail("nach dem Namen fehlt der Doppelpunkt");
            ++pos;

            JsonValue item;
            if (!value(item))
                return false;

            out.fields.push_back(std::make_pair(name, item));
        }
    }
};

}  // namespace

const JsonValue* JsonValue::find(const char* key) const
{
    for (const auto& f : fields)
        if (f.first == key)
            return &f.second;
    return nullptr;
}

double JsonValue::number(const char* key, double fallback) const
{
    const JsonValue* v = find(key);
    return (v != nullptr && v->type == Type::Number) ? v->num : fallback;
}

std::string JsonValue::text(const char* key, const char* fallback) const
{
    const JsonValue* v = find(key);
    return (v != nullptr && v->type == Type::String) ? v->str : std::string(fallback);
}

bool ParseJson(const std::string& source, JsonValue& out, std::string& error)
{
    Reader r(source);
    if (!r.value(out))
    {
        error = r.error;
        return false;
    }
    return true;
}
