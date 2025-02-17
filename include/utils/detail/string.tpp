
#pragma once

#ifndef STRING_TPP
#define STRING_TPP

#include "utils/result.hpp"
#include "utils/assert.hpp"
#include "utils/tuple.hpp"

#include <charconv> // std::to_chars
#include <cmath> // std::log10

namespace utils {

    namespace detail {

        struct Identifier {
            Identifier();
            Identifier(std::size_t position);
            Identifier(std::string_view name);
            ~Identifier();
            
            bool operator==(const Identifier& other) const;
            
            enum class Type {
                Auto = 0,
                Position,
                Name,
            } type;
            
            // This layout has the same size as std::variant<std::size_t, std::string>
            // Prefer this way so that accessing the underlying identifier is easier
            std::size_t position;
            std::string_view name;
        };
        
        char nibble_to_hexadecimal(const char nibble[4]);
        
        // Returns the number of characters read
        std::size_t parse_identifier(std::string_view in, Identifier& out);
        
        // Returns the index of the first invalid character
        std::size_t parse_format_spec(std::string_view in, FormatSpec& out, bool nested = false);

        template <typename T>
        bool is_reserved_argument(const NamedArgument<T>& arg) {
            if (arg.name.length() < 2) {
                return false;
            }
            
            // Reserved arguments start with '__'
            return arg.name[0] == '_' && arg.name[1] == '_';
        }
        
        // Performs various validation checks on function arguments
        template <typename Tuple>
        void validate_arguments(const Tuple& tuple, bool is_auto_numbered) {
            if (is_auto_numbered) {
                // Check: argument list must not contain any NamedArgument types (aside from builtin ones)
                utils::apply([]<typename T, std::size_t I>(const T& value) {
                    if constexpr (is_named_argument<T>::value) {
                        if (!is_reserved_argument(value)) {
                            throw std::runtime_error(utils::format("invalid argument at position {} - named arguments are not allowed in format strings that only contain auto-numbered placeholders", I));
                        }
                    }
                }, tuple);
            }
            else {
                // Format string contains a mix of positional and named placeholders
                std::size_t num_positional_arguments = 0u;
                constexpr std::size_t num_arguments = std::tuple_size<Tuple>::value;
        
                // Check: arguments for positional placeholders must come before any arguments for named placeholders
                utils::apply([&num_positional_arguments, positional_arguments_parsed = false]<typename T>(const T&, std::size_t index) mutable {
                    if constexpr (is_named_argument<T>::value) {
                        if (!positional_arguments_parsed) {
                            positional_arguments_parsed = true;
                        }
                    }
                    else {
                        if (positional_arguments_parsed) {
                            // Encountered positional argument after named argument cutoff
                            throw std::runtime_error(utils::format("invalid argument at position {} - arguments for positional placeholders must come before arguments for named placeholders", index));
                        }
        
                        ++num_positional_arguments;
                    }
                }, tuple);
                
                // Check: two NamedArgument<T> arguments should not reference the same named placeholder
                utils::apply_for([&tuple, num_arguments]<typename T>(const T& outer, std::size_t i) {
                    ASSERT(is_named_argument<T>::value, "argument is not of type NamedArgument<T>");
        
                    if constexpr (is_named_argument<T>::value) {
                        utils::apply_for([&outer, i]<typename U>(const U& inner, std::size_t j) {
                            ASSERT(is_named_argument<U>::value, "argument is not of type NamedArgument<U>");
        
                            if constexpr (is_named_argument<U>::value) {
                                if (outer.name == inner.name) {
                                    throw std::runtime_error(utils::format("invalid argument at position {} - named arguments must be unique (argument for placeholder '{}' first encountered at argument position {})", j, inner.name, i));
                                }
                            }
                        }, tuple, i + 1u, num_arguments);
                    }
                }, tuple, num_positional_arguments, num_arguments);
            }
        }
        
        std::string format_no_args(std::string_view fmt, std::source_location source);

    }
    
    // namespace utils
    
    template <String T, String U>
    [[nodiscard]] bool icasecmp(const T& first, const U& second) {
        std::string_view a = first;
        std::string_view b = second;
        
        if (a.length() != b.length()) {
            return false;
        }
        
        for (std::size_t i = 0u; i < a.length(); ++i) {
            if (std::tolower(a[i]) != std::tolower(b[i])) {
                return false;
            }
        }
        
        return true;
    }

    template <String T, String U>
    [[nodiscard]] bool operator==(const T& first, const U& second) {
        std::string_view a = first;
        std::string_view b = second;
        
        if (a.length() != b.length()) {
            return false;
        }
        
        for (std::size_t i = 0u; i < a.length(); ++i) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        
        return true;
    }
    
    template <typename ...Ts>
    FormatSpec::SpecifierView FormatSpec::get_specifier(std::string_view first, std::string_view second, Ts... rest) const {
        constexpr std::size_t argument_count = sizeof...(Ts) + 2u; // Include 'first' and 'second' specifiers
        SpecifierView specifiers[argument_count];

        // Index of the first valid specifier
        std::size_t index = argument_count; // Initially set to invalid
        std::size_t valid_specifier_count = 0u;

        utils::apply([this, &specifiers, &index, &valid_specifier_count](std::string_view name, std::size_t i) {
            specifiers[i].name = name;
            if (has_specifier(name)) {
                index = i;
                specifiers[i].value = get_specifier(name);
                ++valid_specifier_count;
            }
        }, std::make_tuple(first, second, std::string_view(rest)...));

        if (valid_specifier_count == 0u) {
            // No valid specifiers found
            // Error message format: bad format specification access - no values found for any of the following specifiers: ... (length: 87 + comma-separated list of specifiers)
            std::size_t capacity = 87u;
            for (std::size_t i = 0u; i < argument_count; ++i) {
                capacity += specifiers[i].value.length();
                if (i != argument_count) {
                    // Avoid trailing comma ', '
                    capacity += 2u;
                }
            }
            
            std::string error;
            error.reserve(capacity);
            
            error += "bad format specification access - no values found for any of the following specifiers: ";

            for (std::size_t i = 0u; i < argument_count; ++i) {
                error += specifiers[i].name;

                // Do not add a trailing comma
                if (i != argument_count) {
                    error += ", ";
                }
            }

            throw std::runtime_error(error);
        }
        else if (valid_specifier_count > 1u) {
            // Found multiple valid specifiers
            // Error message format: ambiguous format specification access - value found for more than one of the following specifiers: ... (length: 99 + comma-separated list of found specifiers)
            std::size_t capacity = 99u;
            for (std::size_t i = 0u, count = 0u; i < argument_count; ++i) {
                if (!specifiers[i].value.empty()) {
                    capacity += specifiers[i].name.length();
                    if (count != valid_specifier_count) {
                        // Do not add a trailing comma
                        capacity += 2u;
                    }
                    ++count;
                }
            }

            std::string error;
            error.reserve(capacity);
            
            error += "ambiguous format specification access - value found for more than one of the following specifiers: ";

            for (std::size_t i = 0u, count = 0u; i < argument_count; ++i) {
                if (!specifiers[i].value.empty()) {
                    error += specifiers[i].name;
                    if (count != valid_specifier_count) {
                        // Do not add a trailing comma
                        error += ", ";
                    }
                    ++count;
                }
            }
            
            throw std::runtime_error(error);
        }
        
        return specifiers[index];
    }
    
