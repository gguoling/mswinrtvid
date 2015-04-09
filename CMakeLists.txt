############################################################################
# CMakeLists.txt
# Copyright (C) 2015  Belledonne Communications, Grenoble France
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
project(MSWP8VID CXX)


find_package(Mediastreamer2 REQUIRED)


include_directories(
	${MEDIASTREAMER2_INCLUDE_DIRS}
)

file(GLOB SOURCE_FILES "*.h" "*.cpp")
set(SOURCE_FILES
	"IVideoDispatcher.h"
	"IVideoRenderer.h"
	"mswp8cap.cpp"
	"mswp8cap.h"
	"mswp8dis.cpp"
	"mswp8dis.h"
	"mswp8vid.cpp"
	"VideoBuffer.h"
)
set(LIBS ${MEDIASTREAMER2_LIBRARIES} ws2_32)

add_library(mswp8vid MODULE ${SOURCE_FILES})
set_target_properties(mswp8vid PROPERTIES VERSION 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Debug/libmswp8vid.pdb
		DESTINATION bin
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
	)
endif()
set_target_properties(mswp8vid PROPERTIES PREFIX "lib")
target_compile_definitions(mswp8vid PRIVATE
	"-DWIN32_LEAN_AND_MEAN"
	"-D_CRT_SECURE_NO_WARNINGS"
)
set_target_properties(mswp8vid PROPERTIES LINK_FLAGS "/WINMDFILE:Mediastreamer2.WP8Video.winmd /OUT:${CMAKE_BUILD_TYPE}\\\$(TargetName)\$(TargetExt)")
set_target_properties(mswp8vid PROPERTIES VS_WINRT_COMPONENT TRUE)
target_link_libraries(mswp8vid ${LIBS})

install(TARGETS mswp8vid
	RUNTIME DESTINATION lib/mediastreamer/plugins
	LIBRARY DESTINATION lib/mediastreamer/plugins
	ARCHIVE DESTINATION lib/mediastreamer/plugins
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Mediastreamer2.WP8Video.winmd" DESTINATION lib/mediastreamer/plugins)

install(FILES "VideoRenderer.cs" "VideoStreamSource.cs" DESTINATION src/mswp8vid)
