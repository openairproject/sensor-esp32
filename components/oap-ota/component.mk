#
# Main component makefile.
#
# This Makefile can be left empty. By default, it will take the sources in the 
# src/ directory, compile them and link them into lib(subdirectory_name).a 
# in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#

# GitHub (https://www.digicert.com/CACerts/DigiCertHighAssuranceEVRootCA.crt)
#COMPONENT_EMBED_TXTFILES = digicert_ca.pem

# openairproject.com (CloudFront)
#COMPONENT_EMBED_TXTFILES = comodo_ca.pem

# stb.42u.de
COMPONENT_EMBED_TXTFILES = lets-encrypt-x3-cross-signed.pem