    template <typename ...Ts>
    bool FormatSpec::has_specifier(std::string_view first, std::string_view second, Ts... rest) const {
        return has_specifier(first) || has_specifier(second) || (has_specifier(rest) || ...);
    }
    
    template <typename T>
    NamedArgument<T>::NamedArgument(std::string_view name, const T& value) : name(name),
                                                                             value(value) {
        static_assert(!is_named_argument<T>::value, "NamedArgument type must not be NamedArgument");
    }

    template <typename T>
    NamedArgument<T>::~NamedArgument() = default;
    
    template <typename ...Ts>
    std::string format(const FormatString& str, const Ts&... args) {
        std::string_view fmt = str.format;
        std::source_location source = str.source;
        
        if (fmt.empty()) {
            return "";
        }
        
        std::size_t length = fmt.length();
        std::size_t last_read_position = 0u;
        std::size_t i = 0u;
        
        std::string out;
        
        if constexpr (sizeof...(Ts)) {
            std::tuple<typename std::decay<const Ts>::type...> tuple = std::make_tuple(args...);
            
            // 'source' is overridden by the implementation to reference external function calls
            utils::apply([&source]<typename T>(const T& arg) {
                if constexpr (std::is_same<T, NamedArgument<std::source_location>>::value) {
                    if (icasecmp(arg.name, "__source")) {
                        source = arg.value;
                    }
                }
            }, tuple);
            
            // Reserved arguments are provided automatically by the implementation and should not be considered part of the user-specified args
            std::size_t num_arguments = sizeof...(Ts);
            utils::apply([&num_arguments]<typename T>(const T& arg) {
                if constexpr (is_named_argument<T>::value) {
                    if (detail::is_reserved_argument(arg)) {
                        --num_arguments;
                    }
                }
            }, tuple);
            
            if (num_arguments == 0) {
                // No user arguments provided to format
                return std::move(detail::format_no_args(fmt, source));
            }
            
            std::optional<detail::Identifier::Type> type { };
            std::size_t argument_index = 0u; // Used only for auto-numbered format strings
            
            while (i < length) {
                if (fmt[i] == '{') {
                    if (i + 1 == length) {
                        throw std::runtime_error(utils::format("unterminated placeholder opening brace at position {} - opening brace literals must be escaped as '}}' ({})", i, source));
                    }
                    else if (fmt[i + 1] == '{') {
                        // Escaped opening brace '{{'
                        out.append(fmt, last_read_position, i - last_read_position + 1u); // Include the first opening brace
                        
                        ++i;
                        last_read_position = i + 1u; // Skip to the next character after the second brace
                    }
                    else {
                        out.append(fmt, last_read_position, i - last_read_position); // Do not include opening brace
                        
                        // Skip placeholder opening brace '{'
                        i++;
                        
                        detail::Identifier identifier { };
                        i += parse_identifier(fmt.substr(i), identifier);
                        if (fmt[i] != ':' && fmt[i] != '}') {
                            // Expecting format spec separator ':' or placeholder closing brace '}'
                            throw std::runtime_error(utils::format("invalid character '{}' at position {} ({})", fmt[i], i, source));
                        }
                        
                        if (!type) {
                            // The identifier of the first placeholder dictates the type of format string
                            type = identifier.type;
                            detail::validate_arguments(tuple, identifier.type == detail::Identifier::Type::Auto);
                        }
                        else {
                            // Verify format string homogeneity - all placeholders must be of the same type
                            bool homogeneous = (*type == detail::Identifier::Type::Auto && identifier.type == detail::Identifier::Type::Auto) || (*type != detail::Identifier::Type::Auto && identifier.type != detail::Identifier::Type::Auto);
                            if (!homogeneous) {
                                throw std::runtime_error(utils::format("invalid format string - placeholder types must be homogeneous ({})", source));
                            }
                        }
                        
                        FormatSpec spec { };
                        if (fmt[i] == ':') {
                            // Skip format spec separator ':'
                            ++i;
                            
                            i += detail::parse_format_spec(fmt.substr(i, length - i), spec);
                            if (fmt[i] != '}') {
                                throw std::runtime_error(utils::format("invalid character '{}' at position {} ({})", fmt[i], i, source));
                            }
                        }
    
                        // Format placeholder
                        switch (*type) {
                            case detail::Identifier::Type::Auto: {
                                // utils::apply is a noop is the argument index exceeds the length of the tuple
                                // An exception for this is raised below (after parsing the whole format string)
                                utils::apply([&spec, &out]<typename T>(const T& value) {
                                    // builtin arguments are specified via NamedArgument types, and should not be considered as a part of the user-specified argument list
                                    if constexpr (!is_named_argument<T>::value) {
                                        Formatter<T> formatter { };
                                        formatter.parse(spec);
                                        out.append(formatter.format(value));
                                    }
                                }, tuple, argument_index++);
                                break;
                            }
                            case detail::Identifier::Type::Position: {
                                if (identifier.position >= num_arguments) {
                                    throw std::runtime_error(utils::format("invalid format string - missing argument for placeholder {} at position {} ()", identifier.position, i, source));
                                }
                                else {
                                    utils::apply([&spec, &out]<typename T>(const T& value) {
                                        Formatter<T> formatter { };
                                        formatter.parse(spec);
                                        out.append(formatter.format(value));
                                    }, tuple, identifier.position);
                                }
                                break;
                            }
                            case detail::Identifier::Type::Name: {
                                bool formatted = false;
                                utils::apply([name = identifier.name, &formatted, &spec, &out]<typename T>(const T& arg) {
                                    if constexpr (is_named_argument<T>::value) {
                                        if (arg.name == name) {
                                            Formatter<typename T::type> formatter { };
                                            formatter.parse(spec);
                                            out.append(formatter.format(arg.value));
                                            formatted = true;
                                        }
                                    }
                                }, tuple);
                                
                                if (!formatted) {
                                    throw std::runtime_error(utils::format("invalid format string - missing NamedArgument for placeholder '{}' at position {} ({})", identifier.name, i, source));
                                }
                                break;
                            }
                        }
                        
                        // Skip placeholder closing brace '}'
                        last_read_position = ++i;
                    }
                }
                else if (fmt[i] == '}') {
                    if (i + 1 < length && fmt[i + 1] == '}') {
                        // Escaped closing brace '}}'
                        out.append(fmt, last_read_position, i - last_read_position + 1u); // Include the first opening brace
                        
                        ++i;
                        last_read_position = i + 1u; // Skip to the next character after the second brace
                    }
                    else {
                        throw std::runtime_error(utils::format("invalid placeholder closing brace at position {} - closing brace literals must be escaped as '}}}}' ({})", i, source));
                    }
                }
                
                ++i;
            }
            
            if (*type == detail::Identifier::Type::Auto && argument_index > num_arguments) {
                throw std::runtime_error(utils::format("not enough arguments provided to format(...) - expecting: {}, received: {} ({})", argument_index, num_arguments, source));
            }
            
            if (i != last_read_position) {
                // Append any remaining characters
                out.append(fmt, last_read_position, i - last_read_position);
            }
            
            return std::move(out);
        }
        else {
            // No arguments provided to format
            return std::move(detail::format_no_args(fmt, source));
        }
    }
    
