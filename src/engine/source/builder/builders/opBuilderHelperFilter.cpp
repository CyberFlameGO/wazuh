/* Copyright (C) 2015-2021, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */
#include "opBuilderHelperFilter.hpp"

#include <optional>
#include <string>
#include <tuple>

#include <fmt/format.h>
#include <re2/re2.h>

#include "syntax.hpp"
#include <utils/ipUtils.hpp>
#include <utils/stringUtils.hpp>

namespace
{

using opString = std::optional<std::string>;
using builder::internals::syntax::REFERENCE_ANCHOR;
using std::runtime_error;
using std::string;

constexpr const char* SUCCESS_TRACE_MSG {"{} Condition Success."};
constexpr const char* FAILURE_TRACE_MSG {"{} Condition Failure."};

/**
 * @brief Get the Comparator operator, and the value to compare
 * or the reference to value to compare
 *
 * @param def The JSON definition of the operator
 * @return std::tuple<std::string, opString, opString> the operator,
 * the value to compare and the reference to value to compare (if exists)
 * @throw std::runtime_error if the number of parameters is not valid
 * @throw std::logic_error if the json node is not valid definition for the
 * helper function
 */
std::tuple<std::string, opString, opString>
getCompOpParameter(const base::DocumentValue& def)
{
    // Get destination path
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};
    // Get function helper
    if (!def.MemberBegin()->value.IsString())
    {
        throw std::logic_error("Invalid operator definition");
    }
    std::string rawValue {def.MemberBegin()->value.GetString()};

    // Parse parameters
    std::vector<std::string> parameters {utils::string::split(rawValue, '/')};
    if (parameters.size() != 2)
    {
        throw runtime_error("Invalid number of parameters");
    }

    std::optional<std::string> refValue {};
    std::optional<std::string> value {};

    if (parameters[1][0] == REFERENCE_ANCHOR)
    {
        refValue = json::formatJsonPath(parameters[1].substr(1));
    }
    else
    {
        value = parameters[1];
    }

    return {field, refValue, value};
}
} // namespace

namespace builder::internals::builders
{

// <key>: +exists
std::function<bool(base::Event)> opBuilderHelperExists(const base::DocumentValue& def,
                                                       types::TracerFn tr)
{
    // Get Field path to check
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};

    // Check parameters
    std::vector<std::string> parameters {
        utils::string::split(def.MemberBegin()->value.GetString(), '/')};
    if (parameters.size() != 1)
    {
        throw runtime_error("Invalid number of parameters");
    }

    // Tracing
    std::string successTrace = fmt::format("{{{}: +exists}} Condition Success",
                                           def.MemberBegin()->name.GetString());
    std::string failureTrace = fmt::format("{{{}: +exists}} Condition Failure",
                                           def.MemberBegin()->name.GetString());

