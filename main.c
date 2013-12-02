#include <stdlib.h>

#include <sphinxbase/cont_ad.h>
#include <sphinxbase/err.h>
#include <pocketsphinx.h>

#include "ppapi_simple/ps_event.h"
#include "ppapi_simple/ps_main.h"

static FILE *stream;

static void
print_word_times(int32 start)
{
  E_ERROR("not implemented\n");
}

static int32
cont_read(ad_rec_t *ad_rec, int16 * buf, int32 max)
{
  size_t nread = fread(buf, sizeof *buf, max, stream);
  E_INFO("%d\n", nread);
  return nread > 0 ? nread : -1;
}

int
pepper_main(int argc, char **argv)
{
  /*
  if (umount("/")) {
    perror("failed to unmount root fs");
    exit(EXIT_FAILURE);
  }

  if (mount("", "/", "memfs", 0, NULL)) {
    perror("failed to mount memfs");
    exit(EXIT_FAILURE);
  }

  if (mkdir("/model")) {
    perror("failed to create `models'");
    exit(EXIT_FAILURE);
  }
  */

  if (mount("http://localhost:8000/model", "/model", "httpfs", 0, NULL)) {
    perror("failed to mount models directory");
    exit(EXIT_FAILURE);
  }

  cmd_ln_t *config = cmd_ln_init(NULL, ps_args(), FALSE, NULL);
  cmd_ln_set_str_r(config, "-hmm",  "/model/hmm/en_US//hub4wsj_sc_8k");
  cmd_ln_set_str_r(config, "-dict", "/model/lm/en_US/cmu07a.dic");
  cmd_ln_set_str_r(config, "-lm",   "/model/lm/en_US/hub4.5000.DMP");
  ps_decoder_t *ps = ps_init(config);

  /*
  int16 *buf = (int16 *) malloc(1024);
  size_t nread;

  ps_start_utt(decoder, NULL);
  while ((nread = fread(buf, sizeof *buf, sizeof buf, stream)) > 0)
    ps_process_raw(decoder, buf, nread, FALSE, FALSE);
  ps_end_utt(decoder);

  int32 score;
  const char *uttid;
  fprintf(stderr, "%s\n", ps_get_hyp(decoder, &score, &uttid));
  free(buf);
  */

  if (NULL == (stream = fopen("/model/goforward.raw", "r"))) {
    perror("failed to open audio file");
    exit(EXIT_FAILURE);
  }

  cont_ad_t *cont;
  ad_rec_t file_ad = { 0 };
  file_ad.sps = (int32) cmd_ln_float32_r(config, "-samprate");
  file_ad.bps = sizeof(int16);

  if (NULL == (cont = cont_ad_init_rawmode(&file_ad, cont_read))) {
    E_FATAL("Failed to initialize voice activity detection\n");
    exit(EXIT_FAILURE);
  }

  if (cont_ad_calib(cont) < 0)
    E_INFO("Using default voice activity detection\n");

  int32 start = -1;
  int16 adbuf[4096];
  int32 endsil = (int32) (0.7 * file_ad.sps);       /* 0.7s for utterance end */
  const char *uttid;
  for (;;) {
    int32 k = cont_ad_read(cont, adbuf, sizeof adbuf / sizeof *adbuf);
    E_INFO("%d\n", k);
    if (k < 0) {            /* End of input audio file; end the utt and exit */
      if (start > 0) {
        ps_end_utt(ps);
        if (cmd_ln_boolean_r(config, "-time"))
          print_word_times(start);
        else
          fprintf(stderr, "%s\n", ps_get_hyp(ps, NULL, &uttid));
      }

      break;
    }

    if (cont->state == CONT_AD_STATE_SIL) { /* Silence data got */
      if (start >= 0) {   /* Currently in an utterance */
        if (cont->seglen > endsil) {    /* Long enough silence detected; end the utterance */
          ps_end_utt(ps);
          if (cmd_ln_boolean_r(config, "-time"))
            print_word_times(start);
          else
            fprintf(stderr, "%s\n", ps_get_hyp(ps, NULL, &uttid));
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

  E_INFO("finished\n");

	for (;;) {
		PSEvent *event;
		while ((event = PSEventTryAcquire())) {
			PSEventRelease(event);
		}
	}

  return EXIT_SUCCESS;
}

PPAPI_SIMPLE_REGISTER_MAIN(pepper_main);