    template <typename T>
    IntegerFormatter<T>::IntegerFormatter() : FormatterBase(),
                                              representation(Representation::Decimal),
                                              sign(Sign::NegativeOnly),
                                              use_separator_character(),
                                              group_size(),
                                              use_base_prefix(false),
                                              digits() {
        static_assert(is_integer_type<T>::value, "value must be an integer type");
    }

    template <typename T>
    IntegerFormatter<T>::~IntegerFormatter() = default;

    template <typename T>
    void IntegerFormatter<T>::parse(const FormatSpec& spec) {
        ASSERT(spec.type() == FormatSpec::Type::SpecifierList, "format spec for integer values must be a specifier list");
        
        FormatterBase::parse(spec);

        if (spec.has_specifier("representation")) {
            std::string_view value = trim(spec.get_specifier("representation"));
            if (icasecmp(value, "decimal")) {
                representation = Representation::Decimal;
            }
            else if (icasecmp(value, "binary")) {
                representation = Representation::Binary;
            }
            else if (icasecmp(value, "hexadecimal")) {
                representation = Representation::Hexadecimal;
            }
        }

        if (spec.has_specifier("sign")) {
            std::string_view value = trim(spec.get_specifier("sign"));
            if (icasecmp(value, "negative only") || icasecmp(value, "negative_only") || icasecmp(value, "negativeonly")) {
                sign = Sign::NegativeOnly;
            }
            else if (icasecmp(value, "aligned")) {
                sign = Sign::Aligned;
            }
            else if (icasecmp(value, "both")) {
                sign = Sign::Both;
            }
        }
        
        if (spec.has_specifier("use_separator", "useseparator", "use_separator_character", "useseparatorcharacter")) {
            std::string_view value = trim(spec.get_specifier("use_separator", "useseparator", "use_separator_character", "useseparatorcharacter").value);
            
            if (icasecmp(value, "true") || icasecmp(value, "1")) {
                use_separator_character = true;
            }
            else if (icasecmp(value, "false") || icasecmp(value, "0")) {
                use_separator_character = false;
            }
        }

        if (spec.has_specifier("group_size", "groupsize")) {
            std::string_view value = trim(spec.get_specifier("group_size", "groupsize").value);

            unsigned _group_size;
            std::size_t num_characters_read = from_string(value, _group_size);

            if (num_characters_read > 0) {
                group_size = _group_size;
            }
        }

        if (spec.has_specifier("use_base_prefix", "usebaseprefix")) {
            std::string_view value = trim(spec.get_specifier("use_base_prefix", "usebaseprefix").value);
            
            if (icasecmp(value, "true") || icasecmp(value, "1")) {
                use_base_prefix = true;
            }
            else if (icasecmp(value, "false") || icasecmp(value, "0")) {
                use_base_prefix = false;
            }
        }

        if (spec.has_specifier("digits")) {
            std::string_view value = trim(spec.get_specifier("digits"));

            unsigned _digits;
            std::size_t num_characters_read = from_string(value, _digits);

            if (num_characters_read > 0) {
                digits = _digits;
            }
        }
    }

    template <typename T>
    std::string IntegerFormatter<T>::format(T value) const {
        if (representation == Representation::Decimal) {
            return to_decimal(value);
        }
        else if (representation == Representation::Binary) {
            return to_binary(value);
        }
        else { // if (representation == Representation::Hexadecimal) {
            return to_hexadecimal(value);
        }
    }

