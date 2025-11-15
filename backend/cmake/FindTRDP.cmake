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

# If the headers live under an include/trdp/ directory (the default layout
# produced by backend/third_party/TRDP_INSTALL_LINUX.md), expose the parent
# include/ directory to consumers so they can include files via "trdp/*.h".
set(_TRDP_INTERFACE_INCLUDE_DIR "${TRDP_INCLUDE_DIR}")
if(_TRDP_INTERFACE_INCLUDE_DIR)
    get_filename_component(_TRDP_DIR_NAME "${_TRDP_INTERFACE_INCLUDE_DIR}" NAME)
    if(_TRDP_DIR_NAME STREQUAL "trdp")
        get_filename_component(_TRDP_INTERFACE_INCLUDE_DIR "${_TRDP_INTERFACE_INCLUDE_DIR}" DIRECTORY)
    endif()
endif()

find_library(TRDP_LIBRARY NAMES trdp libtrdp
    HINTS ${_TRDP_HINTS}
    PATH_SUFFIXES lib
)

find_library(TRDPAP_LIBRARY NAMES trdpap libtrdpap
    HINTS ${_TRDP_HINTS}
    PATH_SUFFIXES lib
)

find_package(Threads QUIET)
find_library(TRDP_UUID_LIBRARY NAMES uuid)
find_library(TRDP_RT_LIBRARY NAMES rt)

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
            INTERFACE_INCLUDE_DIRECTORIES "${_TRDP_INTERFACE_INCLUDE_DIR}"
        )
        set(_TRDP_EXTRA_LIBS)
        if(Threads_FOUND)
            list(APPEND _TRDP_EXTRA_LIBS Threads::Threads)
        endif()
        if(TRDP_UUID_LIBRARY)
            list(APPEND _TRDP_EXTRA_LIBS "${TRDP_UUID_LIBRARY}")
        endif()
        if(TRDP_RT_LIBRARY)
            list(APPEND _TRDP_EXTRA_LIBS "${TRDP_RT_LIBRARY}")
        endif()
        if(_TRDP_EXTRA_LIBS)
            set_property(TARGET TRDP::trdp APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${_TRDP_EXTRA_LIBS})
        endif()
    endif()

    # Optional AP helper library target
    if (TRDPAP_LIBRARY AND NOT TARGET TRDP::trdpap)
        add_library(TRDP::trdpap STATIC IMPORTED)
        set_target_properties(TRDP::trdpap PROPERTIES
            IMPORTED_LOCATION "${TRDPAP_LIBRARY}"
            INTERFACE_LINK_LIBRARIES TRDP::trdp
            INTERFACE_INCLUDE_DIRECTORIES "${_TRDP_INTERFACE_INCLUDE_DIR}"
        )
    endif()

endif()
