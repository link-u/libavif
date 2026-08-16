# - Try to find dav2d
# Once done this will define
#
#  DAV2D_FOUND - system has dav2d
#  DAV2D_INCLUDE_DIR - the dav2d include directory
#  DAV2D_LIBRARIES - Link these to use dav2d
#
#=============================================================================
#  Copyright (c) 2020 Andreas Schneider <asn@cryptomilk.org>
#
#  Distributed under the OSI-approved BSD License (the "License");
#  see accompanying file Copyright.txt for details.
#
#  This software is distributed WITHOUT ANY WARRANTY; without even the
#  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the License for more information.
#=============================================================================
#

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(_DAV2D dav2d)
endif(PKG_CONFIG_FOUND)

find_path(DAV2D_INCLUDE_DIR NAMES dav2d/dav2d.h PATHS ${_DAV2D_INCLUDEDIR})

find_library(DAV2D_LIBRARY NAMES dav2d PATHS ${_DAV2D_LIBDIR})

if(DAV2D_LIBRARY)
    set(DAV2D_LIBRARIES ${DAV2D_LIBRARIES} ${DAV2D_LIBRARY})
endif(DAV2D_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dav2d REQUIRED_VARS DAV2D_LIBRARY DAV2D_LIBRARIES DAV2D_INCLUDE_DIR VERSION_VAR _DAV2D_VERSION)

mark_as_advanced(DAV2D_INCLUDE_DIR DAV2D_LIBRARY DAV2D_LIBRARIES)

if(DAV2D_LIBRARY)
    if("${DAV2D_LIBRARY}" MATCHES "\\.a$")
        add_library(dav2d::dav2d STATIC IMPORTED GLOBAL)
    else()
        add_library(dav2d::dav2d SHARED IMPORTED GLOBAL)
    endif()
    set_target_properties(
        dav2d::dav2d PROPERTIES IMPORTED_LOCATION "${DAV2D_LIBRARY}" IMPORTED_IMPLIB "${DAV2D_LIBRARY}" IMPORTED_SONAME dav2d
    )
    target_include_directories(dav2d::dav2d INTERFACE ${DAV2D_INCLUDE_DIR})
endif()
