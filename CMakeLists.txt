cmake_minimum_required(VERSION 2.8)
INCLUDE (CheckIncludeFiles)
INCLUDE(FindPkgConfig)

PROJECT(usteerd C)

IF("${CMAKE_SYSTEM_NAME}" MATCHES "Linux" AND NOT NL_CFLAGS)
  FIND_PROGRAM(PKG_CONFIG pkg-config)
  IF(PKG_CONFIG)
    EXECUTE_PROCESS(
		COMMAND pkg-config --silence-errors --cflags libnl-tiny
		OUTPUT_VARIABLE NL_CFLAGS
		OUTPUT_STRIP_TRAILING_WHITESPACE)
    EXECUTE_PROCESS(
		COMMAND pkg-config --silence-errors --libs libnl-tiny
		OUTPUT_VARIABLE NL_LIBS
		OUTPUT_STRIP_TRAILING_WHITESPACE)
  ENDIF()
ENDIF()

CHECK_INCLUDE_FILES(pcap/pcap.h HAVE_PCAP_H)
IF(NOT HAVE_PCAP_H)
	UNSET(HAVE_PCAP_H CACHE)
	MESSAGE(FATAL_ERROR "pcap/pcap.h is not found")
ENDIF()

SET(SOURCES main.c local_node.c node.c sta.c policy.c ubus.c remote.c parse.c netifd.c timeout.c event.c neighbor_report.c element.c measurement.c rrm.c candidate.c scan.c)

IF(NL_CFLAGS)
	ADD_DEFINITIONS(${NL_CFLAGS})
	SET(SOURCES ${SOURCES} nl80211.c)
ENDIF()

ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations)

FIND_LIBRARY(libjson NAMES json-c json)
ADD_EXECUTABLE(usteerd ${SOURCES})
ADD_EXECUTABLE(fakeap fakeap.c timeout.c)

TARGET_LINK_LIBRARIES(usteerd ubox ubus blobmsg_json
			${LIBS_EXTRA} ${libjson} ${NL_LIBS})
TARGET_LINK_LIBRARIES(fakeap ubox ubus)

ADD_EXECUTABLE(ap-monitor monitor.c parse.c)
TARGET_LINK_LIBRARIES(ap-monitor ubox pcap blobmsg_json)

SET(CMAKE_INSTALL_PREFIX /usr)

INSTALL(TARGETS usteerd
	RUNTIME DESTINATION sbin
)
