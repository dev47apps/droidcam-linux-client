cmake_minimum_required(VERSION 3.15)

project(droidcam)
set(COMMON_SOURCE src/connection.c src/decoder.c)

include(FindPkgConfig)

#find_package(GTK2 2.6 REQUIRED gtk)
find_path(SWSCALE_INCLUDE_DIRS libswscale/swscale.h)
find_library(SWSCALE_LIBRARY swscale)
find_library(M m)

pkg_check_modules(JPEGTURBO REQUIRED libturbojpeg)
pkg_check_modules(JPEG REQUIRED libjpeg)
pkg_check_modules(GTK2 REQUIRED gtk+-2.0)
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)


add_executable(droidcam ${COMMON_SOURCE} src/droidcam.c)
add_executable(droidcam-cli ${COMMON_SOURCE} src/droidcam-cli.c)

include_directories(${SWSCALE_INCLUDE_DIRS})
include_directories(${JPEG_INCLUDE_DIRS})

target_include_directories(droidcam PUBLIC "${GTK2_INCLUDE_DIRS}")
#target_include_directories(droidcam PUBLIC "${GTK_INCLUDE_DIRS}")
target_link_libraries(droidcam ${GTK2_LDFLAGS})
target_link_libraries(droidcam m swscale ${JPEG_LDFLAGS} ${JPEGTURBO_LDFLAGS} ${GTHREAD2_LDFLAGS})
#target_link_libraries(droidcam m swscale libturbojpeg.a ${GTHREAD2_LDFLAGS})
target_link_libraries(droidcam-cli m swscale ${JPEG_LDFLAGS} ${JPEGTURBO_LDFLAGS} ${GTHREAD2_LDFLAGS})
