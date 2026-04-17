#pragma once

#include <variant>
#include <vector>
#include <map>
#include <string>
#include <optional>
#include <memory>

// Forward declarations
struct JsonValue;

// Type aliases
using JsonArray  = std::vector<std::shared_ptr<JsonValue>>;
using JsonObject = std::map<std::string, std::shared_ptr<JsonValue>>;

// JSON value -- holds any JSON value
struct JsonValue {
    std::variant<std::monostate, bool, double, std::string,
                 JsonArray, JsonObject> data;

    // Type checks
    bool is_null()   const;
    bool is_bool()   const;
    bool is_number() const;
    bool is_string() const;
    bool is_array()  const;
    bool is_object() const;

    // Accessors -- throw on wrong type
    bool               as_bool()   const;
    double             as_number() const;
    const std::string& as_string() const;
    const JsonArray&   as_array()  const;
    const JsonObject&  as_object() const;

    // Safe path access -- returns nullopt if missing
    std::optional<JsonValue> get(const std::string& key) const;
    std::optional<JsonValue> get(size_t index)           const;

    // Chained access operator
    std::optional<JsonValue> operator[](const std::string& key) const;
    std::optional<JsonValue> operator[](size_t index)           const;
};

// Public interface
JsonValue   parse_json(std::string_view input);
std::string to_json(const JsonValue& val);
std::string escape_json(const std::string& s);
