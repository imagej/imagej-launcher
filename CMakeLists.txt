cmake_minimum_required(VERSION 3.2.0)

# Default policy is from 3.4.0
cmake_policy(VERSION 3.4.0)

# Set project metadata
project("imagej-launcher"
  LANGUAGES C)

# Get version
execute_process(
	COMMAND mvn help:evaluate -Dexpression=project.version -q -DforceStdout
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get hash of current commit
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Add JNI include directory
find_package(JNI REQUIRED)
if (JNI_FOUND)
	message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
	message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
endif()
include_directories(${JNI_INCLUDE_DIRS})

# Find Frameworks on macOS
if (APPLE)
	find_library(CORE_FOUNDATION CoreFoundation)
	if (NOT CORE_FOUNDATION)
		message(FATAL_ERROR "AudioUnit not found")
	endif()

	find_library(APPLICATION_SERVICES ApplicationServices)
	if (NOT APPLICATION_SERVICES)
		message(FATAL_ERROR "ApplicationServices not found")
	endif()
endif (APPLE)

# Define executable name
IF (NOT EXECUTABLE_NAME)
	IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		# macOS
		SET(EXECUTABLE_NAME "ImageJ-macosx")
	ELSEIF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		# Windows (64-bit)
		SET(EXECUTABLE_NAME "ImageJ-win64")

		IF (${TARGET_ARCHITECTURE} MATCHES "x86")
			SET(EXECUTABLE_NAME "ImageJ-win32")
		ENDIF()
	ELSEIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		# Linux (64-bit)
		SET(EXECUTABLE_NAME "ImageJ-linux64")

		IF (${TARGET_ARCHITECTURE} MATCHES "x86")
			SET(EXECUTABLE_NAME "ImageJ-linux32")
		ENDIF()
	ENDIF()
ENDIF()

# Define libdl name
IF (NOT LIB_DL)
	IF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		# Windows
		SET(LIB_DL "kernel32")
	ELSEIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		# Linux
		SET(LIB_DL "dl")
	ENDIF()
ENDIF()

# Define libpthread name
IF (NOT LIB_PTHREAD)
	IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		# Linux
		SET(LIB_PTHREAD "pthread")
	ENDIF()
ENDIF()

# Check target architecture
IF (${TARGET_ARCHITECTURE} MATCHES "x86")
	SET(ARCHITECTURE "-m32")
ENDIF ()

# Add projects include directory
include_directories("src/main/include")

# Define macros used in the ImageJ.c#usage()
add_definitions(-DLAUNCHER_VERSION="${VERSION}")
add_definitions(-DBUILD_NUMBER="${GIT_COMMIT_HASH}")

# Define sources files
set(SOURCE_FOLDER "src/main/c")
set(SOURCE_FILES ${SOURCE_FOLDER}/ImageJ.c ${SOURCE_FOLDER}/common.c ${SOURCE_FOLDER}/config.c ${SOURCE_FOLDER}/exe-ico.c ${SOURCE_FOLDER}/file-funcs.c ${SOURCE_FOLDER}/java.c ${SOURCE_FOLDER}/platform.c ${SOURCE_FOLDER}/string-funcs.c ${SOURCE_FOLDER}/xalloc.c)

# Set compiler flags and compile
if (APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.6")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# Windows (64-bit)
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__int64=int64_t")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	# Linux
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-stack-protector")
endif (APPLE)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCHITECTURE} ${DEBUG_OPTION}")
add_executable (${EXECUTABLE_NAME} WIN32 ${SOURCE_FILES})

# Set linker flags and compile
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARCHITECTURE}")
target_link_libraries(${EXECUTABLE_NAME} ${LIB_DL} ${LIB_PTHREAD} ${CORE_FOUNDATION} ${APPLICATION_SERVICES})