    template <typename T>
    std::string IntegerFormatter<T>::to_decimal(T value) const {
        // Compute the minimum number of characters to hold the formatted value
        std::size_t num_characters;

        // +1 character for sign
        
        if constexpr (std::is_signed<T>::value) {
            if (value < 0) {
                num_characters = (std::size_t) (std::log10(-value) + 1u) + 1u;
            }
            else {
                if (value == 0) {
                    // std::log10(0) = 0
                    num_characters = 1;
                }
                else {
                    num_characters = (std::size_t) (std::log10(value) + 1u) + (sign != Sign::NegativeOnly);
                }
            }
        }
        else {
            if (value == 0) {
                // std::log10(0) = 0
                num_characters = 1;
            }
            else {
                num_characters = (std::size_t) (std::log10(value) + 1u) + (sign != Sign::NegativeOnly);
            }
        }

        bool _use_separator_character = use_separator_character && *use_separator_character;
        std::uint8_t _group_size = 3u;

        // Reserve capacity for separator characters (inserted between two groups)
        std::size_t num_separator_characters = 0u;
        if (_use_separator_character) {
            num_separator_characters = num_characters / _group_size;

            // Do not include a leading separator character if the number of characters is an even multiple of the group size
            if (num_characters && num_characters % _group_size == 0) {
                num_separator_characters -= 1u;
            }
        }

        std::size_t capacity = std::max(num_characters, (std::size_t) width);
        std::string result(capacity, fill_character);

        std::size_t write_position;
        switch (justification) {
            case Justification::Left:
                write_position = 0u;
                break;
            case Justification::Right:
                write_position = capacity - num_characters;
                break;
            case Justification::Center:
                write_position = (capacity - num_characters) / 2;
                break;
        }

        char buffer[std::numeric_limits<T>::digits10 + (std::is_signed<T>::value ? 1 : 0)];
        char* start = buffer;
        char* end = buffer + sizeof(buffer) / sizeof(buffer[0]);
        const auto& [ptr, error_code] = std::to_chars(start, end, value, 10);

        if (value < 0) {
            result[write_position++] = '-';
            ++start; // Do not read negative sign from buffer
        }
        else {
            switch (sign) {
                case Sign::Aligned:
                    result[write_position++] = ' ';
                    break;
                case Sign::Both:
                    result[write_position++] = '+';
                    break;
                case Sign::NegativeOnly:
                default:
                    break;
            }
        }

        std::size_t num_characters_written = ptr - start;

        if (_use_separator_character) {
            std::size_t current = 3 - (num_characters_written % 3);
            for (std::size_t i = 0u; i < num_characters_written; ++i, ++current) {
                if (i && (current % 3) == 0u) {
                    result[write_position++] = ',';
                }

                result[write_position++] = *(start + i);
            }
        }
        else {
            // Copy contents directly
            result.replace(write_position, num_characters_written, start, 0, num_characters_written);
        }
        
        return std::move(FormatterBase::format(result));
    }

    template <typename T>
    std::string IntegerFormatter<T>::to_binary(T value) const {
        std::size_t num_characters;

        // Compute the minimum number of characters to hold the formatted value
        if (value < 0) {
            // Twos complement is used for formatting negative values, which by default uses as many digits as required by the system architecture
            num_characters = sizeof(T) * CHAR_BIT;
        }
        else {
            // The minimum number of digits required to format a binary number is log2(n) + 1
            num_characters = (std::size_t) std::floor(std::log2(value)) + 1u;
        }

        std::size_t num_padding_characters = 0u;

        // The number of characters can be overridden by a user-specified 'digits' value
        // If the desired number of digits is smaller than the required number of digits, remove digits starting from the front (most significant) bits
        // If the desired number of digits is larger than the required number of digits, append digits to the front (1 for negative integers, 0 for positive integers)
        if (digits) {
            std::uint8_t _digits = *digits;
            if (num_characters >= _digits) {
                num_characters = _digits;
            }
            else {
                // Append leading padding characters to reach the desired number of digits
                num_padding_characters = _digits - num_characters;
            }
        }

        bool _use_separator_character = false;
        std::uint8_t _group_size = 0u;

        if (use_separator_character) {
            if (*use_separator_character) {
                if (group_size) {
                    _group_size = *group_size;

                    if (*group_size) {
                        _use_separator_character = true;
                    }
                    else {
                        // Group size explicitly provided as 0, use of separator character is disabled
                        _use_separator_character = false;
                    }
                }
                else {
                    // Group size is 4 by default (if not specified)
                    _group_size = 4u;
                    _use_separator_character = true;
                }
            }
            else {
                // Use of separator character explicitly disabled
                _use_separator_character = false;
            }
        }
        else {
            // Use of separator character is disabled by default
            _use_separator_character = false;
        }

        // Reserve capacity for separator characters (inserted between two groups)
        std::size_t num_separator_characters = 0u;
        if (_use_separator_character) {
            num_separator_characters = num_characters / _group_size;

            // Do not include a leading separator character if the number of characters is an even multiple of the group size
            // Example: 0b'0000 should be 0b0000
            if (num_characters && num_characters % _group_size == 0) {
                num_separator_characters -= 1u;
            }
        }

        std::size_t length = num_characters + num_padding_characters + num_separator_characters;
        if (use_base_prefix) {
            // +2 characters for base prefix '0b'
            length += 2u;
        }

        std::size_t capacity = std::max(length, (std::size_t) width);
        std::string result(capacity, fill_character);

        std::size_t write_position;
        switch (justification) {
            case Justification::Left:
                write_position = 0u;
                break;
            case Justification::Right:
                write_position = capacity - length;
                break;
            case Justification::Center:
                write_position = (capacity - length) / 2;
                break;
        }

        // Convert value to binary
        char buffer[sizeof(T) * CHAR_BIT] { 0 };
        char* end = buffer;
        for (int i = 0; i < (int) num_characters; ++i, ++end) {
            int bit = (value >> (num_characters - 1 - i)) & 1;
            *end = (char) (48 + bit); // 48 is the ASCII code for '0'
        }

        if (use_base_prefix) {
            result[write_position++] = '0';
            result[write_position++] = 'b';
        }

        if (_use_separator_character) {
            std::size_t current = 0u;

            for (std::size_t i = 0u; i < num_padding_characters; ++i, ++current) {
                if (current && (num_characters - current) % _group_size == 0u) {
                    result[write_position++] = '\'';
                }

                result[write_position++] = value < 0 ? '1' : '0';
            }

            for (char* start = buffer; start != end; ++start, ++current) {
                if (current && (num_characters - current) % _group_size == 0u) {
                    result[write_position++] = '\'';
                }

                result[write_position++] = *start;
            }
        }
        else {
            for (std::size_t i = 0u; i < num_padding_characters; ++i) {
                result[write_position++] = value < 0 ? '1' : '0';
            }

            // Copy contents directly
            std::size_t num_characters_written = end - buffer;
            result.replace(write_position, num_characters_written, buffer, 0, num_characters_written);
        }

        return std::move(result);
    }

