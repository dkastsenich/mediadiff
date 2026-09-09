#pragma once

// CONT-02 (doc 02 section 2, 03-06-PLAN.md Task 3): the single mapping from
// a libav-reported container format name to the family TOKEN that appears
// as the SECOND dotted segment of every scoped check id
// (`container.mp4.*`, `container.mkv.*`, `container.ts.*`). Lives in
// core/ -- never probe/ -- precisely so compare/engine.cpp's cross-
// container demotion can call it directly with no dependency on the probe
// layer or on any libav header; src/probe/demux_session.cpp's own
// container_family_from_format_name (the probe layer's ContainerFamily
// derivation) calls this SAME function, so the two can never disagree
// about what family a file belongs to (this plan's own key_link).

#include <string_view>

namespace mediadiff {

// Maps a libav-reported format name -- either the full comma-delimited
// AVInputFormat::name ("mov,mp4,m4a,3gp,3g2,mj2", "matroska,webm") or an
// already-truncated first token ("mov", "matroska") -- to the family token
// a scoped check id's own second dotted segment uses: "mp4", "mkv", "ts",
// or an EMPTY string_view for any format this project does not (yet)
// special-case. The empty-string return is deliberate, not a failure
// mode: a check id scoped to an unrecognized family is simply never
// demoted by the cross-container mechanism (compare/engine.cpp), because
// it has no counterpart namespace to collide with.
std::string_view container_family_token(std::string_view format_name);

}  // namespace mediadiff
