/* --> fileno */
#define _POSIX_C_SOURCE 1
#include <stdio.h>
/* <-- */

#include <stdlib.h>
#include "debug.h"
#include <string.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

FILE *inoteDebugFile = NULL;
static enum DebugLevel inoteDebugLevel = LV_ERROR_LEVEL;
static int checkEnableCount = 0;
static void DebugFileInit();

int inoteDebugEnabled(enum DebugLevel level)
{
  if (!inoteDebugFile)
    DebugFileInit();

  return (inoteDebugFile && (level <= inoteDebugLevel)); 
}


void inoteDebugDisplayTime()
{
  struct timeval tv;
  if (!inoteDebugFile)
    DebugFileInit();

  if (!inoteDebugFile)
    return;
  
  gettimeofday(&tv, NULL);
  fprintf(inoteDebugFile, "%03ld.%06ld ", tv.tv_sec%1000, tv.tv_usec);
}


void inoteDebugDump(const char *label, uint8_t *buf, size_t size)
{
#define MAX_BUF_SIZE 1024 
  size_t i;
  char line[20];

  if (!buf || !label)
    return;

  if (size > MAX_BUF_SIZE)
    size = MAX_BUF_SIZE;

  if (!inoteDebugFile)
    DebugFileInit();
  
  if (!inoteDebugFile)
    return;
  
  memset(line ,0, sizeof(line));
  fprintf(inoteDebugFile, "%s", label);

  for (i=0; i<size; i++) {
    if (!(i%16)) {
      fprintf(inoteDebugFile, "  %s", line);
      memset(line, 0, sizeof(line));
      fprintf(inoteDebugFile, "\n%p  ", buf+i);
    }
    fprintf(inoteDebugFile, "%02x ", buf[i]);
    line[i%16] = isprint(buf[i]) ? buf[i] : '.';
  }

  fprintf(inoteDebugFile, "\n");
}


void DebugFileInit()
{
  FILE *fd = NULL;
  char c;
#define MAX_FILENAME 40
  char filename[MAX_FILENAME+1];
  mode_t old_mask;
  struct stat buf;
  
  if (checkEnableCount)
    return;

  checkEnableCount = 1;
  
  char *home = getenv("HOME");
  if (!home)
    return;
  
  if (snprintf(filename, MAX_FILENAME, "%s/%s", home, ENABLE_LOG) >= MAX_FILENAME)
    return;
  
  fd = fopen(filename, "r");
  if (!fd)
    return;
  
  inoteDebugLevel = LV_DEBUG_LEVEL;
  if (fread(&c, 1, 1, fd)) {
    uint32_t i = atoi(&c);
    if (i <= LV_DEBUG_LEVEL)
      inoteDebugLevel = i;
  }

  if (snprintf(filename, MAX_FILENAME, LIBINOTELOG, getpid()) >= MAX_FILENAME)
    goto exit0;

  /* the debug file must be read by the user only */
  unlink(filename);
  old_mask = umask(0077);
  inoteDebugFile = fopen(filename, "w");
  umask(old_mask);
  if (!inoteDebugFile)
    goto exit0;

  if (fstat(fileno(inoteDebugFile), &buf) || buf.st_mode & 0077) {
    err("mode=%o", buf.st_mode);
    fclose(inoteDebugFile);
    inoteDebugFile = NULL;
    goto exit0;
  }
  
  setbuf(inoteDebugFile, NULL);

 exit0:
  if (fd)
    fclose(fd);    
}


void inoteDebugFileFinish()
{
  if (inoteDebugFile)
    fclose(inoteDebugFile);  

  inoteDebugFile = NULL;
  checkEnableCount = 0;
}

/* local variables: */
/* c-basic-offset: 2 */
/* end: */