    template <typename T>
    std::string IntegerFormatter<T>::to_hexadecimal(T value) const {
        std::size_t num_characters;

        // Compute the minimum number of characters to hold the formatted value
        if (value < 0) {
            // Twos complement is used for formatting negative values, which by default uses as many digits as required by the system architecture
            num_characters = sizeof(T) * CHAR_BIT / 4;
        }
        else {
            // The minimum number of digits required to format a binary number is log2(n) + 1
            // Each hexadecimal character represents 4 bits
            num_characters = (std::size_t) std::floor(std::log2(value) / 4) + 1u;
        }

        std::size_t num_padding_characters = 0u;

        // The number of characters can be overridden by a user-specified 'digits' value
        // If the desired number of digits is smaller than the required number of digits, remove digits starting from the front (most significant) bits
        // If the desired number of digits is larger than the required number of digits, append digits to the front (1 for negative integers, 0 for positive integers)
        if (digits) {
            std::uint8_t _digits = *digits;
            if (num_characters >= _digits) {
                num_characters = _digits;
            }
            else {
                // Append leading padding characters to reach the desired number of digits
                num_padding_characters = _digits - num_characters;
            }
        }

        bool _use_separator_character = false;
        std::uint8_t _group_size = 0u;

        if (use_separator_character) {
            if (*use_separator_character) {
                if (group_size) {
                    _group_size = *group_size;

                    if (*group_size) {
                        _use_separator_character = true;
                    }
                    else {
                        // Group size explicitly provided as 0, use of separator character is disabled
                        _use_separator_character = false;
                    }
                }
                else {
                    // Group size is 4 by default (if not specified)
                    _group_size = 4u;
                    _use_separator_character = true;
                }
            }
            else {
                // Use of separator character explicitly disabled
                _use_separator_character = false;
            }
        }
        else {
            // Use of separator character is disabled by default
            _use_separator_character = false;
        }

        // Reserve capacity for separator characters (inserted between two groups)
        std::size_t num_separator_characters = 0u;
        if (_use_separator_character) {
            num_separator_characters = (num_characters + num_padding_characters) / _group_size;

            // Do not include a leading separator character if the number of characters is an even multiple of the group size
            // Example: 0b'0000 should be 0b0000
            if ((num_characters + num_padding_characters) % _group_size == 0) {
                num_separator_characters -= 1u;
            }
        }

        std::size_t length = num_characters + num_padding_characters + num_separator_characters;

        if (use_base_prefix) {
            // +2 characters for base prefix '0x'
            length += 2u;
        }

        std::size_t capacity = std::max(length, (std::size_t) width);
        std::string result(capacity, fill_character);

        std::size_t write_position;
        switch (justification) {
            case Justification::Left:
                write_position = 0u;
                break;
            case Justification::Right:
                write_position = capacity - length;
                break;
            case Justification::Center:
                write_position = (capacity - length) / 2;
                break;
        }

        // Convert value to binary
        std::size_t num_characters_binary = num_characters * 4u;
        char buffer[sizeof(T) * CHAR_BIT] { '0' };
        char* end = buffer;
        for (int i = 0; i < (int) num_characters_binary; ++i, ++end) {
            int bit = (value >> (num_characters_binary - 1 - i)) & 1;
            *end = (char) (48 + bit); // 48 is the ASCII code for '0'
        }

        if (use_base_prefix) {
            result[write_position++] = '0';
            result[write_position++] = 'x';
        }

        char nibble[4] = { '0' };

        // Convert binary representation to hexadecimal
        // Pad first group binary representation if necessary, as this group might not be the same size as the rest
        // Example: 0b01001 has two groups (000)1 and 1001
        std::size_t num_groups = num_characters_binary / 4u;
        std::size_t remainder = num_characters_binary % 4u;
        std::size_t num_padding_characters_binary = 0u;

        if (remainder) {
            // Number of binary characters does not equally divide into a hexadecimal representation, first group will require padding characters
            num_groups += 1u;
            num_padding_characters_binary = 4u - remainder;
        }

        if (_use_separator_character) {
            std::size_t current = 0u;

            // Append any extra padding characters
            for (std::size_t i = 0u; i < num_padding_characters; ++i, ++current) {
                if (current && (num_characters - current) % _group_size == 0u) {
                    result[write_position++] = '\'';
                }

                result[write_position++] = value < 0 ? 'f' : '0';
            }

            char* ptr = buffer;
            for (std::size_t group = 0; group < num_groups; ++group, ++current) {
                // Insert separator
                if (current && (num_characters - current) % _group_size == 0u) {
                    result[write_position++] = '\'';
                }

                // Convert binary to hexadecimal
                int i = 0;

                // Append any necessary padding characters to the front of the nibble
                if (num_padding_characters_binary) {
                    for (; i < num_padding_characters_binary; ++i) {
                        nibble[i] = value < 0 ? '1' : '0';
                    }
                    // Padding is only applicable to the first group
                    num_padding_characters_binary = 0u;
                }

                while (i != 4) {
                    nibble[i++] = *ptr++;
                }
                result[write_position++] = detail::nibble_to_hexadecimal(nibble);
            }
        }
        else {
            for (std::size_t i = 0u; i < num_padding_characters; ++i) {
                result[write_position++] = value < 0 ? 'f' : '0';
            }

            char* ptr = buffer;
            for (std::size_t group = 0; group < num_groups; ++group) {
                // Convert binary to hexadecimal
                int i = 0;

                // Append any necessary padding characters to the front of the nibble
                if (num_padding_characters_binary) {
                    for (; i < num_padding_characters_binary; ++i) {
                        nibble[i] = value < 0 ? '1' : '0';
                    }
                    // Padding is only applicable to the first group
                    num_padding_characters_binary = 0u;
                }

                while (i != 4) {
                    nibble[i++] = *ptr++;
                }
                result[write_position++] = detail::nibble_to_hexadecimal(nibble);
            }
        }

        return std::move(result);
    }

    template <typename T>
    FloatingPointFormatter<T>::FloatingPointFormatter() : FormatterBase(),
                                                          representation(Representation::Fixed),
                                                          sign(Sign::NegativeOnly),
                                                          precision(std::numeric_limits<T>::digits10) {
        static_assert(is_floating_point_type<T>::value, "value must be a floating point type");
    }

    template <typename T>
    FloatingPointFormatter<T>::~FloatingPointFormatter() = default;