    // Return Function
    return [=](base::Event e) {
        if (e->getEvent()->exists(field))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +not_exists
std::function<bool(base::Event)> opBuilderHelperNotExists(const base::DocumentValue& def,
                                                          types::TracerFn tr)
{
    // Get Field path to check
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};

    std::vector<std::string> parameters =
        utils::string::split(def.MemberBegin()->value.GetString(), '/');
    if (parameters.size() != 1)
    {
        throw runtime_error("Invalid number of parameters");
    }

    // Tracing
    string successTrace = fmt::format("{{{}: +not_exists}} Condition Success",
                                      def.MemberBegin()->name.GetString());
    string failureTrace = fmt::format("{{{}: +not_exists}} Condition Failure",
                                      def.MemberBegin()->name.GetString());

    // Return Function
    return [=](base::Event e) {
        if (!e->getEvent()->exists(field))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

//*************************************************
//*           String filters                      *
//*************************************************

bool opBuilderHelperStringComparison(const std::string key,
                                     char op,
                                     base::Event& e,
                                     std::optional<std::string> refValue,
                                     std::optional<std::string> value)
{

    // TODO Remove try catch or if nullptr after fix get method of document
    // class
    // TODO Update to use proper references
    // TODO Following the philosofy of doing as much as possible in the build
    // phase this function should
    //      return another function used by the filter, instead of deciding the
    //      operator on runtime
    // TODO string and int could be merged if they used the same comparators
    // Get value to compare
    const rapidjson::Value* fieldToCompare {};
    try
    {
        fieldToCompare = &e->getEvent()->get(key);
    }
    catch (std::exception& ex)
    {
        // TODO Check exception type
        return false;
    }

    if (fieldToCompare == nullptr || !fieldToCompare->IsString())
    {
        return false;
    }

    // get str to compare
    if (refValue.has_value())
    {
        // Get reference to json event
        // TODO Remove try catch or if nullptr after fix get method of document
        // class
        // TODO Update to use proper references
        const rapidjson::Value* refValueToCheck {};
        try
        {
            refValueToCheck = &e->getEvent()->get(refValue.value());
        }
        catch (std::exception& ex)
        {
            // TODO Check exception type
            return false;
        }

        if (refValueToCheck == nullptr || !refValueToCheck->IsString())
        {
            return false;
        }
        value = std::string {refValueToCheck->GetString()};
    }

    // String operation
    switch (op)
    {
        case '=': return string {fieldToCompare->GetString()} == value.value();
        case '!': return string {fieldToCompare->GetString()} != value.value();
        case '>': return string {fieldToCompare->GetString()} > value.value();
        // case '>=':
        case 'g': return string {fieldToCompare->GetString()} >= value.value();
        case '<': return string {fieldToCompare->GetString()} < value.value();
        // case '<=':
        case 'l': return string {fieldToCompare->GetString()} <= value.value();
        default:
            // if raise here, then the logic is wrong
            throw std::invalid_argument("Invalid operator: '" + string {op} + "' ");
    }

    return false;
}

// <key>: +s_eq/<value>
std::function<bool(base::Event)> opBuilderHelperStringEq(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catch, return false
        if (opBuilderHelperStringComparison(key, '=', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +s_eq_n/<n_chars>/<s2>
std::function<bool(base::Event)> opBuilderHelperStringEqN(const base::DocumentValue& def,
                                                          types::TracerFn tr)
{
    if (!def.MemberBegin()->name.IsString())
    {
        // Logical error
        throw runtime_error(
            "Invalid key type for json_delete_fields operator (str expected).");
    }
    // Get field key to be compared
    string key {json::formatJsonPath(def.MemberBegin()->name.GetString())};

    if (!def.MemberBegin()->value.IsString())
    {
        // Logical error
        throw runtime_error(
            "Invalid parameter type for json_delete_fields operator (str expected).");
    }

    auto parameters {utils::string::split(def.MemberBegin()->value.GetString(), '/')};

    if (3 != parameters.size())
    {
        throw runtime_error(
            "Invalid number of parameters for s_eq_n operator (3 expected).");
    }

    auto n = atoi(parameters[1].c_str());
    string parameter = parameters[2];

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        bool retVal {false};
        try
        {
            string s2;
            string sourceString {(&e->getEventValue(key))->GetString()};

            if (REFERENCE_ANCHOR == parameter[0])
            {
                auto auxS2 = (&e->getEventValue(json::formatJsonPath(parameter.substr(1))))->GetString();
                s2 = auxS2;
            }
            else
            {
                s2 = parameter;
            }

            retVal = (sourceString.substr(0, n) == s2.substr(0, n));

            // try and catch, return false
            if (retVal)
            {
                tr(successTrace);
            }
            else
            {
                tr(failureTrace);
            }
        }
        catch (const std::exception& exc)
        {
            tr(failureTrace + ": " + exc.what());
        }

        return retVal;
    };
} // namespace builder::internals::builders

// <key>: +s_ne/<value>
std::function<bool(base::Event)> opBuilderHelperStringNE(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperStringComparison(key, '!', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +s_gt/<value>|$<ref>
std::function<bool(base::Event)> opBuilderHelperStringGT(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperStringComparison(key, '>', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +s_ge/<value>|$<ref>
std::function<bool(base::Event)> opBuilderHelperStringGE(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperStringComparison(key, 'g', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +s_lt/<value>|$<ref>
std::function<bool(base::Event)> opBuilderHelperStringLT(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperStringComparison(key, '<', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// <key>: +s_le/<value>|$<ref>
std::function<bool(base::Event)> opBuilderHelperStringLE(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{
    auto [key, refValue, value] {getCompOpParameter(def)};

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperStringComparison(key, 'l', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

//*************************************************
//*               Int filters                     *
//*************************************************

bool opBuilderHelperIntComparison(const std::string field,
                                  char op,
                                  base::Event& e,
                                  std::optional<std::string> refValue,
                                  std::optional<int> value)
{

    // TODO Remove try catch or if nullptr after fix get method of document
    // class
    // TODO Update to use proper references
    // TODO Same as opBuilderHelperStringComparison
    // Get value to compare
    const rapidjson::Value* fieldValue {};
    try
    {
        fieldValue = &e->getEvent()->get(field);
    }
    catch (std::exception& ex)
    {
        // TODO Check exception type
        return false;
    }

    if (fieldValue == nullptr || !fieldValue->IsInt())
    {
        return false;
    }

    // get str to compare
    if (refValue.has_value())
    {
        // Get reference to json event
        // TODO Remove try catch or if nullptr after fix get method of document
        // class
        // TODO update to use proper references
        const rapidjson::Value* refValueToCheck {};
        try
        {
            refValueToCheck = &e->getEvent()->get(refValue.value());
        }
        catch (std::exception& ex)
        {
            // TODO Check exception type
            return false;
        }

        if (refValueToCheck == nullptr || !refValueToCheck->IsInt())
        {
            return false;
        }
        value = refValueToCheck->GetInt();
    }

    // Int operation
    switch (op)
    {
        // case '==':
        case '=': return fieldValue->GetInt() == value.value();
        // case '!=':
        case '!': return fieldValue->GetInt() != value.value();
        case '>': return fieldValue->GetInt() > value.value();
        // case '>=':
        case 'g': return fieldValue->GetInt() >= value.value();
        case '<': return fieldValue->GetInt() < value.value();
        // case '<=':
        case 'l': return fieldValue->GetInt() <= value.value();

        default:
            // if raise here, then the source code is wrong
            throw std::invalid_argument("Invalid operator: '" + string {op} + "' ");
    }

    return false;
}

// field: +i_eq/int|$ref/
std::function<bool(base::Event)> opBuilderHelperIntEqual(const base::DocumentValue& def,
                                                         types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        if (opBuilderHelperIntComparison(field, '=', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// field: +i_ne/int|$ref/
std::function<bool(base::Event)>
opBuilderHelperIntNotEqual(const base::DocumentValue& def, types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catche, return false
        if (opBuilderHelperIntComparison(field, '!', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// field: +i_lt/int|$ref/
std::function<bool(base::Event)>
opBuilderHelperIntLessThan(const base::DocumentValue& def, types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catche, return false
        if (opBuilderHelperIntComparison(field, '<', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// field: +i_le/int|$ref/
std::function<bool(base::Event)>
opBuilderHelperIntLessThanEqual(const base::DocumentValue& def, types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catche, return false
        if (opBuilderHelperIntComparison(field, 'l', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// field: +i_gt/int|$ref/
std::function<bool(base::Event)>
opBuilderHelperIntGreaterThan(const base::DocumentValue& def, types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catche, return false
        if (opBuilderHelperIntComparison(field, '>', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

// field: +i_ge/int|$ref/
std::function<bool(base::Event)>
opBuilderHelperIntGreaterThanEqual(const base::DocumentValue& def, types::TracerFn tr)
{

    auto [field, refValue, valuestr] {getCompOpParameter(def)};

    std::optional<int> value = valuestr.has_value()
                                   ? std::optional<int> {std::stoi(valuestr.value())}
                                   : std::nullopt;

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Function
    return [=](base::Event e) {
        // try and catche, return false
        if (opBuilderHelperIntComparison(field, 'g', e, refValue, value))
        {
            tr(successTrace);
            return true;
        }
        else
        {
            tr(failureTrace);
            return false;
        }
    };
}

//*************************************************
//*               Regex filters                   *
//*************************************************

// field: +r_match/regexp
std::function<bool(base::Event)> opBuilderHelperRegexMatch(const base::DocumentValue& def,
                                                           types::TracerFn tr)
{
    // Get field
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};
    string value {def.MemberBegin()->value.GetString()};

    std::vector<std::string> parameters {utils::string::split(value, '/')};
    if (parameters.size() != 2)
    {
        throw std::invalid_argument("Wrong number of arguments passed");
    }

    auto regex_ptr = std::make_shared<RE2>(parameters[1], RE2::Quiet);
    if (!regex_ptr->ok())
    {
        const std::string err =
            "Error compiling regex '" + parameters[1] + "'. " + regex_ptr->error();
        throw runtime_error(err);
    }

    // Return Lifter
    return [=](base::Event e) {
        // TODO Remove try catch
        // TODO Update to use proper reference
        const rapidjson::Value* field_str {};
        try
        {
            field_str = &e->getEvent()->get(field);
        }
        catch (std::exception& ex)
        {
            // TODO Check exception type
            return false;
        }
        if (field_str != nullptr && field_str->IsString())
        {
            return (RE2::PartialMatch(field_str->GetString(), *regex_ptr));
        }
        return false;
    };
}

// field: +r_not_match/regexp
std::function<bool(base::Event)>
opBuilderHelperRegexNotMatch(const base::DocumentValue& def, types::TracerFn tr)
{
    // Get field
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};
    string value = def.MemberBegin()->value.GetString();

    std::vector<std::string> parameters = utils::string::split(value, '/');
    if (parameters.size() != 2)
    {
        throw runtime_error("Invalid number of parameters");
    }

    auto regex_ptr = std::make_shared<RE2>(parameters[1], RE2::Quiet);
    if (!regex_ptr->ok())
    {
        const std::string err =
            "Error compiling regex '" + parameters[1] + "'. " + regex_ptr->error();
        throw runtime_error(err);
    }

    // Tracing
    base::Document defTmp {def};
    string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Lifter
    return [=](base::Event e) {
        // TODO Remove try catch
        // TODO Update to use proper reference
        const rapidjson::Value* field_str {};
        try
        {
            field_str = &e->getEvent()->get(field);
        }
        catch (std::exception& ex)
        {
            // TODO Check exception type
            tr(failureTrace);
            return false;
        }
        if (field_str != nullptr && field_str->IsString())
        {
            if (!RE2::PartialMatch(field_str->GetString(), *regex_ptr))
            {
                tr(successTrace);
                return true;
            }
            else
            {
                tr(failureTrace);
                return false;
            }
        }
        tr(failureTrace);
        return false;
    };
}

//*************************************************
//*               IP filters                     *
//*************************************************

// path_to_ip: +ip_cidr/192.168.0.0/16
// path_to_ip: +ip_cidr/192.168.0.0/255.255.0.0
std::function<bool(base::Event)> opBuilderHelperIPCIDR(const base::DocumentValue& def,
                                                       types::TracerFn tr)
{
    // Get Field path to check
    string field {json::formatJsonPath(def.MemberBegin()->name.GetString())};
    // Get function helper
    std::string rawValue = def.MemberBegin()->value.GetString();

    std::vector<std::string> parameters = utils::string::split(rawValue, '/');
    if (parameters.size() != 3)
    {
        throw runtime_error("Invalid number of parameters");
    }
    else if (parameters[2].empty())
    {
        throw runtime_error("The network can't be empty");
    }
    else if (parameters[1].empty())
    {
        throw runtime_error("The cidr can't be empty");
    }

    uint32_t network {};
    try
    {
        network = utils::ip::IPv4ToUInt(parameters[1]);
    }
    catch (std::exception& e)
    {
        throw runtime_error("Invalid IPv4 address: " + network);
    }

    uint32_t mask {};
    try
    {
        mask = utils::ip::IPv4MaskUInt(parameters[2]);
    }
    catch (std::exception& e)
    {
        throw runtime_error("Invalid IPv4 mask: " + mask);
    }

    uint32_t net_lower {network & mask};
    uint32_t net_upper {net_lower | (~mask)};

    // Tracing
    base::Document defTmp {def};
    std::string successTrace = fmt::format(SUCCESS_TRACE_MSG, defTmp.str());
    std::string failureTrace = fmt::format(FAILURE_TRACE_MSG, defTmp.str());

    // Return Lifter
    return [=](base::Event e) {
        // TODO Remove try catch
        // TODO Update to use proper reference
        const rapidjson::Value* field_str {};
        try
        {
            field_str = &e->getEvent()->get(field);
        }
        catch (std::exception& ex)
        {
            tr(failureTrace);
            return false;
        }
        if (field_str != nullptr && field_str->IsString())
        {
            uint32_t ip {};
            try
            {
                ip = utils::ip::IPv4ToUInt(field_str->GetString());
            }
            catch (std::exception& ex)
            {
                tr(failureTrace);
                return false;
            }
            if (ip >= net_lower && ip <= net_upper)
            {
                tr(successTrace);
                return true;
            }
            else
            {
                tr(failureTrace);
                return false;
            }
        }
        tr(failureTrace);
        return false;
    };
}

} // namespace builder::internals::builders
