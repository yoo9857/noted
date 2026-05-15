#include <gtest/gtest.h>

#include "noted/engine/error/error.hpp"

TEST(Error, FormatIncludesCodeAndLocation) {
    auto e = noted::make_error(noted::ErrorCode::file_not_found, "missing.png");
    const auto s = e.format();
    EXPECT_NE(s.find("file_not_found"), std::string::npos);
    EXPECT_NE(s.find("missing.png"), std::string::npos);
}

TEST(Error, ChainPreservesCause) {
    auto inner = noted::make_error(noted::ErrorCode::io_failure, "read short");
    auto outer = noted::chain_error(
        noted::make_error(noted::ErrorCode::document_corrupt, "header mismatch"),
        std::move(inner));
    const auto s = outer.format();
    EXPECT_NE(s.find("document_corrupt"), std::string::npos);
    EXPECT_NE(s.find("caused by"), std::string::npos);
    EXPECT_NE(s.find("io_failure"), std::string::npos);
}

TEST(Result, UnexpectedCarriesError) {
    auto fail = []() -> noted::Result<int> {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument, "negative size"));
    };
    auto r = fail();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, noted::ErrorCode::invalid_argument);
}