    template <typename T>
    void FloatingPointFormatter<T>::parse(const FormatSpec& spec) {
        ASSERT(spec.type() == FormatSpec::Type::SpecifierList, "format spec for floating point types must be a specifier list");
        FormatterBase::parse(spec);
        
        if (spec.has_specifier("representation")) {
            std::string_view value = trim(spec.get_specifier("representation"));
            if (icasecmp(value, "fixed")) {
                representation = Representation::Fixed;
            }
            else if (icasecmp(value, "scientific")) {
                representation = Representation::Scientific;
            }
        }

        if (spec.has_specifier("sign")) {
            std::string_view value = trim(spec.get_specifier("sign"));
            if (icasecmp(value, "negative only") || icasecmp(value, "negative_only") || icasecmp(value, "negativeonly")) {
                sign = Sign::NegativeOnly;
            }
            else if (icasecmp(value, "aligned")) {
                sign = Sign::Aligned;
            }
            else if (icasecmp(value, "both")) {
                sign = Sign::Both;
            }
        }

        if (spec.has_specifier("precision")) {
            std::string_view value = trim(spec.get_specifier("precision"));

            unsigned _precision;
            std::size_t num_characters_read = from_string(value, _precision);
            
            if (num_characters_read > 0) {
                precision = _precision;
            }
        }

        if (spec.has_specifier("use_separator", "useseparator", "use_separator_character", "useseparatorcharacter")) {
            std::string_view value = trim(spec.get_specifier("use_separator", "useseparator", "use_separator_character", "useseparatorcharacter").value);
            
            if (icasecmp(value, "true") || icasecmp(value, "1")) {
                use_separator_character = true;
            }
            else if (icasecmp(value, "false") || icasecmp(value, "0")) {
                use_separator_character = false;
            }
        }
    }

    template <typename T>
    std::string FloatingPointFormatter<T>::format(T value) const {
        std::size_t length = 0u;
        std::size_t read_offset = 0u;

        // Sign character
        length += value < 0 ? 1u : unsigned(sign != Sign::NegativeOnly);

        int num_significant_figures = std::numeric_limits<T>::digits10 + 1;
        if (precision) {
            num_significant_figures = precision;
        }

        std::chars_format format_flags = std::chars_format::fixed;
        if (representation == Representation::Scientific) {
            format_flags = std::chars_format::scientific;
        }
        constexpr int num_significant_digits = std::numeric_limits<T>::max_digits10; // Max number of significant digits
        constexpr int sign_character = 1; // 1 character for sign
        constexpr int decimal_character = 1; // 1 character for the decimal point
        constexpr int exponent_character = 1; // 'e' character
        constexpr int exponent_sign = 1; // '+' or '-' sign for exponent
        constexpr int exponent_digits = std::numeric_limits<T>::max_exponent10; // Max number of digits in the exponent

        char buffer[sign_character + num_significant_digits + decimal_character + exponent_character + exponent_sign + exponent_digits];
        char* start = buffer;
        char* end = buffer + sizeof(buffer) / sizeof(buffer[0]);

        // std::numeric_limits<T>::digits10 represents the number of decimal places that are guaranteed to be preserved when converted to text
        // Note: last decimal place will be rounded
        int conversion_precision = std::clamp(num_significant_figures, 0, std::numeric_limits<T>::digits10 + 1);
        const auto& [ptr, error_code] = std::to_chars(start, end, value, format_flags, conversion_precision);

        std::size_t num_characters_written = ptr - (start + read_offset);
        length += num_characters_written;

        // Additional precision
        length += std::max(0, num_significant_figures - conversion_precision);

        std::size_t decimal_position = num_characters_written;
        if (use_separator_character) {
            char* decimal = std::find(start + read_offset, ptr, '.');
            decimal_position = decimal - (start + read_offset);

            // Separators get inserted every 3 characters up until the position of the decimal point
            length += (decimal_position - 1) / 3;
        }

        std::string result(length, fill_character);
        std::size_t write_position = 0;

        if (value < 0) {
            result[write_position++] = '-';
        }
        else {
            switch (sign) {
                case Sign::Aligned:
                    result[write_position++] = ' ';
                    break;
                case Sign::Both:
                    result[write_position++] = '+';
                    break;
                case Sign::NegativeOnly:
                default:
                    break;
            }
        }

        if (representation == Representation::Scientific) {
            char* e = std::find(buffer, ptr, 'e');
            std::size_t e_position = e - (start + read_offset);

            for (std::size_t i = 0u; i < e_position; ++i) {
                result[write_position++] = *(buffer + read_offset + i);
            }

            // For scientific notation, fake precision must be appended before the 'e' denoting the exponent
            for (std::size_t i = conversion_precision; i < num_significant_figures; ++i) {
                result[write_position++] = '0';
            }

            for (start = buffer + read_offset + e_position; start != ptr; ++start) {
                result[write_position++] = *start;
            }
        }
        else {
            // Separator character only makes sense for fixed floating point values
            if (use_separator_character) {
                // Separators get inserted every 3 characters up until the position of the decimal point
                std::size_t group_size = 3;
                std::size_t counter = group_size - (decimal_position % group_size);

                // Write the number portion, up until the decimal point (with separators)
                for (std::size_t i = 0; i < decimal_position; ++i, ++counter) {
                    if (i && counter % group_size == 0u) {
                        result[write_position++] = ',';
                    }

                    result[write_position++] = *(buffer + read_offset + i);
                }

                // Write decimal portion
                for (start = buffer + read_offset + decimal_position; start != ptr; ++start) {
                    result[write_position++] = *start;
                }
            }
            else {
                for (start = buffer + read_offset; start != ptr; ++start) {
                    result[write_position++] = *start;
                }
            }

            // For regular floating point values, fake higher precision by appending the remaining decimal places as 0
            for (std::size_t i = conversion_precision; i < num_significant_figures; ++i) {
                result[write_position++] = '0';
            }
        }

        return std::move(FormatterBase::format(result));
    }
    
    template <typename T, typename U>
    Formatter<std::pair<T, U>>::Formatter() : FormatterBase(),
                                              m_formatters() {
    }
    
    template <typename T, typename U>
    Formatter<std::pair<T, U>>::~Formatter() = default;
    
    template <typename T, typename U>
    void Formatter<std::pair<T, U>>::parse(const utils::FormatSpec& spec) {
        if (spec.type() == FormatSpec::Type::SpecifierList) {
            FormatterBase::parse(spec);
        }
        else {
            if (spec.has_group(0)) {
                const FormatSpec& group = spec.get_group(0);
                ASSERT(group.type() == FormatSpec::Type::SpecifierList, "invalid std::pair format spec - formatting group 0 must be a specifier list");
                FormatterBase::parse(group);
            }
            if (spec.has_group(1)) {
                m_formatters.first.parse(spec.get_group(1));
            }
            if (spec.has_group(2)) {
                m_formatters.second.parse(spec.get_group(2));
            }
        }
    }
    
