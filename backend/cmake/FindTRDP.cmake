# FindTRDP.cmake - Locate a pre-built TRDP stack installation
#
# This module searches for a TRDP installation that follows the standard
# prefix/include/lib layout described in backend/third_party/README.md.
# It exposes the imported targets:
#   * TRDP::trdp    – core TRDP stack
#   * TRDP::trdpap  – (optional) Application Profile helper library
#
# The caller can hint the installation path via the TRDP_ROOT cache
# variable or the TRDP_ROOT environment variable.

set(TRDP_ROOT "" CACHE PATH "Root directory of the TRDP installation")

set(_TRDP_HINTS
    ${TRDP_ROOT}
    $ENV{TRDP_ROOT}
    /usr/local/trdp
    /opt/trdp
)

find_path(TRDP_INCLUDE_DIR
    NAMES
        trdp_if.h
        trdp_if_light.h
        tau_dnr.h
    HINTS ${_TRDP_HINTS}
    PATH_SUFFIXES include include/trdp
)

find_library(TRDP_LIBRARY NAMES trdp libtrdp
    HINTS ${_TRDP_HINTS}
    PATH_SUFFIXES lib
)

find_library(TRDPAP_LIBRARY NAMES trdpap libtrdpap
    HINTS ${_TRDP_HINTS}
    PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TRDP
    REQUIRED_VARS TRDP_INCLUDE_DIR TRDP_LIBRARY
)

if(TRDP_FOUND)

    # Core TRDP library target
    if (NOT TARGET TRDP::trdp)
        add_library(TRDP::trdp STATIC IMPORTED)
        set_target_properties(TRDP::trdp PROPERTIES
            IMPORTED_LOCATION "${TRDP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${TRDP_INCLUDE_DIR}"
        )
    endif()

    # Optional AP helper library target
    if (TRDPAP_LIBRARY AND NOT TARGET TRDP::trdpap)
        add_library(TRDP::trdpap STATIC IMPORTED)
        set_target_properties(TRDP::trdpap PROPERTIES
            IMPORTED_LOCATION "${TRDPAP_LIBRARY}"
            INTERFACE_LINK_LIBRARIES TRDP::trdp
            INTERFACE_INCLUDE_DIRECTORIES "${TRDP_INCLUDE_DIR}"
        )
    endif()

endif()
