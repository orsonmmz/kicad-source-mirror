#
#  This program source code file is part of KICAD, a free EDA CAD application.
#
#  Copyright (C) 2015 Wayne Stambaugh <stambaughw@verizon.net>
#  Copyright (C) 2015-2016 KiCad Developers, see AUTHORS.txt for contributors.
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, you may find one here:
#  http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
#  or you may search the http://www.gnu.org website for the version 2 license,
#  or you may write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#

# Automagically create version header file if the version string was
# not defined during the build configuration.  If
# CreateGitVersionHeader cannot determine the current repo version, a
# version.h file is still created with KICAD_BUILD_VERSION set to
# "no-vcs-found".
include( ${CMAKE_MODULE_PATH}/KiCadVersion.cmake )

# Attempt to detect if we have a git repo and set the version string if
# the version wasn't set to something other than the default value in
# KiCadVersion.cmake.
if( _wvh_version_str STREQUAL "no-vcs-found" AND EXISTS "${SRC_PATH}/.git" )
    message( STATUS "Using Git to determine build version string." )
    include( ${CMAKE_MODULE_PATH}/CreateGitVersionHeader.cmake )
    create_git_version_header( ${SRC_PATH} )
    set( _wvh_version_str ${KICAD_BUILD_VERSION} )
endif()

# If KICAD_BRANCH_NAME is empty, set KICAD_FULL_VERSION to just the build
# version rather than the concatenation of the build version and the branch
# name.
if( KICAD_BRANCH_NAME )
    set( KICAD_FULL_VERSION "${_wvh_version_str}-${KICAD_BRANCH_NAME}" )
else()
    set( KICAD_FULL_VERSION "${_wvh_version_str}" )
endif()

set( _wvh_new_version_text
"/* Do not modify this file, it was automatically generated by CMake. */

/*
 * Define the KiCad build version string.
 */
#ifndef __KICAD_VERSION_H__
#define __KICAD_VERSION_H__

#define KICAD_BUILD_VERSION \"${_wvh_version_str}\"
#define KICAD_BRANCH_NAME \"${KICAD_BRANCH_NAME}\"
#define KICAD_FULL_VERSION \"${KICAD_FULL_VERSION}\"

#endif  /* __KICAD_VERSION_H__ */
" )

set( _wvh_write_version_file ON )

# Only write the header if it has changed, to avoid rebuilds
if( EXISTS ${OUTPUT_FILE} )
    file( READ ${OUTPUT_FILE} _wvh_old_version_text )
    if( _wvh_old_version_text STREQUAL _wvh_new_version_text )
        message( STATUS "Not updating ${OUTPUT_FILE}" )
        set( _wvh_write_version_file OFF )
    endif()
endif()

if( _wvh_write_version_file )
    message( STATUS "Writing ${OUTPUT_FILE} file with version: ${_wvh_version_str}" )

    file( WRITE ${OUTPUT_FILE} ${_wvh_new_version_text} )

endif()

# There should always be a valid version.h file.  Otherwise, the build will fail.
if( NOT EXISTS ${OUTPUT_FILE} )
    message( FATAL_ERROR "Configuration failed to write file ${OUTPUT_FILE}." )
endif()