    template <typename T, typename U>
    std::string Formatter<std::pair<T, U>>::format(const std::pair<T, U>& value) const {
        std::string first = m_formatters.first.format(value.first);
        std::string second = m_formatters.second.format(value.second);
        
        // Format: { first, second }
        
        // 2 characters for container opening / closing braces { }
        // 2 characters for leading space before the first element and trailing space after the last element
        // 2 characters for comma + space in between characters
        std::size_t length = 6 + first.length() + second.length();
        std::string result(length, fill_character);
        
        std::size_t write_position = 0;
        
        result[write_position++] = '{';
        result[write_position++] = ' ';
        
        // First
        length = first.length();
        result.replace(write_position, length, first, 0, length);
        write_position += length;
        
        result[write_position++] = ',';
        result[write_position++] = ' ';
        
        // Second
        length = second.length();
        result.replace(write_position, length, second, 0, length);
        write_position += length;
        
        result[write_position++] = ' ';
        result[write_position++] = '}';
        
        return std::move(FormatterBase::format(result));
    }
    
    // std::tuple
    template <typename ...Ts>
    Formatter<std::tuple<Ts...>>::Formatter() : FormatterBase(),
                                                m_formatters() {
    }
    
    template <typename ...Ts>
    Formatter<std::tuple<Ts...>>::~Formatter() = default;
    
    template <typename ...Ts>
    void Formatter<std::tuple<Ts...>>::parse(const utils::FormatSpec& spec) {
        if (spec.type() == FormatSpec::Type::SpecifierList) {
            FormatterBase::parse(spec);
        }
        else {
            // Specifiers in the first group are applied to the tuple itself
            if (spec.has_group(0)) {
                const FormatSpec& group = spec.get_group(0);
                ASSERT(group.type() == FormatSpec::Type::SpecifierList, "invalid std::tuple format spec - formatting group 0 must be a specifier list");
                FormatterBase::parse(group);
            }
            
            for (std::size_t i = 0; i < sizeof...(Ts); ++i) {
                std::size_t group = i + 1;
                
                if (spec.has_group(group)) {
                    utils::apply([&spec = spec.get_group(group)]<typename T>(Formatter<T>& formatter) {
                        formatter.parse(spec);
                    }, m_formatters, i);
                }
            }
        }
    }
    
    template <typename ...Ts>
    std::string Formatter<std::tuple<Ts...>>::format(const std::tuple<Ts...>& value) const {
        constexpr std::size_t num_elements = sizeof...(Ts);
        if constexpr (num_elements == 0) {
            return "{ }";
        }
        
        std::vector<std::string> elements { };
        elements.reserve(num_elements);
        
        // Format: { first, second, ... }
        
        // 2 characters for container opening / closing braces { }
        // 2 characters for leading space before the first element and trailing space after the last element
        // 2 characters for comma + space between two elements (per element - 1)
        std::size_t length = 4 + (sizeof...(Ts) - 1) * 2;
        
        // Cache formatted values and their lengths
        utils::apply([&elements, &length, &formatters = m_formatters]<typename T, std::size_t I>(const T& value) {
            elements.emplace_back(std::get<I>(formatters).format(value));
            length += elements.back().length();
        }, value);
        
        std::string result(length, fill_character);
        std::size_t write_position = 0;

        result[write_position++] = '{';
        result[write_position++] = ' ';
        
        for (std::size_t i = 0u; i < num_elements; ++i) {
            length = elements[i].length();
            result.replace(write_position, length, elements[i], 0, length);
            write_position += length;

            // Elements are formatted into a comma-separated list
            if (i != num_elements - 1) {
                // Do not insert a trailing comma
                result[write_position++] = ',';
                result[write_position++] = ' ';
            }
        }
        
        result[write_position++] = ' ';
        result[write_position++] = '}';
        
        return std::move(FormatterBase::format(result));
    }
    
    template <typename T>
    Formatter<std::vector<T>>::Formatter() : FormatterBase(),
                                             m_formatter() {
    }
    
    template <typename T>
    Formatter<std::vector<T>>::~Formatter() = default;
    
    template <typename T>
    void Formatter<std::vector<T>>::parse(const utils::FormatSpec& spec) {
        if (spec.type() == FormatSpec::Type::SpecifierList) {
            FormatterBase::parse(spec);
        }
        else {
            if (spec.has_group(0)) {
                const FormatSpec& group = spec.get_group(0);
                ASSERT(group.type() == FormatSpec::Type::SpecifierList, "invalid std::vector format spec - formatting group 0 must be a specifier list");
                FormatterBase::parse(group);
            }
            if (spec.has_group(1)) {
                m_formatter.parse(spec.get_group(1));
            }
        }
    }
    
    template <typename T>
    std::string Formatter<std::vector<T>>::format(const std::vector<T>& value) const {
        if (value.empty()) {
            return "[ ]";
        }
        
        std::size_t num_elements = value.size();
        std::vector<std::string> elements;
        elements.reserve(num_elements);
        
        // Format: [ 1, 2, 3, ... ]
        
        // 2 characters for container opening / closing brackets [ ]
        // 2 characters for leading space before the first element and trailing space after the last element
        // 2 characters for comma + space between two elements (per element - 1)
        std::size_t length = 4 + (num_elements - 1) * 2;
        for (const T& element : value) {
            elements.emplace_back(m_formatter.format(element));
            length += elements.back().length();
        }
        
        std::string result(length, fill_character);
        std::size_t write_position = 0;
        
        result[write_position++] = '[';
        result[write_position++] = ' ';
        
        for (std::size_t i = 0; i < num_elements; ++i) {
            length = elements[i].length();
            result.replace(write_position, length, elements[i], 0, length);
            write_position += length;
            
            // Elements are formatted into a comma-separated list
            if (i != num_elements - 1) {
                // Do not insert a trailing comma
                result[write_position++] = ',';
                result[write_position++] = ' ';
            }
        }
        
        result[write_position++] = ' ';
        result[write_position++] = ']';
        
        return std::move(FormatterBase::format(result));
    }
    
