#
# Main component makefile.
#
# This Makefile can be left empty. By default, it will take the sources in the 
# src/ directory, compile them and link them into lib(subdirectory_name).a 
# in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#
CFLAGS+= -DLV_CONF_INCLUDE_SIMPLE

# Embed the compressed web UI (run compress_web.py first when using the make build)
COMPONENT_EMBED_FILES := www/index.html.gz