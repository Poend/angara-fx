# Godot reads this manifest directly when enabling the plugin.
set(ANGARA_FX_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../../addons/angara_fx/plugin.cfg")
file(STRINGS "${ANGARA_FX_MANIFEST}" version_lines REGEX "^[ \t]*version[ \t]*=")
list(LENGTH version_lines version_count)
set(number "(0|[1-9][0-9]*)")
if(NOT version_count EQUAL 1 OR NOT version_lines MATCHES
    "^[ \t]*version[ \t]*=[ \t]*\"(${number}[.]${number}[.]${number})\"[ \t]*$")
  message(FATAL_ERROR "Expected one quoted MAJOR.MINOR.PATCH version in ${ANGARA_FX_MANIFEST}")
endif()
set(ANGARA_FX_VERSION "${CMAKE_MATCH_1}")

# Script mode checks a release before the maintainer creates its Git tag.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT DEFINED RELEASE_TAG OR NOT RELEASE_TAG STREQUAL "v${ANGARA_FX_VERSION}")
    message(FATAL_ERROR "Use -DRELEASE_TAG=v${ANGARA_FX_VERSION} to match the plugin version")
  endif()
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../CHANGELOG.md" release_headings
       REGEX "^## [0-9]+[.][0-9]+[.][0-9]+$")
  if(NOT release_headings)
    message(FATAL_ERROR "CHANGELOG.md needs a versioned release entry")
  endif()
  list(GET release_headings 0 latest_heading)
  if(NOT latest_heading STREQUAL "## ${ANGARA_FX_VERSION}")
    message(FATAL_ERROR "The latest CHANGELOG.md entry must be for ${ANGARA_FX_VERSION}")
  endif()
  message(STATUS "Release tag, Godot manifest and changelog agree on ${RELEASE_TAG}")
endif()
