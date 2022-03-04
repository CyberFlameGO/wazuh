/* Copyright (C) 2015-2021, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include <gtest/gtest.h>
#include <vector>
#include <re2/re2.h>

#include "OpBuilderHelperFilter.hpp"
#include "testUtils.hpp"

using namespace builder::internals::builders;

TEST(opBuilderHelperRegexMatch, Builds)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/regexp"}
    })"};
    ASSERT_NO_THROW(opBuilderHelperRegexMatch(*doc.get("/check")));
}

TEST(opBuilderHelperRegexMatch, NotEnoughArgumentsError)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/"}
    })"};
    ASSERT_THROW(opBuilderHelperRegexMatch(*doc.get("/check")), std::invalid_argument);
}

TEST(opBuilderHelperRegexMatch, TooManyArgumentsError)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/regexp/regexp2"}
    })"};
    ASSERT_THROW(opBuilderHelperRegexMatch(*doc.get("/check")), std::invalid_argument);
}

TEST(opBuilderHelperRegexMatch, InvalidRegex)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/(\\w{"}
    })"};

    ASSERT_THROW(opBuilderHelperRegexMatch(*doc.get("/check")), std::runtime_error);
}

TEST(opBuilderHelperRegexMatch, InvalidSrcType)
{
    Document doc{R"({
        "check":
            {"fieldSrc": "+r_match/\\d+"}
    })"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            // Object
            s.on_next(Event{R"(
                {"fieldSrc": { "fieldSrc" : "child value"} }
            )"});
            // Number
            s.on_next(Event{R"(
                {"fieldSrc":55}
            )"});
            // Array
            s.on_next(Event{R"(
                {"fieldSrc":[123]}
            )"});
            // Not existing field
            s.on_next(Event{R"(
                {"field":"fieldSrc not exist"}
            )"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/check"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 0);
}

TEST(opBuilderHelperRegexMatch, StringRegexMatch)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/exp"}
    })"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            s.on_next(Event{R"(
                {"field":"exp"}
            )"});
            s.on_next(Event{R"(
                {"field":"expregex"}
            )"});
            s.on_next(Event{R"(
                {"field":"this is a test exp"}
            )"});
            s.on_next(Event{R"(
                {"field":"value"}
            )"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/check"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 3);
    ASSERT_TRUE(RE2::PartialMatch(expected[0].get("/field")->GetString(), "exp"));
    ASSERT_TRUE(RE2::PartialMatch(expected[1].get("/field")->GetString(), "exp"));
    ASSERT_TRUE(RE2::PartialMatch(expected[2].get("/field")->GetString(), "exp"));
}

TEST(opBuilderHelperRegexMatch, NumericRegexMatch)
{
    Document doc{R"({
        "check":
            {"field": "+r_match/123"}
    })"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            s.on_next(Event{R"(
                {"field":"123"}
            )"});
            s.on_next(Event{R"(
                {"field":"123.02"}
            )"});
            s.on_next(Event{R"(
                {"field":"10123"}
            )"});
            s.on_next(Event{R"(
                {"field":"234"}
            )"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/check"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 3);
    ASSERT_TRUE(RE2::PartialMatch(expected[0].get("/field")->GetString(), "123"));
    ASSERT_TRUE(RE2::PartialMatch(expected[1].get("/field")->GetString(), "123"));
    ASSERT_TRUE(RE2::PartialMatch(expected[2].get("/field")->GetString(), "123"));
}

TEST(opBuilderHelperRegexMatch, AdvancedRegexMatch)
{
    Document doc{R"~~({
        "check":
            {"field": "+r_match/([^ @]+)@([^ @]+)"}
    })~~"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            s.on_next(Event{R"(
                {"field":"client@wazuh.com"}
            )"});
            s.on_next(Event{R"(
                {"field":"engine@wazuh.com"}
            )"});
            s.on_next(Event{R"(
                {"field":"wazuh.com"}
            )"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/check"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 2);
    ASSERT_TRUE(RE2::PartialMatch(expected[0].get("/field")->GetString(),
                                  "([^ @]+)@([^ @]+)"));
    ASSERT_TRUE(RE2::PartialMatch(expected[1].get("/field")->GetString(),
                                  "([^ @]+)@([^ @]+)"));
}

TEST(opBuilderHelperRegexMatch, NestedFieldRegexMatch)
{
    Document doc{R"~~({
        "map":
            {"test/field": "+r_match/exp"}
    })~~"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            s.on_next(Event{R"~~({
            "test":
                {"field": "exp"}
            })~~"});
            s.on_next(Event{R"~~({
            "test":
                {"field": "this is a test exp"}
            })~~"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/map"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 2);
    ASSERT_TRUE(RE2::PartialMatch(expected[0].get("/test/field")->GetString(), "exp"));
    ASSERT_TRUE(RE2::PartialMatch(expected[1].get("/test/field")->GetString(), "exp"));
}

TEST(opBuilderHelperRegexMatch, FieldNotExistsRegexMatch)
{
    Document doc{R"({
        "check":
            {"field2": "+r_match/exp"}
    })"};

    Observable input = observable<>::create<Event>(
        [=](auto s)
        {
            s.on_next(Event{R"(
                {"field2":"exp"}
            )"});
            s.on_next(Event{R"(
                {"field":"exp"}
            )"});
            s.on_completed();
        });

    Lifter lift = opBuilderHelperRegexMatch(*doc.get("/check"));
    Observable output = lift(input);
    vector<Event> expected;
    output.subscribe([&](Event e) { expected.push_back(e); });

    ASSERT_EQ(expected.size(), 1);
    ASSERT_TRUE(expected[0].exists("/field2"));
}