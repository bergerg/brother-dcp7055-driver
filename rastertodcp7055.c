/*
 * CUPS raster filter for the Brother DCP-7055 (monochrome, 1 bit per pixel).
 * The printer's command format was learned from the GPL brlaser project.
 *
 * Page data: ESC*b1030m  { "<n>w" 0x00 <line count> <lines> }...  1030M FF
 * Line: 0xFF = blank, otherwise <edit count> <edits>, each edit relative to
 * the previous line:
 *   literal 0ooooccc  (c+1 bytes follow)
 *   repeat  1ooccccc  (c+2 copies of the one byte that follows)
 * o = bytes left unchanged before the edit. A maxed-out field continues in
 * extra bytes: 255, 255, ..., remainder.
 */
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIDTH   2000  /* bytes per line; widest paper at 600 dpi is ~640 */
#define MAX_LINE    (2 * MAX_WIDTH + 64)
#define BLOCK_LINES 64
#define BLOCK_BYTES 16000
#define MAX_EDITS   254

static volatile sig_atomic_t cancelled;
static void on_term(int sig) { (void)sig; cancelled = 1; }

static unsigned char *put_field(unsigned char *p, int value, int max)
{
  if (value < max) return p;
  for (value -= max; value >= 255; value -= 255) *p++ = 255;
  *p++ = (unsigned char)value;
  return p;
}

static int min(int a, int b) { return a < b ? a : b; }

static unsigned char *put_literal(unsigned char *p, int skip, const unsigned char *s, int n)
{
  *p++ = (unsigned char)(min(skip, 15) << 3 | min(n - 1, 7));
  p = put_field(p, skip, 15);
  p = put_field(p, n - 1, 7);
  memcpy(p, s, n);
  return p + n;
}

static unsigned char *put_repeat(unsigned char *p, int skip, unsigned char byte, int n)
{
  *p++ = (unsigned char)(128 | min(skip, 3) << 5 | min(n - 2, 31));
  p = put_field(p, skip, 3);
  p = put_field(p, n - 2, 31);
  *p++ = byte;
  return p;
}

/* Number of equal bytes starting at s[i]. */
static int run_length(const unsigned char *s, int i, int end)
{
  int j = i + 1;
  while (j < end && s[j] == s[i]) j++;
  return j - i;
}

/* ref == NULL: encode the whole line without using the previous one. */
static int encode_line(unsigned char *out, const unsigned char *s, const unsigned char *ref, int width)
{
  unsigned char *p = out + 1;
  int edits = 0, pos = 0, i = 0, end = width, k;

  for (k = 0; k < width && !s[k]; k++) ;
  if (k == width) { out[0] = 0xFF; return 1; }

  if (ref) while (end > 0 && s[end - 1] == ref[end - 1]) end--;

  for (;;) {
    int skip, run;
    if (ref) while (i < end && s[i] == ref[i]) i++;
    if (i >= end) break;
    skip = i - pos;
    if (++edits == MAX_EDITS) {          /* out of edits: rest as one literal */
      p = put_literal(p, skip, s + i, end - i);
      break;
    }
    run = run_length(s, i, end);
    if (run >= 3) {
      p = put_repeat(p, skip, s[i], run);
      i += run;
    } else {
      int start = i;
      while (i < end) {
        if (run_length(s, i, end) >= 3) break;
        if (ref && i + 1 < end && s[i] == ref[i] && s[i + 1] == ref[i + 1]) break;
        i++;
      }
      p = put_literal(p, skip, s + start, i - start);
    }
    pos = i;
  }
  out[0] = (unsigned char)edits;
  return (int)(p - out);
}

static unsigned char block[BLOCK_BYTES];
static int block_bytes, block_lines;

static void flush_block(void)
{
  if (block_lines) {
    printf("%dw%c%c", block_bytes + 2, 0, block_lines);
    fwrite(block, 1, block_bytes, stdout);
  }
  block_bytes = block_lines = 0;
}

static const char *paper_name(const char *size)
{
  if (!strcmp(size, "Letter")) return "LETTER";
  if (!strcmp(size, "Legal")) return "LEGAL";
  if (!strcmp(size, "A5")) return "A5";
  return "A4";
}

