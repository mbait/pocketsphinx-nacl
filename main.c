#include <stdlib.h>
#include <pthread.h>

#include <sphinxbase/cont_ad.h>
#include <sphinxbase/err.h>
#include <pocketsphinx.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_array_buffer.h"
#include "ppapi/c/ppp.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_messaging.h"
#include "nacl_io/nacl_io.h"

#include "queue.h"

static cmd_ln_t *ps_config;
static ps_decoder_t *ps;

static PPB_Var *ppb_var = NULL;
static PPB_VarArrayBuffer *ppb_var_array_buffer = NULL;

static int32
cont_read(ad_rec_t *ad_rec, int16 *buf, int32 max)
{
  /*
  struct PP_Var *jsdata = (struct PP_Var *) queue_poll();
  int16 *data = (int16 *) ppb_var_array_buffer->Map(*jsdata);
  uint32_t byte_len;
  ppb_var_array_buffer->ByteLength(*jsdata, &byte_len);
  fprintf(stderr, "data len: %d\n", byte_len);
  */
  /*
  ppb_var->Release(*jsdata);
  free(jsdata);
  */
   
  // return byte_len / sizeof *buf;
  static FILE *stream = NULL;
  
  if (!stream)
    stream = fopen("model/goforward.raw", "r");

  size_t nread = fread(buf, sizeof *buf, max, stream);
  if (!nread) {
    rewind(stream);
    nread = fread(buf, sizeof *buf, max, stream);
  }

  fprintf(stderr, "nread %d\n", nread);

  return nread;
}

static int
pocketsphinx_init(void)
{
  ps_config = cmd_ln_init(NULL, ps_args(), FALSE, NULL);
  cmd_ln_set_str_r(ps_config, "-hmm",  "model/hmm/en_US/hub4wsj_sc_8k");
  cmd_ln_set_str_r(ps_config, "-dict", "model/lm/en_US/cmu07a.dic");
  cmd_ln_set_str_r(ps_config, "-lm",   "model/lm/en_US/hub4.5000.DMP");
  
  if (NULL == (ps = ps_init(ps_config)))
    return 1;

  cont_ad_t *cont;
  ad_rec_t file_ad = { 0 };

  if (NULL == (cont = cont_ad_init(&file_ad, cont_read))) {
    E_FATAL("failed to initialize voice activity detection\n");
    return 1;
  }

  /*
  if (cont_ad_calib(cont) < 0)
    E_INFO("using default voice activity detection\n");
    */
  if (cont_ad_calib(cont) < 0)
    E_FATAL("Failed to calibrate voice activity detection\n");

  int16 adbuf[4096];
  const char *hyp, *uttid;
  int32 k, ts, rem;

  for (;;) {
    /* Indicate listening for next utterance */
    printf("READY....\n");

    /* Wait data for next utterance */
    while ((k = cont_ad_read(cont, adbuf, 4096)) == 0);

    if (k < 0)
      E_FATAL("Failed to read audio\n");

    /*
     * Non-zero amount of data received; start recognition of new utterance.
     * NULL argument to uttproc_begin_utt => automatic generation of utterance-id.
     */
    if (ps_start_utt(ps, NULL) < 0)
      E_FATAL("Failed to start utterance\n");

    ps_process_raw(ps, adbuf, k, FALSE, FALSE);
    printf("Listening...\n");

    /* Note timestamp for this first block of data */
    ts = cont->read_ts;

    /* Decode utterance until end (marked by a "long" silence, >1sec) */
    for (;;) {
      /* Read non-silence audio data, if any, from continuous listening module */
      if ((k = cont_ad_read(cont, adbuf, 4096)) < 0)
        E_FATAL("Failed to read audio\n");

      if (k == 0) {
        /*
         * No speech data available; check current timestamp with most recent
         * speech to see if more than 1 sec elapsed.  If so, end of utterance.
         */
        if ((cont->read_ts - ts) > DEFAULT_SAMPLES_PER_SEC / 4)
          break;
      }
      else {
        /* New speech data received; note current timestamp */
        ts = cont->read_ts;
      }

      /*
       * Decode whatever data was read above.
       */
      rem = ps_process_raw(ps, adbuf, k, FALSE, FALSE);
    }

    /*
     * Utterance ended; flush any accumulated, unprocessed A/D data and stop
     * listening until current utterance completely decoded
     */
    cont_ad_reset(cont);

    printf("Stopped listening, please wait...\n");
    /* Finish decoding, obtain and print result */
    ps_end_utt(ps);
    hyp = ps_get_hyp(ps, NULL, &uttid);
    printf("%s: %s\n", uttid, hyp);
  }

  cont_ad_close(cont);
}

static void *
pepper_main(void *user_date)
{
  if (pocketsphinx_init())
    exit(EXIT_FAILURE);
}

static PP_Instance g_instance = 0;
static PPB_GetInterface get_browser_interface = NULL;
static PPB_Messaging* ppb_messaging_interface = NULL;

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[])
{
  g_instance = instance;
  nacl_io_init_ppapi(instance, get_browser_interface);

  if (mount("/model", "/model", "httpfs", 0, NULL)) {
    perror("failed to mount models directory");
    return PP_FALSE;
  }

  pthread_t main_thread;
  if (pthread_create(&main_thread, NULL, &pepper_main, NULL)) {
    perror("failed to create main thread");
    return PP_FALSE;
  }

  queue_init();
  
  return PP_TRUE;
}

static void
Instance_DidDestroy(PP_Instance instance)
{
  ps_free(ps);
  cmd_ln_free_r(ps_config);
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
  if (PP_VARTYPE_ARRAY_BUFFER == message.type) {
    fprintf(stderr, "new data\n");
    /*
    if (!queue_offer(memcpy(malloc(sizeof message), &message, sizeof message)))
      fprintf(stderr, "queue is full\n");
      */
  }
}

PP_EXPORT int32_t
PPP_InitializeModule(PP_Module a_module_id, PPB_GetInterface get_browser)
{
  get_browser_interface = get_browser;
  ppb_messaging_interface =
      (PPB_Messaging*)(get_browser(PPB_MESSAGING_INTERFACE));
  ppb_var = (PPB_Var *)(get_browser(PPB_VAR_INTERFACE));
  ppb_var_array_buffer =
    (PPB_VarArrayBuffer *)(get_browser(PPB_VAR_ARRAY_BUFFER_INTERFACE));
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
