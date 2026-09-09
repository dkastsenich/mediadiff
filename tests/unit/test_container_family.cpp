// CONT-02 (03-06-PLAN.md Task 3, behavior 1): container_family_token's own
// mapping table, exercised directly (no probe layer, no libav) -- the
// pure function both src/probe/demux_session.cpp's ContainerFamily
// derivation and src/compare/engine.cpp's cross-container demotion call.

#include <catch2/catch_test_macros.hpp>

#include "core/container_family.h"

using mediadiff::container_family_token;

TEST_CASE("container_family_token - maps the raw, untruncated AVInputFormat::name for mp4/mkv/ts", "[unit]") {
  REQUIRE(container_family_token("mov,mp4,m4a,3gp,3g2,mj2") == "mp4");
  REQUIRE(container_family_token("matroska,webm") == "mkv");
  REQUIRE(container_family_token("mpegts") == "ts");
}

TEST_CASE("container_family_token - also accepts an already-truncated first token", "[unit]") {
  REQUIRE(container_family_token("mov") == "mp4");
  REQUIRE(container_family_token("matroska") == "mkv");
  REQUIRE(container_family_token("mpegts") == "ts");
}

TEST_CASE("container_family_token - an unrecognized format name returns an empty token", "[unit]") {
  REQUIRE(container_family_token("avi").empty());
  REQUIRE(container_family_token("wav").empty());
  REQUIRE(container_family_token("").empty());
  REQUIRE(container_family_token("something,else").empty());
}