static void page_setup(const cups_page_header2_t *h)
{
  unsigned dpi = h->HWResolution[0];
  printf("\033%%-12345X@PJL\n"
         "@PJL SET RAS1200MODE = FALSE\n"
         "@PJL SET RESOLUTION = %u\n"
         "@PJL SET ECONOMODE = OFF\n"
         "@PJL SET SOURCETRAY = AUTO\n"
         "@PJL SET MEDIATYPE = PLAIN\n"
         "@PJL SET PAPER = %s\n"
         "@PJL SET PAGEPROTECT = AUTO\n"
         "@PJL SET ORIENTATION = PORTRAIT\n"
         "@PJL ENTER LANGUAGE = PCL\n"
         "\033E\033&l%uX\033&u%uD\033*t%uR\033&l0S",
         dpi, paper_name(h->cupsPageSizeName), h->NumCopies ? h->NumCopies : 1, dpi, dpi);
}

int main(int argc, char **argv)
{
  static unsigned char a[MAX_WIDTH], b[MAX_WIDTH], enc[MAX_LINE];
  unsigned char *cur = a, *prev = b;
  cups_page_header2_t h;
  cups_raster_t *ras;
  char setup[256] = "", want[256];
  int fd = 0, pages = 0, failed = 0;
  unsigned y;

  if (argc != 6 && argc != 7) {
    fputs("ERROR: usage: rastertodcp7055 job user title copies options [file]\n", stderr);
    return 1;
  }
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: open");
    return 1;
  }
  signal(SIGTERM, on_term);
  signal(SIGPIPE, SIG_IGN);
  if (!(ras = cupsRasterOpen(fd, CUPS_RASTER_READ))) {
    fputs("ERROR: cannot read raster data\n", stderr);
    return 1;
  }

  while (!cancelled && !failed && cupsRasterReadHeader2(ras, &h)) {
    if (h.cupsBitsPerPixel != 1 || h.cupsNumColors != 1 || h.cupsHeight == 0 ||
        h.cupsBytesPerLine == 0 || h.cupsBytesPerLine > MAX_WIDTH) {
      fputs("ERROR: unsupported raster format\n", stderr);
      failed = 1;
      break;
    }
    if (!pages) {
      for (int i = 0; i < 128; i++) putchar(0);
      fputs("\033%-12345X@PJL\n@PJL JOB NAME=\"CUPS\"\n", stdout);
    }
    pages++;
    snprintf(want, sizeof want, "%u/%u/%s", h.HWResolution[0], h.NumCopies, h.cupsPageSizeName);
    if (strcmp(setup, want)) {
      page_setup(&h);
      strcpy(setup, want);
    }

    fputs("\033*b1030m", stdout);
    for (y = 0; y < h.cupsHeight && !cancelled; y++) {
      int n;
      if (cupsRasterReadPixels(ras, cur, h.cupsBytesPerLine) != h.cupsBytesPerLine) {
        fputs("ERROR: raster data ended early\n", stderr);
        failed = 1;
        break;
      }
      if (y % BLOCK_LINES == 0) flush_block();
      n = encode_line(enc, cur, block_lines ? prev : NULL, (int)h.cupsBytesPerLine);
      if (block_bytes + n > BLOCK_BYTES) {
        flush_block();
        n = encode_line(enc, cur, NULL, (int)h.cupsBytesPerLine);
      }
      memcpy(block + block_bytes, enc, n);
      block_bytes += n;
      block_lines++;
      unsigned char *t = cur; cur = prev; prev = t;
    }
    flush_block();
    fputs("1030M\f", stdout);
    fflush(stdout);
    fprintf(stderr, "PAGE: %d %u\n", pages, h.NumCopies ? h.NumCopies : 1);
  }

  if (pages)
    fputs("\033%-12345X@PJL\n@PJL EOJ NAME=\"CUPS\"\n\033%-12345X\n", stdout);
  else
    fputs("ERROR: no pages found\n", stderr);
  cupsRasterClose(ras);
  return fflush(stdout) || ferror(stdout) || failed || !pages;
}
