#
# Main component makefile.
#
# This Makefile can be left empty. By default, it will take the sources in the 
# src/ directory, compile them and link them into lib(subdirectory_name).a 
# in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#

# TODO split into proper components
#COMPONENT_PRIV_INCLUDEDIRS := .
#COMPONENT_SRCDIRS := . bootwifi mongoose hardware net

CFLAGS += -DCS_PLATFORM=CS_P_ESP32 \
	-DMG_DISABLE_DIRECTORY_LISTING=1 \
	-DMG_DISABLE_DAV=1 \
	-DMG_DISABLE_CGI=1 \
	-DMG_DISABLE_FILESYSTEM=1 \
	-DMG_LWIP=1 \
	-DMG_ENABLE_BROADCAST