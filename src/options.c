/*
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-2015 by Solar Designer
 *
 * ...with changes in the jumbo patch, by JimF and magnum (and various others?)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#define NEED_OS_FORK
#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "misc.h"
#include "params.h"
#include "memory.h"
#include "list.h"
#include "loader.h"
#include "logger.h"
#include "status.h"
#include "recovery.h"
#include "options.h"
#include "bench.h"
#include "external.h"
#include "john.h"
#include "dynamic.h"
#include "unicode.h"
#include "fake_salts.h"
#include "path.h"
#include "regex.h"
#ifdef HAVE_MPI
#include "john-mpi.h"
#ifdef _OPENMP
#define _MP_VERSION "_mpi+omp"
#else
#define _MP_VERSION "_mpi"
#endif
#define _PER_NODE "per node "
#else
#ifdef _OPENMP
#define _MP_VERSION "_omp"
#else
#define _MP_VERSION ""
#endif
#define _PER_NODE ""
#endif
#ifdef DEBUG
#define DEBUG_STRING "_dbg"
#else
#define DEBUG_STRING ""
#endif
#ifdef WITH_ASAN
#define ASAN_STRING "_asan"
#else
#define ASAN_STRING ""
#endif
#if defined(MEMDBG_ON) && defined(MEMDBG_EXTRA_CHECKS)
#define MEMDBG_STRING "_memdbg-ex"
#elif defined(MEMDBG_ON)
#define MEMDBG_STRING "_memdbg"
#else
#define MEMDBG_STRING ""
#endif
#ifdef HAVE_OPENCL
#include "common-opencl.h"
#endif
#if HAVE_LIBGMP || HAVE_INT128 || HAVE___INT128 || HAVE___INT128_T
#include "prince.h"
#endif
#include "memdbg.h"

struct options_main options;
struct pers_opts pers_opts; /* Not reset after forked resume */
static char *field_sep_char_str, *show_uncracked_str, *salts_str;
static char *encoding_str, *target_enc_str, *internal_enc_str;
#if FMT_MAIN_VERSION > 11
static char *costs_str;
#endif

