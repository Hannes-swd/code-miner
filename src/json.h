#pragma once

#include <string>
#include <utility>
#include <vector>

// Ein kleiner JSON-Leser. Reicht genau fuer Dateien wie data/erze.json und
// bringt keine fremde Bibliothek ins Projekt.
//
// Zusaetzlich erlaubt: Kommentare mit // und /* */. Eine Datei, die man von
// Hand pflegt, muss sich erklaeren koennen - und echtes JSON kann das nicht.
struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type        type = Type::Null;
    bool        flag = false;
    double      num  = 0.0;
    std::string str;

    std::vector<JsonValue>                         items;   // Array
    std::vector<std::pair<std::string, JsonValue>> fields;  // Objekt

    const JsonValue* find(const char* key) const;

    // Bequem: Feld lesen, sonst den Ersatzwert nehmen.
    double      number(const char* key, double fallback) const;
    std::string text(const char* key, const char* fallback) const;
};

// false = kaputt, dann steht in error, was und wo.
bool ParseJson(const std::string& source, JsonValue& out, std::string& error);
