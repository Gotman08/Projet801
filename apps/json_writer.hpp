#pragma once

/**
 * @file json_writer.hpp
 * @brief Minimal dependency-free JSON emitter used by `wfc_dungeon` to
 *        produce a structured description of the synthesised dungeon
 *        for downstream consumption (Unreal Engine plugin, scripts).
 *
 * Pulling a fully-fledged JSON library (nlohmann/json, RapidJSON) for
 * the sole purpose of dumping a few thousand small objects would be
 * overkill. This header offers only what the dungeon writer needs:
 *
 *   - escape and quote a string properly;
 *   - emit numbers (ints, doubles) and booleans;
 *   - bracket / brace nesting via RAII;
 *   - automatic comma insertion between siblings.
 *
 * The output is pretty-printed (one element per line, 2-space indent)
 * because the resulting files are read both by humans (debugging) and
 * by tools.
 */

#include <ostream>
#include <string>
#include <string_view>

namespace wfc::json {

/**
 * @brief Stateful pretty-printer that tracks nesting and comma needs.
 *
 * Emits to an arbitrary `std::ostream` (a file, `std::cout`, etc.).
 * The `Object` and `Array` RAII helpers below open and close braces /
 * brackets and update the comma flag automatically — callers only need
 * to invoke `key()` / `value()` between them.
 */
class Writer {
public:
    explicit Writer(std::ostream& os) : os_(os) {}

    /**
     * @brief Open a JSON object or array; call `close_object`/`close_array`
     *        when done. Use the RAII helpers `Object`/`Array` for safety.
     */
    void open_object() { delimiter_("{"); ++depth_; need_comma_ = false; }
    void close_object() { --depth_; newline_indent_(); os_ << '}'; need_comma_ = true; }
    void open_array()  { delimiter_("["); ++depth_; need_comma_ = false; }
    void close_array() { --depth_; newline_indent_(); os_ << ']'; need_comma_ = true; }

    /// @brief Emit `"key": ` and prepare to receive a value next.
    void key(std::string_view name) {
        delimiter_("");
        write_string_(name);
        os_ << ": ";
        suppress_newline_ = true;
        need_comma_ = false;
    }

    /**
     * @name Value emitters
     *
     * Explicit overloads for `const char*` and `std::string` exist so
     * that string literals don't get sucked into the `bool` overload
     * via the standard "pointer→bool" conversion (which would turn
     * any non-empty literal into the JSON token `true`).
     * @{
     */
    void value(std::string_view s)         { delimiter_(""); write_string_(s); need_comma_ = true; }
    void value(const char* s)              { value(std::string_view(s ? s : "")); }
    void value(const std::string& s)       { value(std::string_view(s)); }
    void value(int v)                      { delimiter_(""); os_ << v;          need_comma_ = true; }
    void value(long v)                     { delimiter_(""); os_ << v;          need_comma_ = true; }
    void value(long long v)                { delimiter_(""); os_ << v;          need_comma_ = true; }
    void value(unsigned long long v)       { delimiter_(""); os_ << v;          need_comma_ = true; }
    void value(double v)                   { delimiter_(""); os_ << v;          need_comma_ = true; }
    void value(bool v)                     { delimiter_(""); os_ << (v ? "true" : "false"); need_comma_ = true; }
    /// @}

    /// @brief Convenience: `key(k); value(v);` in one call.
    template <typename T>
    void key_value(std::string_view k, const T& v) { key(k); value(v); }

private:
    /**
     * @brief Internal: emit either a comma+newline or just an opener.
     *
     * Honours the `suppress_newline_` flag set by `key()` so values
     * appear on the same line as their key.
     */
    void delimiter_(const char* opener) {
        if (need_comma_) os_ << ',';
        if (!suppress_newline_) {
            if (need_comma_ || depth_ > 0) os_ << '\n';
            for (int i = 0; i < depth_; ++i) os_ << "  ";
        }
        os_ << opener;
        suppress_newline_ = false;
        need_comma_ = false;
    }

    void newline_indent_() {
        os_ << '\n';
        for (int i = 0; i < depth_; ++i) os_ << "  ";
    }

    /**
     * @brief Write a JSON string literal: leading quote, escape any
     *        characters that JSON disallows raw (backslash, quote,
     *        control chars), trailing quote.
     */
    void write_string_(std::string_view s) {
        os_ << '"';
        for (char c : s) {
            switch (c) {
                case '"':  os_ << "\\\""; break;
                case '\\': os_ << "\\\\"; break;
                case '\b': os_ << "\\b";  break;
                case '\f': os_ << "\\f";  break;
                case '\n': os_ << "\\n";  break;
                case '\r': os_ << "\\r";  break;
                case '\t': os_ << "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x",
                                      static_cast<unsigned>(c));
                        os_ << buf;
                    } else {
                        os_ << c;
                    }
            }
        }
        os_ << '"';
    }

    std::ostream& os_;
    int  depth_            = 0;
    bool need_comma_       = false;
    bool suppress_newline_ = false;
};

/**
 * @brief RAII wrapper around `open_object`/`close_object`. Use to
 *        avoid forgetting to close a scope.
 */
struct Object {
    explicit Object(Writer& w) : w_(w) { w.open_object(); }
    ~Object() { w_.close_object(); }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
private:
    Writer& w_;
};

/// @brief RAII counterpart for arrays. @see Object
struct Array {
    explicit Array(Writer& w) : w_(w) { w.open_array(); }
    ~Array() { w_.close_array(); }
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
private:
    Writer& w_;
};

}
