#all : main
#
#main : main.c
#	gcc main.c -static -I../pocketsphinx/include -I../sphinxbase/include \
#		-L../pocketsphinx/src/libpocketsphinx/.libs -lpocketsphinx \
#		-L../sphinxbase/src/libsphinxbase/.libs -lsphinxbase -lm -o main
#

CONFIG := Debug
VALID_TOOLCHAINS :=  glibc pnacl newlib

# TODO: make portable
NACL_SDK_ROOT = /home/mbait/src-local/dist/nacl_sdk/pepper_31
include $(NACL_SDK_ROOT)/tools/common.mk

SPHINXBASE_ROOT = ../sphinxbase/src/libsphinxbase
CFLAGS = -DHAVE_CONFIG_H -I../sphinxbase/include

SOURCES := ../sphinxbase/src/libsphinxad/cont_ad_base.c
$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,sphinxad,$(SOURCES)))

SPHINXUTIL_SOURCES := \
  bio.c \
  bitvec.c \
  case.c \
  ckd_alloc.c \
  cmd_ln.c \
  dtoa.c \
  err.c \
  errno.c \
  f2c_lite.c \
  filename.c \
  genrand.c \
  glist.c \
  hash_table.c \
  heap.c \
  huff_code.c \
  info.c \
  listelem_alloc.c \
  logmath.c \
  matrix.c \
  mmio.c \
  pio.c \
  profile.c \
  sbthread.c \
  strfuncs.c \
  utf8.c
SOURCES := $(addprefix $(SPHINXBASE_ROOT)/util/,$(SPHINXUTIL_SOURCES))

$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,sphinxutil,$(SOURCES)))

SPHINXFE_SOURCES := \
  fe_noise.c \
  fe_interface.c \
  fe_sigproc.c \
  fe_warp_affine.c \
  fe_warp.c \
  fe_warp_inverse_linear.c \
  fe_warp_piecewise_linear.c \
  fixlog.c
SOURCES := $(addprefix $(SPHINXBASE_ROOT)/fe/,$(SPHINXFE_SOURCES))

$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,sphinxfe,$(SOURCES)))

SPHINXLM_SOURCES := \
  fsg_model.c \
  jsgf.c \
  jsgf_parser.c \
  jsgf_scanner.c \
  lm3g_model.c \
  ngram_model_arpa.c \
  ngram_model_dmp.c \
  ngram_model_set.c \
  ngram_model.c
SOURCES := $(addprefix $(SPHINXBASE_ROOT)/lm/,$(SPHINXLM_SOURCES))
$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,sphinxlm,$(SOURCES)))

SPHINXFEAT_SOURCES := \
  agc.c \
  cmn.c \
  cmn_prior.c \
  feat.c \
  lda.c
SOURCES := $(addprefix $(SPHINXBASE_ROOT)/feat/,$(SPHINXFEAT_SOURCES))
$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,sphinxfeat,$(SOURCES)))

CFLAGS = -I../sphinxbase/include -I../pocketsphinx/include
POCKETSPHINX_SOURCES := \
  acmod.c     \
  bin_mdef.c    \
  blkarray_list.c   \
  dict.c     \
  dict2pid.c    \
  fsg_history.c   \
  fsg_lextree.c   \
  fsg_search.c   \
  hmm.c     \
  mdef.c     \
  ms_gauden.c    \
  ms_mgau.c    \
  ms_senone.c    \
  ngram_search.c   \
  ngram_search_fwdtree.c \
  ngram_search_fwdflat.c \
  phone_loop_search.c  \
  pocketsphinx.c \
  ps_lattice.c   \
  ps_mllr.c    \
  ptm_mgau.c    \
  s2_semi_mgau.c   \
  tmat.c     \
  vector.c
SOURCES := $(addprefix ../pocketsphinx/src/libpocketsphinx/,$(POCKETSPHINX_SOURCES))
$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))
$(eval $(call LIB_RULE,pocketsphinx,$(SOURCES)))

TARGET = main
SOURCES = main.c
DEPS = ppapi_simple nacl_io
PS_DEPS = pocketsphinx sphinxlm sphinxfeat sphinxfe sphinxutil sphinxad
LIBS = $(DEPS) ppapi_cpp ppapi pthread $(PS_DEPS)

$(foreach dep,$(DEPS),$(eval $(call DEPEND_RULE,$(dep))))
$(foreach src,$(SOURCES),$(eval $(call COMPILE_RULE,$(src),$(CFLAGS))))

ifeq ($(CONFIG),Release)
$(eval $(call LINK_RULE,$(TARGET)_unstripped,$(SOURCES),$(LIBS),$(DEPS) $(PS_DEPS)))
$(eval $(call STRIP_RULE,$(TARGET),$(TARGET)_unstripped))
else
$(eval $(call LINK_RULE,$(TARGET),$(SOURCES),$(LIBS),$(DEPS) $(PS_DEPS)))
endif

$(eval $(call NMF_RULE,$(TARGET),))
#$(eval $(call HTML_RULE,$(TARGET)))
