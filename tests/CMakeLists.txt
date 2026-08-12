# Tests subtree entry point. Explicit add_subdirectory calls only — no
# globbing that could silently pick up a sibling before its CMakeLists.txt
# exists.
add_subdirectory(unit)
add_subdirectory(integration)
