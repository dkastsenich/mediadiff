# Tests subtree entry point. Explicit add_subdirectory calls only — no
# globbing that could silently pick up a sibling before its CMakeLists.txt
# exists. Plan 02 adds add_subdirectory(integration).
add_subdirectory(unit)
