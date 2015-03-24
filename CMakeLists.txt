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
set_source_files_properties(${SOURCE_FILES} PROPERTIES COMPILE_FLAGS "/ZW /AI\$(WindowsSDK_MetadataPath)")
set(LIBS ${MEDIASTREAMER2_LIBRARIES} ws2_32)

if(ENABLE_STATIC)
	add_library(mswp8vid STATIC ${SOURCE_FILES})
else()
	add_library(mswp8vid SHARED ${SOURCE_FILES})
	set_target_properties(mswp8vid PROPERTIES VERSION 0)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Debug/libmswp8vid.pdb
			DESTINATION bin
			PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		)
	endif()
	set_target_properties(mswp8vid PROPERTIES PREFIX "lib")
endif()
target_link_libraries(mswp8vid ${LIBS})

install(TARGETS mswp8vid
	RUNTIME DESTINATION lib/mediastreamer/plugins
	LIBRARY DESTINATION lib/mediastreamer/plugins
	ARCHIVE DESTINATION lib/mediastreamer/plugins
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
