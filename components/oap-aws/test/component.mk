#
#Component Makefile
#

COMPONENT_ADD_LDFLAGS = -Wl,--whole-archive -l$(COMPONENT_NAME) -Wl,--no-whole-archive

COMPONENT_EMBED_TXTFILES := 1cbf751210-certificate.pem.crt
COMPONENT_EMBED_TXTFILES += 1cbf751210-private.pem.key