    template <typename K, typename V, typename H, typename P, typename A>
    Formatter<std::unordered_map<K, V, H, P, A>>::Formatter() : FormatterBase(),
                                                                m_key_formatter(),
                                                                m_value_formatter() {
    }
    
    template <typename K, typename V, typename H, typename P, typename A>
    Formatter<std::unordered_map<K, V, H, P, A>>::~Formatter() = default;

    template <typename K, typename V, typename H, typename P, typename A>
    void Formatter<std::unordered_map<K, V, H, P, A>>::parse(const FormatSpec& spec) {
        if (spec.type() == FormatSpec::Type::SpecifierList) {
            // A format spec consisting of a list of specifiers is applied globally to the unordered map
            FormatterBase::parse(spec);
        }
        else {
            if (spec.has_group(0)) {
                const FormatSpec& group = spec.get_group(0);
                ASSERT(group.type() == FormatSpec::Type::SpecifierList, "invalid std::unordered_map format spec - formatting group 0 must be a specifier list");
                FormatterBase::parse(group);
            }
            if (spec.has_group(1)) {
                // The second formatting group is applied to the map key type
                m_key_formatter.parse(spec.get_group(1));
            }
            if (spec.has_group(2)) {
                // The third formatting group is applied to the map value type
                m_value_formatter.parse(spec.get_group(2));
            }
        }
    }
    
    template <typename K, typename V, typename H, typename P, typename A>
    std::string Formatter<std::unordered_map<K, V, H, P, A>>::format(const T& value) const {
        if (value.empty()) {
            return "{ }";
        }
        
        std::size_t num_elements = value.size();

        // Format elements
        std::vector<std::pair<std::string, std::string>> elements;
        elements.reserve(num_elements);
        for (auto iter = std::begin(value); iter != std::end(value); ++iter) {
            elements.emplace_back(m_key_formatter.format(iter->first), m_value_formatter.format(iter->second));
        }

        // Format: { { key: value }, { key: value }, ... }
        
        // 2 characters for container opening / closing braces { }
        // 2 characters for leading space before the first element and trailing space after the last element
        
        // 2 characters for element opening / closing braces { } (per element)
        // 2 characters for leading space before the element key and trailing space after the element value (per element)
        // 2 characters for comma + space between element key and value (per element)
        // 2 characters for comma + space between two elements (per element - 1)
        std::size_t length = 4 + (num_elements * 6) + (num_elements - 1) * 2;
        for (const std::pair<std::string, std::string>& element : elements) {
            length += element.first.length();
            length += element.second.length();
        }

        std::string result(length, fill_character);
        std::size_t write_position = 0;

        result[write_position++] = '{';
        result[write_position++] = ' ';

        for (std::size_t i = 0u; i < num_elements; ++i) {
            // Element format: { key: value }
            const std::pair<std::string, std::string>& element = elements[i];
            
            result[write_position++] = '{';
            result[write_position++] = ' ';
            
            // Key
            length = element.first.length();
            result.replace(write_position, length, element.first, 0, length);
            write_position += length;

            result[write_position++] = ':';
            result[write_position++] = ' ';
            
            // Value
            length = element.second.length();
            result.replace(write_position, length, element.second, 0, length);
            write_position += length;
            
            result[write_position++] = ' ';
            result[write_position++] = '}';
            
            // Elements are formatted into a comma-separated list
            if (i != num_elements - 1) {
                // Do not insert a trailing comma
                result[write_position++] = ',';
                result[write_position++] = ' ';
            }
        }

        result[write_position++] = ' ';
        result[write_position++] = '}';

        return std::move(FormatterBase::format(result));
    }
    
    template <typename K, typename H, typename E, typename A>
    Formatter<std::unordered_set<K, H, E, A>>::Formatter() : Formatter<K>() {
    }
    
    template <typename K, typename H, typename E, typename A>
    Formatter<std::unordered_set<K, H, E, A>>::~Formatter() = default;

    template <typename K, typename H, typename E, typename A>
    void Formatter<std::unordered_set<K, H, E, A>>::parse(const utils::FormatSpec& spec) {
        if (spec.type() == FormatSpec::Type::SpecifierList) {
            // A format spec consisting of a list of specifiers is applied globally to the unordered map
            FormatterBase::parse(spec);
        }
        else {
            if (spec.has_group(0)) {
                const FormatSpec& group = spec.get_group(0);
                ASSERT(group.type() == FormatSpec::Type::SpecifierList, "invalid std::unordered_map format spec - formatting group 0 must be a specifier list");
                FormatterBase::parse(group);
            }
            if (spec.has_group(1)) {
                // The second formatting group is applied to the underlying set type
                Formatter<K>::parse(spec.get_group(1));
            }
        }
    }
    
    template <typename K, typename H, typename E, typename A>
    std::string Formatter<std::unordered_set<K, H, E, A>>::format(const T& value) const {
        if (value.empty()) {
            return "{ }";
        }
        
        std::size_t num_elements = value.size();

        // Format elements
        std::vector<std::string> elements;
        elements.reserve(num_elements);
        
        for (const K& element : value) {
            elements.emplace_back(Formatter<K>::format(element));
        }
        
        // Format: { value, ... }
        
        // 2 characters for container opening / closing braces { }
        // 2 characters for leading space before the first element and trailing space after the last element
        // 2 characters for comma + space between two elements (per element - 1)
        std::size_t length = 4 + (num_elements - 1) * 2;
        for (const std::string& element : elements) {
            length += element.length();
        }

        std::string result(length, Formatter<K>::fill_character);
        std::size_t write_position = 0;

        result[write_position++] = '{';
        result[write_position++] = ' ';

        for (std::size_t i = 0u; i < num_elements; ++i) {
            // Element format: { key: value }
            const std::string& element = elements[i];

            length = element.length();
            result.replace(write_position, length, element, 0, length);
            write_position += length;
            
            // Elements are formatted into a comma-separated list
            if (i != num_elements - 1) {
                // Do not insert a trailing comma
                result[write_position++] = ',';
                result[write_position++] = ' ';
            }
        }

        result[write_position++] = ' ';
        result[write_position++] = '}';

        return std::move(FormatterBase::format(result));
    }
    
    template <typename T>
    std::string Formatter<NamedArgument<T>>::format(const NamedArgument<T>& value) {
        return Formatter<T>::format(value.value);
    }
    
}

#endif // STRING_TPP

