############################################################################
# CMakeLists.txt
# Copyright (C) 2016  Belledonne Communications, Grenoble France
#
############################################################################
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
############################################################################

cmake_minimum_required(VERSION 3.0)
project(MSWINRTVID CXX)


if(LINPHONE_BUILDER_GROUP_EXTERNAL_SOURCE_PATH_BUILDERS)
	include("${EP_ms2_CONFIG_DIR}/Mediastreamer2Config.cmake")
else()
	find_package(Mediastreamer2 REQUIRED)
endif()


include_directories(
	${MEDIASTREAMER2_INCLUDE_DIRS}
)

set(SOURCE_FILES
	"IVideoDispatcher.h"
	"IVideoRenderer.h"
	"LinkList.h"
	"mswinrtcap.cpp"
	"mswinrtcap.h"
	"mswinrtdis.cpp"
	"mswinrtdis.h"
	"mswinrtmediasink.cpp"
	"mswinrtmediasink.h"
	"mswinrtvid.cpp"
	"mswinrtvid.h"
	"VideoBuffer.h"
)
set(LIBS ${MEDIASTREAMER2_LIBRARIES} mfplat.lib;mfuuid.lib)

add_library(mswinrtvid MODULE ${SOURCE_FILES})
set_target_properties(mswinrtvid PROPERTIES VERSION 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Debug/libmswinrtvid.pdb
		DESTINATION lib/mediastreamer/plugins
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
	)
endif()
set_target_properties(mswinrtvid PROPERTIES PREFIX "lib")
#set_target_properties(mswinrtvid PROPERTIES LINK_FLAGS "/WINMDFILE:Mediastreamer2.WP8Video.winmd /OUT:${CMAKE_BUILD_TYPE}\\\$(TargetName)\$(TargetExt)")
set_target_properties(mswinrtvid PROPERTIES VS_WINRT_COMPONENT TRUE)
target_link_libraries(mswinrtvid ${LIBS})

install(TARGETS mswinrtvid
	RUNTIME DESTINATION lib/mediastreamer/plugins
	LIBRARY DESTINATION lib/mediastreamer/plugins
	ARCHIVE DESTINATION lib/mediastreamer/plugins
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

#install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Mediastreamer2.WP8Video.winmd" DESTINATION lib/mediastreamer/plugins)

#install(FILES "VideoRenderer.cs" "VideoStreamSource.cs" DESTINATION src/mswinrtvid)