static struct opt_entry opt_list[] = {
	{"", FLG_PASSWD, 0, 0, 0, OPT_FMT_ADD_LIST, &options.passwd},
	{"single", FLG_SINGLE_SET, FLG_CRACKING_CHK, 0, FLG_STACKING,
		OPT_FMT_STR_ALLOC, &pers_opts.activesinglerules},
	{"wordlist", FLG_WORDLIST_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.wordlist},
	{"loopback", FLG_LOOPBACK_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.wordlist},
#if HAVE_LIBGMP || HAVE_INT128 || HAVE___INT128 || HAVE___INT128_T
	{"prince", FLG_PRINCE_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.wordlist},
	{"prince-loopback", FLG_PRINCE_SET | FLG_PRINCE_LOOPBACK | FLG_DUPESUPP,
		FLG_CRACKING_CHK, 0, 0, OPT_FMT_STR_ALLOC,
		&options.wordlist},
	{"prince-elem-cnt-min", FLG_ZERO, 0, FLG_PRINCE_CHK,
		OPT_REQ_PARAM, "%d", &prince_elem_cnt_min},
	{"prince-elem-cnt-max", FLG_ZERO, 0, FLG_PRINCE_CHK,
		OPT_REQ_PARAM, "%d", &prince_elem_cnt_max},
	{"prince-skip", FLG_ZERO, 0, FLG_PRINCE_CHK,
		0, OPT_FMT_STR_ALLOC, &prince_skip_str},
	{"prince-limit", FLG_ZERO, 0, FLG_PRINCE_CHK,
		0, OPT_FMT_STR_ALLOC, &prince_limit_str},
	{"prince-wl-dist-len", FLG_PRINCE_DIST, 0, FLG_PRINCE_CHK, 0},
	{"prince-wl-max", FLG_ZERO, 0, FLG_PRINCE_CHK,
		OPT_REQ_PARAM, "%d", &prince_wl_max},
	{"prince-case-permute", FLG_PRINCE_CASE_PERMUTE, 0,
		FLG_PRINCE_CHK, FLG_PRINCE_MMAP},
	{"prince-keyspace", FLG_PRINCE_KEYSPACE | FLG_STDOUT, 0,
		FLG_PRINCE_CHK, 0},
	{"prince-mmap", FLG_PRINCE_MMAP, 0,
		FLG_PRINCE_CHK, FLG_PRINCE_CASE_PERMUTE},
#endif
	/* -enc is an alias for -input-enc for legacy reasons */
	{"encoding", FLG_INPUT_ENC, FLG_INPUT_ENC,
		0, 0, OPT_FMT_STR_ALLOC, &encoding_str},
	{"input-encoding", FLG_INPUT_ENC, FLG_INPUT_ENC,
		0, 0, OPT_FMT_STR_ALLOC, &encoding_str},
	{"internal-encoding", FLG_SECOND_ENC, FLG_SECOND_ENC,
		0, 0, OPT_FMT_STR_ALLOC, &internal_enc_str},
	{"target-encoding", FLG_SECOND_ENC, FLG_SECOND_ENC,
		0, 0, OPT_FMT_STR_ALLOC, &target_enc_str},
	{"stdin", FLG_STDIN_SET, FLG_CRACKING_CHK},
#if HAVE_WINDOWS_H
	{"pipe", FLG_PIPE_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.sharedmemoryfilename},
#else
	{"pipe", FLG_PIPE_SET, FLG_CRACKING_CHK},
#endif
	{"rules", FLG_RULES_SET, FLG_RULES, FLG_RULES_ALLOW, FLG_STDIN_CHK,
		OPT_FMT_STR_ALLOC, &pers_opts.activewordlistrules},
	{"incremental", FLG_INC_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.charset},
	{"mask", FLG_MASK_SET, FLG_MASK_CHK,
		0, FLG_REGEX_CHK, OPT_FMT_STR_ALLOC, &options.mask},
	{"1", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[0]},
	{"2", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[1]},
	{"3", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[2]},
	{"4", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[3]},
	{"5", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[4]},
	{"6", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[5]},
	{"7", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[6]},
	{"8", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[7]},
	{"9", FLG_ZERO, 0, FLG_MASK_SET, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.custom_mask[8]},
	{"markov", FLG_MKV_SET, FLG_CRACKING_CHK,
		0, 0, OPT_FMT_STR_ALLOC, &options.mkv_param},
	{"mkv-stats", FLG_ZERO, 0,
		FLG_MKV_SET, OPT_REQ_PARAM, OPT_FMT_STR_ALLOC,
		&options.mkv_stats},
	{"external", FLG_EXTERNAL_SET, FLG_EXTERNAL_CHK,
		0, OPT_REQ_PARAM, OPT_FMT_STR_ALLOC, &options.external},
#if HAVE_REXGEN
	{"regex", FLG_REGEX_SET, FLG_REGEX_CHK,
		0, FLG_MASK_CHK | OPT_REQ_PARAM, OPT_FMT_STR_ALLOC,
		&options.regex},
#endif
	{"stdout", FLG_STDOUT, FLG_STDOUT,
		FLG_CRACKING_SUP, FLG_SINGLE_CHK | FLG_BATCH_CHK,
		"%u", &options.length},
	{"restore", FLG_RESTORE_SET, FLG_RESTORE_CHK,
		0, ~FLG_RESTORE_SET & ~OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.session},
	{"session", FLG_SESSION, FLG_SESSION,
		FLG_CRACKING_SUP, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.session},
	{"status", FLG_STATUS_SET, FLG_STATUS_CHK,
		0, ~FLG_STATUS_SET & ~OPT_REQ_PARAM & ~FLG_DYNFMT,
		OPT_FMT_STR_ALLOC, &options.session},
	{"make-charset", FLG_MAKECHR_SET, FLG_MAKECHR_CHK,
		0, FLG_CRACKING_CHK | FLG_SESSION | OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.charset},
	{"show", FLG_SHOW_SET, FLG_SHOW_CHK,
		0, FLG_CRACKING_SUP | FLG_MAKECHR_CHK,
		OPT_FMT_STR_ALLOC, &show_uncracked_str},
	{"test", FLG_TEST_SET, FLG_TEST_CHK,
		0, ~FLG_TEST_SET & ~FLG_FORMAT & ~FLG_SAVEMEM & ~FLG_DYNFMT &
		~OPT_REQ_PARAM & ~FLG_NOLOG, "%u", &benchmark_time},
	{"users", FLG_NONE, 0, FLG_PASSWD, OPT_REQ_PARAM,
		OPT_FMT_ADD_LIST_MULTI, &options.loader.users},
	{"groups", FLG_NONE, 0, FLG_PASSWD, OPT_REQ_PARAM,
		OPT_FMT_ADD_LIST_MULTI, &options.loader.groups},
	{"shells", FLG_NONE, 0, FLG_PASSWD, OPT_REQ_PARAM,
		OPT_FMT_ADD_LIST_MULTI, &options.loader.shells},
	{"salts", FLG_SALTS, FLG_SALTS, FLG_PASSWD, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &salts_str},
	{"save-memory", FLG_SAVEMEM, FLG_SAVEMEM, 0, OPT_REQ_PARAM,
		"%u", &mem_saving_level},
	{"node", FLG_NODE, FLG_NODE, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.node_str},
#if OS_FORK
	{"fork", FLG_FORK, FLG_FORK,
		FLG_CRACKING_CHK, FLG_STDIN_CHK | FLG_STDOUT | FLG_PIPE_CHK | OPT_REQ_PARAM,
		"%u", &options.fork},
#endif
	{"pot", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &pers_opts.activepot},
	{"format", FLG_FORMAT, FLG_FORMAT,
		0, FLG_STDOUT | OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.format},
	{"subformat", FLG_ZERO, 0,
		0, FLG_STDOUT | OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.subformat},
	{"list", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.listconf},
	{"mem-file-size", FLG_ZERO, 0,
		FLG_WORDLIST_CHK, (FLG_DUPESUPP | FLG_SAVEMEM |
		FLG_STDIN_CHK | FLG_PIPE_CHK | OPT_REQ_PARAM),
		"%zu", &options.max_wordfile_memory},
	{"dupe-suppression", FLG_DUPESUPP, FLG_DUPESUPP, 0,
		FLG_SAVEMEM | FLG_STDIN_CHK | FLG_PIPE_CHK},
	{"fix-state-delay", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%u", &options.max_fix_state_delay},
	{"field-separator-char", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &field_sep_char_str},
	{"config", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.config},
	{"nolog", FLG_NOLOG, FLG_NOLOG},
	{"log-stderr", FLG_LOG_STDERR | FLG_NOLOG, FLG_LOG_STDERR},
	{"crack-status", FLG_CRKSTAT, FLG_CRKSTAT},
	{"mkpc", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%d", &options.force_maxkeys},
	{"min-length", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%u", &options.force_minlength},
	{"max-length", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%u", &options.force_maxlength},
	{"max-run-time", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%u", &options.max_run_time},
	{"progress-every", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		"%u", &options.status_interval},
	{"regen-lost-salts", FLG_ZERO, 0, FLG_CRACKING_CHK, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &regen_salts_options},
	{"bare-always-valid", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		"%c", &options.dynamic_bare_hashes_always_valid},
	{"reject-printable", FLG_REJECT_PRINTABLE, FLG_REJECT_PRINTABLE},
	{"verbosity", FLG_VERBOSITY, FLG_VERBOSITY, 0, OPT_REQ_PARAM,
		"%u", &options.verbosity},
#ifdef HAVE_OPENCL
	{"platform", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_STR_ALLOC, &options.ocl_platform},
	{"force-scalar", FLG_SCALAR, FLG_SCALAR, 0, FLG_VECTOR},
	{"force-vector-width", FLG_VECTOR, FLG_VECTOR, 0,
		(FLG_SCALAR | OPT_REQ_PARAM), "%u", &options.v_width},
#endif
#if defined(HAVE_OPENCL) || defined(HAVE_CUDA)
	{"devices", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
		OPT_FMT_ADD_LIST_MULTI, &options.gpu_devices},
#endif
	{"skip-self-tests", FLG_NOTESTS, FLG_NOTESTS},
#if FMT_MAIN_VERSION > 11
	{"costs", FLG_ZERO, 0, 0, OPT_REQ_PARAM,
                OPT_FMT_STR_ALLOC, &costs_str},

#endif
	{"keep-guessing", FLG_KEEP_GUESSING, FLG_KEEP_GUESSING},
	{"stress-test", FLG_LOOPTEST | FLG_TEST_SET, FLG_TEST_CHK,
		0, ~FLG_TEST_SET & ~FLG_FORMAT & ~FLG_SAVEMEM & ~FLG_DYNFMT &
		~OPT_REQ_PARAM & ~FLG_NOLOG, "%u", &benchmark_time},
	{NULL}
};

#define JOHN_COPYRIGHT \
	"Solar Designer and others"

// the 2 DJ_DOS builds currently set this (and do not build the header). If other environs
// can not build the header, then they will also have this value set.
#ifdef NO_JOHN_BLD
#define JOHN_BLD "unk-build-type"
#else
#include "john_build_rule.h"
#endif

#if OS_FORK
#define JOHN_USAGE_FORK \
"--fork=N                  fork N processes\n"
#else
#define JOHN_USAGE_FORK ""
#endif

#if HAVE_REXGEN
#define JOHN_USAGE_REGEX \
	"--regex=REGEXPR           regular expression mode (see doc/README.librexgen)\n"
#else
#define JOHN_USAGE_REGEX ""
#endif

#if HAVE_LIBGMP || HAVE_INT128 || HAVE___INT128 || HAVE___INT128_T
#define PRINCE_USAGE \
	"--prince[=FILE]           PRINCE mode, read words from FILE\n"
#else
#define PRINCE_USAGE ""
#endif

#define JOHN_USAGE	  \
"John the Ripper password cracker, version " JOHN_VERSION _MP_VERSION DEBUG_STRING MEMDBG_STRING ASAN_STRING " [" JOHN_BLD "]\n" \
"Copyright (c) 1996-2015 by " JOHN_COPYRIGHT "\n" \
"Homepage: http://www.openwall.com/john/\n" \
"\n" \
"Usage: %s [OPTIONS] [PASSWORD-FILES]\n" \
"--single[=SECTION]        \"single crack\" mode\n" \
"--wordlist[=FILE] --stdin wordlist mode, read words from FILE or stdin\n" \
"                  --pipe  like --stdin, but bulk reads, and allows rules\n" \
"--loopback[=FILE]         like --wordlist, but fetch words from a .pot file\n" \
"--dupe-suppression        suppress all dupes in wordlist (and force preload)\n" \
PRINCE_USAGE \
"--encoding=NAME           input encoding (eg. UTF-8, ISO-8859-1). See also\n" \
"                          doc/ENCODING and --list=hidden-options.\n" \
"--rules[=SECTION]         enable word mangling rules for wordlist modes\n" \
"--incremental[=MODE]      \"incremental\" mode [using section MODE]\n" \
"--mask=MASK               mask mode using MASK\n" \
"--markov[=OPTIONS]        \"Markov\" mode (see doc/MARKOV)\n" \
"--external=MODE           external mode or word filter\n" \
JOHN_USAGE_REGEX \
"--stdout[=LENGTH]         just output candidate passwords [cut at LENGTH]\n" \
"--restore[=NAME]          restore an interrupted session [called NAME]\n" \
"--session=NAME            give a new session the NAME\n" \
"--status[=NAME]           print status of a session [called NAME]\n" \
"--make-charset=FILE       make a charset file. It will be overwritten\n" \
"--show[=LEFT]             show cracked passwords [if =LEFT, then uncracked]\n" \
"--test[=TIME]             run tests and benchmarks for TIME seconds each\n" \
"--users=[-]LOGIN|UID[,..] [do not] load this (these) user(s) only\n" \
"--groups=[-]GID[,..]      load users [not] of this (these) group(s) only\n" \
"--shells=[-]SHELL[,..]    load users with[out] this (these) shell(s) only\n" \
"--salts=[-]COUNT[:MAX]    load salts with[out] COUNT [to MAX] hashes\n" \
"--save-memory=LEVEL       enable memory saving, at LEVEL 1..3\n" \
"--node=MIN[-MAX]/TOTAL    this node's number range out of TOTAL count\n" \
JOHN_USAGE_FORK \
"--pot=NAME                pot file to use\n" \
"--list=WHAT               list capabilities, see --list=help or doc/OPTIONS\n"

#define JOHN_USAGE_FORMAT \
"--format=NAME             force hash of type NAME. The supported formats can\n" \
"                          be seen with --list=formats and --list=subformats\n\n"
#if defined(HAVE_OPENCL) && defined(HAVE_CUDA)
#define JOHN_USAGE_GPU \
"--devices=N[,..]          set OpenCL or CUDA device(s)\n"
#elif defined(HAVE_OPENCL)
#define JOHN_USAGE_GPU \
"--devices=N[,..]          set OpenCL device(s) (list using --list=opencl-devices)\n"
#elif defined (HAVE_CUDA)
#define JOHN_USAGE_GPU \
"--device=N                set CUDA device (list using --list=cuda-devices)\n"
#endif

static void print_usage(char *name)
{
	if (!john_main_process)
		exit(0);

	printf(JOHN_USAGE, name);
#if defined(HAVE_OPENCL) || defined(HAVE_CUDA)
	printf("%s", JOHN_USAGE_GPU);
#endif
	printf("%s", JOHN_USAGE_FORMAT);
	exit(0);
}

void opt_print_hidden_usage(void)
{
	puts("--help                    print usage summary, just like running the command");
	puts("                          without any parameters");
	puts("--config=FILE             use FILE instead of john.conf or john.ini");
	puts("--mem-file-size=SIZE      size threshold for wordlist preload (default 5 MB)");
	printf("--format=CLASS            valid classes: dynamic, cpu");
#if defined(HAVE_OPENCL) || defined(HAVE_CUDA)
	printf(", gpu");
#ifdef HAVE_CUDA
	printf(", cuda");
#endif
#ifdef HAVE_OPENCL
	printf(", opencl");
#endif
#endif
#ifdef _OPENMP
	printf(", omp");
#endif
	printf("\n");
	puts("--subformat=FORMAT        pick a benchmark format for --format=crypt");
	puts("--mkpc=N                  request a lower max. keys per crypt");
	puts("--min-length=N            request a minimum candidate length");
	puts("--max-length=N            request a maximum candidate length");
#if FMT_MAIN_VERSION > 11
	puts("--costs=[-]C[:M][,...]    load salts with[out] cost value Cn [to Mn] for");
	puts("                          tunable cost parameters, see doc/OPTIONS");
	puts("                          (comma separated list of values/ranges per param.)");
#endif
	puts("--field-separator-char=C  use 'C' instead of the ':' in input and pot files");
	puts("--fix-state-delay=N       performance tweak, see doc/OPTIONS");
	puts("--nolog                   disables creation and writing to john.log file");
	puts("--log-stderr              log to screen instead of file");
	puts("--bare-always-valid=C     if C is 'Y' or 'y', then the dynamic format will");
	puts("                          always treat bare hashes as valid");
	puts("--progress-every=N        emit a status line every N seconds");
	puts("--crack-status            emit a status line whenever a password is cracked");
	puts("--keep-guessing           try more candidates for cracked hashes (ie. search");
	puts("                          for plaintext collisions)");
	puts("--max-run-time=N          gracefully exit after this many seconds");
	puts("--regen-lost-salts=N      regenerate lost salts (see doc/OPTIONS)");
	puts("--mkv-stats=FILE          \"Markov\" stats file (see doc/MARKOV)");
	puts("--reject-printable        reject printable binaries");
	puts("--verbosity=N             change verbosity (1-5, default 3)");
	puts("--skip-self-tests         skip self tests");
	puts("--stress-test[=TIME]      loop self tests forever");
	puts("--input-encoding=NAME     input encoding (alias for --encoding)");
	puts("--internal-encoding=NAME  encoding used in rules/masks (see doc/ENCODING)");
	puts("--target-encoding=NAME    output encoding (used by format, see doc/ENCODING)");
#ifdef HAVE_OPENCL
	puts("--force-scalar            (OpenCL) force scalar mode");
	puts("--force-vector-width=N    (OpenCL) force vector width N");
	puts("--platform=N              set OpenCL platform (deprecated)");
#endif
#if HAVE_LIBGMP || HAVE_INT128 || HAVE___INT128 || HAVE___INT128_T
	puts("\nPRINCE mode options:");
	puts("--prince-loopback[=FILE]  fetch words from a .pot file");
	puts("--prince-elem-cnt-min=N   minimum number of elements per chain (1)");
	puts("--prince-elem-cnt-max=N   maximum number of elements per chain (8)");
	puts("--prince-skip=N           initial skip");
	puts("--prince-limit=N          limit number of candidates generated");
	puts("--prince-wl-dist-len      calculate length distribution from wordlist");
	puts("                          instead of using built-in table");
	puts("--prince-wl-max=N         load only N words from input wordlist");
	puts("--prince-case-permute     permute case of first letter");
	puts("--prince-mmap             memory-map infile (not available when permuting case)");
	puts("--prince-keyspace         just show total keyspace that would be produced");
	puts("                          (disregarding skip and limit)");
#endif
	puts("");
}

void opt_init(char *name, int argc, char **argv, int show_usage)
{
	if (show_usage)
		print_usage(name);

	memset(&options, 0, sizeof(options));

	options.loader.field_sep_char = ':';
	options.regen_lost_salts = 0;
	options.max_fix_state_delay = 0;
	options.max_wordfile_memory = WORDLIST_BUFFER_DEFAULT;
	options.force_maxkeys = options.force_maxlength = 0;
	options.force_minlength = -1; options.reload_at_crack = 0;
	options.max_run_time = options.status_interval = 0;
	options.reload_at_save = options.dynamic_bare_hashes_always_valid = 0;
	options.verbosity = 3;

	list_init(&options.passwd);

	options.loader.flags = DB_LOGIN;
	list_init(&options.loader.users);
	list_init(&options.loader.groups);
	list_init(&options.loader.shells);
#if defined(HAVE_OPENCL) || defined(HAVE_CUDA)
	list_init(&options.gpu_devices);
#endif

	options.length = -1;

	opt_process(opt_list, &options.flags, argv);

	ext_flags = 0;
	if (options.flags & FLG_EXTERNAL_CHK) {
		if (options.flags & (FLG_CRACKING_CHK | FLG_MAKECHR_CHK)) {
			ext_flags = EXT_REQ_FILTER | EXT_USES_FILTER;
		} else {
			options.flags |= FLG_CRACKING_SET;
			ext_flags = EXT_REQ_GENERATE | EXT_USES_RESTORE |
			    EXT_USES_GENERATE | EXT_USES_FILTER;
			if (rec_restored)
				ext_flags |= EXT_REQ_RESTORE;
		}
	}
	if (options.flags & FLG_MASK_CHK) {
		if (options.flags & FLG_CRACKING_CHK)
			options.flags |= FLG_MASK_STACKED;
		else
			options.flags |= FLG_CRACKING_SET;
	}
	if (options.flags & FLG_REGEX_CHK) {
		//if (options.flags & FLG_CRACKING_CHK)
		//	options.flags |= FLG_REGEX_STACKED;
		//else
			options.flags |= FLG_CRACKING_SET;
	}

	/* Bodge for bash completion of eg. "john -stdout -list=..." */
	if (options.listconf != NULL && options.fork == 0)
		options.flags |= (FLG_CRACKING_SUP | FLG_STDIN_SET);

	if (!(options.flags & FLG_ACTION))
		options.flags |= FLG_BATCH_SET;

	opt_check(opt_list, options.flags, argv);

#if HAVE_OPENCL
	if (options.format && strcasestr(options.format, "opencl") &&
	    (options.flags & FLG_FORK) && options.gpu_devices->count == 0) {
		list_add(options.gpu_devices, "all");
	}
#endif
	if (options.flags & FLG_MASK_STACKED && ext_flags & EXT_REQ_FILTER) {
		fprintf(stderr, "Can't use Hybrid Mask mode with External "
		        "Filter\n");
		error();
	}

	if (options.session) {
		rec_name = options.session;
		rec_name_completed = 0;
	}

#ifdef HAVE_MPI
	if (mpi_p > 1) {
		if (options.flags & FLG_RESTORE_CHK || rec_restored) {
			if (options.fork && options.fork != mpi_p) {
				if (john_main_process)
				fprintf(stderr,
				        "Node count in session file is %d.\n",
				        options.fork);
				error();
			}
			options.fork = 0;
			options.flags &= ~FLG_FORK;
		} else
		if (options.fork) {
			if (john_main_process)
				fprintf(stderr, "Can't use --fork with MPI.\n");
			error();
		}
	}
#endif

	if (options.flags & FLG_RESTORE_CHK) {
#if OS_FORK || defined(HAVE_MPI)
		char *rec_name_orig = rec_name;
#endif
		rec_restored = 1;
#ifndef HAVE_MPI
		rec_restore_args(1);
#else
		rec_restore_args(mpi_p);
#endif
#if OS_FORK || defined(HAVE_MPI)
#ifndef HAVE_MPI
		if (options.fork) {
#else
		if (options.fork || mpi_p > 1) {
#endif
			rec_name = rec_name_orig;
			rec_name_completed = 0;
		}
#endif
		return;
	}

	if (options.flags & FLG_STATUS_CHK) {
#if OS_FORK
		char *rec_name_orig = rec_name;
#endif
		rec_restore_args(0);
		options.flags |= FLG_STATUS_SET;
		status_init(NULL, 1);
		status_print();
#if OS_FORK
		if (options.fork) {
			unsigned int i;
			for (i = 2; i <= options.fork; i++) {
				rec_name = rec_name_orig;
				rec_name_completed = 0;
				rec_restoring_now = 0;
				options.node_min = options.node_max = i;
				john_main_process = 0;
				rec_restore_args(0);
				john_main_process = 1;
				options.node_min = options.node_max = i;
				options.flags |= FLG_STATUS_SET;
				if (rec_restoring_now)
					status_print();
			}
		}
#endif
		path_done();
		cleanup_tiny_memory();
		MEMDBG_PROGRAM_EXIT_CHECKS(stderr);
		exit(0);
	}
#if FMT_MAIN_VERSION > 11
	if (costs_str) {
		/*
		 * costs_str: [-]COST1[:MAX1][,[-]COST2[:MAX2]][...,[-]COSTn[:MAXn]]
		 *            but not --costs=,2:9 or --costs=,-99
		 *            istead use --costs=:,2:9 or --costs=:,-99
		 *            if you want to specify values for the 2nd cost param.
		 */
		int i;
		char *range[FMT_TUNABLE_COSTS] = { 0 };
		char *dummy;

		for( i = 0; i < FMT_TUNABLE_COSTS; i++) {
			if (i)
				range[i] = strtok(NULL, ",");
			else
				range[i] = strtok(costs_str, ",");

			options.loader.min_cost[i] = 0;
			options.loader.max_cost[i] = UINT_MAX;
		}
		dummy = strtok(NULL, ",");
		if (dummy) {
			if (john_main_process)
				fprintf(stderr, "max. %d different tunable cost parameters"
				                " supported\n", FMT_TUNABLE_COSTS);
			error();
		}
		for( i = 0; i < FMT_TUNABLE_COSTS; i++) {
			int negative;
			int two_values;

			if (range[i] == NULL)
				break;
			if (range[i][0] == '-') {
				negative = 1;
				range[i]++;
			}
			else {
				negative = 0;
			}
			if (range[i][0] != '\0') {
				two_values = 0;
				if (sscanf(range[i], "%u:%u",
				           &options.loader.min_cost[i], &options.loader.max_cost[i]) == 2)
					two_values = 1;
				if (two_values && negative) {
					if (john_main_process)
						fprintf(stderr, "Usage of negative --cost is not valid"
						                " for cost range (min:max)\n");
					error();
				}
				if (!two_values)
					sscanf(range[i], "%u", &options.loader.min_cost[i]);
					if (negative && options.loader.min_cost[i] == 0) {
						if (john_main_process)
							fprintf(stderr, "Usage of negative --cost is not valid"
							                " for value 0\n");
						error();
					}
				if (!two_values) {
					if (negative) {
						options.loader.max_cost[i] = options.loader.min_cost[i] - 1;
						options.loader.min_cost[i] = 0;
					}
					else {
						options.loader.max_cost[i] = UINT_MAX;
					}
				}
				if (options.loader.max_cost[i] < options.loader.min_cost[i]) {
					if (john_main_process)
						fprintf(stderr, "Max. cost value must be >= min. cost value\n");
					error();
				}
			}
		}
	}
	else {
		int i;

		for( i = 0; i < FMT_TUNABLE_COSTS; i++) {
			options.loader.min_cost[i] = 0;
			options.loader.max_cost[i] = UINT_MAX;
		}
	}
#endif

	if (options.flags & FLG_SALTS) {
		int two_salts = 0;
		if (sscanf(salts_str, "%d:%d", &options.loader.min_pps, &options.loader.max_pps) == 2)
			two_salts = 1;
		if (!two_salts && sscanf(salts_str, "%d,%d", &options.loader.min_pps, &options.loader.max_pps) == 2)
			two_salts = 1;
		if (!two_salts){
			sscanf(salts_str, "%d", &options.loader.min_pps);
			if (options.loader.min_pps < 0) {
				options.loader.max_pps = -1 - options.loader.min_pps;
				options.loader.min_pps = 0;
			}
			else
				options.loader.max_pps = 0x7fffffff;
		} else if (options.loader.min_pps < 0) {
			if (john_main_process)
				fprintf(stderr, "Usage of negative -salt min "
				        "is not 'valid' if using Min and Max "
				        "salt range of values\n");
			error();
		}
		if (options.loader.min_pps > options.loader.max_pps) {
			if (john_main_process)
				fprintf(stderr, "Min number salts wanted is "
				        "less than Max salts wanted\n");
			error();
		}
	}

	if (options.flags & FLG_VERBOSITY &&
	    (options.verbosity < 1 || options.verbosity > 5)) {
		if (john_main_process)
			fprintf(stderr, "Invalid --verbosity level, use 1-5\n");
		error();
	}
	if (options.length < 0)
		options.length = PLAINTEXT_BUFFER_SIZE - 3;
	else
	if (options.length < 1 || options.length > PLAINTEXT_BUFFER_SIZE - 3) {
		if (john_main_process)
			fprintf(stderr, "Invalid plaintext length requested\n");
		error();
	}
	if (options.force_maxlength && options.force_maxlength < options.force_minlength) {
		if (john_main_process)
			fprintf(stderr, "Invalid options: --min-length larger "
			        "than --max-length\n");
		error();
	}
	if (options.force_maxlength < 0 || options.force_maxlength > PLAINTEXT_BUFFER_SIZE - 3) {
		if (john_main_process)
			fprintf(stderr, "Invalid max length requested\n");
		error();
	}
	if (options.force_maxkeys != 0 && options.force_maxkeys < 1) {
		if (john_main_process)
			fprintf(stderr,
			        "Invalid options: --mkpc must be at least 1\n");
		error();
	}

	if (options.flags & FLG_STDOUT) options.flags &= ~FLG_PWD_REQ;

#if OS_FORK
	if ((options.flags & FLG_FORK) &&
	    (options.fork < 2 || options.fork > 1024)) {
		fprintf(stderr, "--fork number must be between 2 and 1024\n");
		error();
	}
#endif

	if (options.node_str) {
		const char *msg = NULL;
		int n;
		if ((n = sscanf(options.node_str, "%u-%u/%u",
		    &options.node_min, &options.node_max,
		    &options.node_count)) != 3) {
			n = sscanf(options.node_str, "%u/%u",
			    &options.node_min, &options.node_count);
			options.node_max = options.node_min;
#if OS_FORK
			if (options.fork)
				options.node_max += options.fork - 1;
#endif
#ifdef HAVE_MPI
			if (mpi_p > 1)
				options.node_max += mpi_p - 1;
#endif
		}
		if (n < 2)
			msg = "valid syntax is MIN-MAX/TOTAL or N/TOTAL";
		else if (!options.node_min)
			msg = "valid node numbers start from 1";
		else if (options.node_min > options.node_max)
			msg = "range start can't exceed range end";
		else if (options.node_count < 2)
			msg = "node count must be at least 2";
		else if (options.node_max > options.node_count)
			msg = "node numbers can't exceed node count";
#if OS_FORK
		else if (options.fork &&
		    options.node_max - options.node_min + 1 != options.fork)
			msg = "range must be consistent with --fork number";
#endif
#ifdef HAVE_MPI
		if (mpi_p > 1 &&
		    options.node_max - options.node_min + 1 != mpi_p)
			msg = "range must be consistent with MPI node count";
#endif
		else if (!options.fork &&
#ifdef HAVE_MPI
		         mpi_p == 1 &&
#endif
		    options.node_max - options.node_min + 1 ==
		    options.node_count)
			msg = "node numbers can't span the whole range";
		if (msg) {
			if (john_main_process)
			fprintf(stderr, "Invalid node specification: %s: %s\n",
			    options.node_str, msg);
			error();
		}
#if OS_FORK
	} else if (options.fork) {
		options.node_min = 1;
		options.node_max = options.node_min + options.fork - 1;
		options.node_count = options.node_max;
#endif
	}
#ifdef HAVE_MPI
	else if (mpi_p > 1) {
		options.node_min = 1;
		options.node_max = options.node_min + mpi_p - 1;
		options.node_count = options.node_max;
	}
#endif

	/*
	 * By default we are setup in 7 bit ascii mode (for rules) and
	 * ISO-8859-1 codepage (for Unicode conversions).  We can change
	 * that in john.conf or with the --encoding option.
	 */
	if ((encoding_str && !strcasecmp(encoding_str, "list")) ||
	    (internal_enc_str &&
	     !strcasecmp(internal_enc_str, "list")) ||
	    (target_enc_str && !strcasecmp(target_enc_str, "list"))) {
		listEncodings(stdout);
		exit(EXIT_SUCCESS);
	}

	if (encoding_str)
		pers_opts.input_enc = cp_name2id(encoding_str);

	if (target_enc_str)
		pers_opts.target_enc = cp_name2id(target_enc_str);

	if (internal_enc_str)
		pers_opts.internal_enc = cp_name2id(internal_enc_str);

	if (pers_opts.input_enc && pers_opts.input_enc != UTF_8) {
		if (!pers_opts.target_enc)
			pers_opts.target_enc = pers_opts.input_enc;
		if (!pers_opts.internal_enc)
			pers_opts.internal_enc = pers_opts.input_enc;
	}

#ifdef HAVE_OPENCL
	if (options.v_width) {
		if (options.v_width > 1 && options.flags & FLG_SCALAR) {
			if (john_main_process)
				fprintf(stderr, "Scalar or Vector modes are "
				        "mutually exclusive\n");
			error();
		}
		if (options.v_width != 1 && options.v_width != 2 &&
		    options.v_width != 3 && options.v_width != 4 &&
		    options.v_width != 8 && options.v_width != 16) {
			if (john_main_process)
				fprintf(stderr, "Vector width must be one of"
				        " 1, 2, 3, 4, 8 or 16\n");
			error();
		}
		if (options.v_width == 3 && john_main_process)
			fprintf(stderr, "Warning: vector width 3 is not "
			        "expected to work well with all formats\n");
	}
#endif
	/*
	 * This line is not a bug - it extends the next conditional.
	 * It's from commit 90a8caee.
	 */
	if (!(options.subformat && !strcasecmp(options.subformat, "list")) &&
	    (!options.listconf))
	if ((options.flags & (FLG_PASSWD | FLG_PWD_REQ)) == FLG_PWD_REQ) {
		if (john_main_process)
			fprintf(stderr, "Password files required, "
			        "but none specified\n");
		error();
	}

	if ((options.flags & (FLG_PASSWD | FLG_PWD_SUP)) == FLG_PASSWD) {
		if (john_main_process)
			fprintf(stderr, "Password files specified, "
			        "but no option would use them\n");
		error();
	}

	if ( (options.flags & FLG_SHOW_CHK) && show_uncracked_str) {
		if (!strcasecmp(show_uncracked_str, "left"))  {
			options.loader.showuncracked = 1;
			// Note we 'do' want the pot file to load normally, but during that load,
			// we print out hashes left. At the end of the load, john exits.  However
			// we do NOT want the 'special' -SHOW_CHK logic to happen (which happens
			// instead of normal loading if we are in 'normal' show mode)
			options.flags &= ~FLG_SHOW_CHK;
		}
		else {
			fprintf(stderr, "Invalid option in --show switch.\nOnly --show or --show=left are valid\n");
			error();
		}
	}

	if (options.dynamic_bare_hashes_always_valid == 'Y' || options.dynamic_bare_hashes_always_valid == 'y' ||
		options.dynamic_bare_hashes_always_valid == '1' || options.dynamic_bare_hashes_always_valid == 't' || options.dynamic_bare_hashes_always_valid == 'T')
		options.dynamic_bare_hashes_always_valid = 'Y';
	else if (options.dynamic_bare_hashes_always_valid == 'N' || options.dynamic_bare_hashes_always_valid == 'n' ||
		options.dynamic_bare_hashes_always_valid == '0' || options.dynamic_bare_hashes_always_valid == 'f' || options.dynamic_bare_hashes_always_valid == 'F')
		options.dynamic_bare_hashes_always_valid = 'N';

	options.regen_lost_salts = regen_lost_salt_parse_options();

	if (field_sep_char_str) {
		// Literal tab or TAB will mean 0x09 tab character
		if (!strcasecmp(field_sep_char_str, "tab"))
			field_sep_char_str = "\x09";
		if (strlen(field_sep_char_str) == 1)
			options.loader.field_sep_char = *field_sep_char_str;
		else if (field_sep_char_str[0] == '\\' &&
		         (field_sep_char_str[1]=='x' ||
		          field_sep_char_str[1]=='X')) {
			unsigned xTmp=0;

			sscanf(&field_sep_char_str[2], "%x", &xTmp);
			if (!xTmp || xTmp > 255) {
				if (john_main_process)
					fprintf (stderr, "trying to use an "
					         "invalid field separator char:"
					         "  %s\n",
					         field_sep_char_str);
				error();
			}
			options.loader.field_sep_char = (char)xTmp;
		}

		if (options.loader.field_sep_char != ':')
			if (john_main_process)
				fprintf (stderr, "using field sep char '%c' "
				         "(0x%02x)\n", options.loader.field_sep_char,
				         options.loader.field_sep_char);
	}

	rec_argc = argc; rec_argv = argv;
	rec_check = 0;
}
