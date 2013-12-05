#include <stdlib.h>

#include <sphinxbase/cont_ad.h>
#include <sphinxbase/err.h>
#include <pocketsphinx.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_var_array_buffer.h"
#include "ppapi/c/ppp.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_messaging.h"
#include "nacl_io/nacl_io.h"

static ps_decoder_t *ps;
static FILE *stream;

static int32
cont_read(ad_rec_t *ad_rec, int16 * buf, int32 max)
{
  size_t nread = fread(buf, sizeof *buf, max, stream);
  return nread > 0 ? nread : -1;
}

static int
pocketsphinx_init(void)
{
  cmd_ln_t *config = cmd_ln_init(NULL, ps_args(), FALSE, NULL);
  cmd_ln_set_str_r(config, "-hmm",  "/model/hmm/en_US/hub4wsj_sc_8k");
  cmd_ln_set_str_r(config, "-dict", "/model/lm/en_US/cmu07a.dic");
  cmd_ln_set_str_r(config, "-lm",   "/model/lm/en_US/hub4.5000.DMP");
  ps = ps_init(config);

  if (NULL == (stream = fopen("/model/goforward.raw", "r"))) {
    perror("failed to open audio file");
    return 1;
  }

  cont_ad_t *cont;
  ad_rec_t file_ad = { 0 };
  file_ad.sps = (int32) cmd_ln_float32_r(config, "-samprate");
  file_ad.bps = sizeof(int16);

  if (NULL == (cont = cont_ad_init_rawmode(&file_ad, cont_read))) {
    E_FATAL("failed to initialize voice activity detection\n");
    return 1;
  }

  if (cont_ad_calib(cont) < 0)
    E_INFO("using default voice activity detection\n");
  rewind(stream);

  int32 start = -1;
  int16 adbuf[4096];
  int32 endsil = (int32) (0.7 * file_ad.sps);       /* 0.7s for utterance end */
  const char *uttid;
  for (;;) {
    int32 k = cont_ad_read(cont, adbuf, sizeof adbuf / sizeof *adbuf);
    if (k < 0) {            /* End of input audio file; end the utt and exit */
      if (start > 0) {
        ps_end_utt(ps);
        printf("%s\n", ps_get_hyp(ps, NULL, &uttid));
      }

      break;
    }

    if (cont->state == CONT_AD_STATE_SIL) { /* Silence data got */
      if (start >= 0) {   /* Currently in an utterance */
        if (cont->seglen > endsil) {    /* Long enough silence detected; end the utterance */
          ps_end_utt(ps);
          printf("%s\n", ps_get_hyp(ps, NULL, &uttid));
          start = -1;
        }
        else {
          ps_process_raw(ps, adbuf, k, FALSE, FALSE);
        }
      }
    }
    else {
      if (start < 0) {    /* Not in an utt; start a new one */
        if (ps_start_utt(ps, NULL) < 0)
          E_FATAL("ps_start_utt() failed\n");
        start = ((cont->read_ts - k) * 100.0) / file_ad.sps;
      }
      ps_process_raw(ps, adbuf, k, FALSE, FALSE);
    }
  }
}

static PP_Instance g_instance = 0;
static PPB_GetInterface get_browser_interface = NULL;
static PPB_Messaging* ppb_messaging_interface = NULL;
static PPB_VarArrayBuffer* ppb_var_array_buffer = NULL;

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[])
{
  g_instance = instance;
  nacl_io_init_ppapi(instance, get_browser_interface);

  if (umount("/")) {
    perror("failed to unmount root");
    return PP_FALSE;
  }

  if (mount("", "/", "memfs", 0, "")) {
    perror("failed to mount root");
    return PP_FALSE;
  }

  if (mount("http://localhost:8000/model", "/model", "httpfs", 0, NULL)) {
    perror("failed to mount models directory");
    return PP_FALSE;
  }

  /*
  if (pocketsphinx_init())
    return PP_FALSE;
    */

  return PP_TRUE;
}

static void
Instance_DidDestroy(PP_Instance instance)
{
}

static void
Instance_DidChangeView(PP_Instance instance,
                                   PP_Resource view_resource)
{
}

static void
Instance_DidChangeFocus(PP_Instance instance, PP_Bool has_focus)
{
}

static PP_Bool
Instance_HandleDocumentLoad(PP_Instance instance, PP_Resource url_loader)
{
  /* NaCl modules do not need to handle the document load function. */
  return PP_FALSE;
}

static void
Messaging_HandleMessage(PP_Instance instance, struct PP_Var message)
{
  // fprintf(stderr, "%d %d\n", message.type, message.padding);
  if (PP_VARTYPE_ARRAY_BUFFER == message.type) {
    int16 *data = (int16 *) ppb_var_array_buffer->Map(message);
    fprintf(stderr, "%d\n", data[1]);
  }
}

PP_EXPORT int32_t
PPP_InitializeModule(PP_Module a_module_id, PPB_GetInterface get_browser)
{
  get_browser_interface = get_browser;
  ppb_messaging_interface =
      (PPB_Messaging*)(get_browser(PPB_MESSAGING_INTERFACE));
  ppb_var_array_buffer =
    (PPB_VarArrayBuffer*)(get_browser(PPB_VAR_ARRAY_BUFFER_INTERFACE));
  return PP_OK;
}

PP_EXPORT const void*
PPP_GetInterface(const char* interface_name)
{
  if (strcmp(interface_name, PPP_INSTANCE_INTERFACE) == 0) {
    static PPP_Instance instance_interface = {
      &Instance_DidCreate,
      &Instance_DidDestroy,
      &Instance_DidChangeView,
      &Instance_DidChangeFocus,
      &Instance_HandleDocumentLoad,
    };

    return &instance_interface;
  } else if (strcmp(interface_name, PPP_MESSAGING_INTERFACE) == 0) {
    static PPP_Messaging messaging_interface = {
      &Messaging_HandleMessage,
    };

    return &messaging_interface;
  }
  return NULL;
}

PP_EXPORT void
PPP_ShutdownModule()
{
}
