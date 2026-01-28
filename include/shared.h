#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "macros.h"

#define COLOR_BG   0xFF222222
#define COLOR_TEXT 0xFFFFFFFF

// RFC 3986
// Turns '/home/user/My File.txt' into 'file:///home/user/My%20File.txt\r\n'
char* CreateUriList(const char *path) {
  if (!path) return NULL;

  size_t len = strlen(path);
  char *output = malloc(len * 3 + 16); 
  if (!output) return NULL;

  char *p = output;
  p += sprintf(p, "file://");

  if (path[0] != '/') *p++ = '/';

  for (const char *s = path; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || 
        c == '_' || c == '~' || c == '/') {
      *p++ = c;
    } else {
      p += sprintf(p, "%%%02X", c);
    }
  }

  *p++ = '\r';
  *p++ = '\n';
  *p = '\0';
  return output;
}

typedef struct {
  char *uri;
  char *name;
} FileInfo;

void FileInfoFree(FileInfo *info) {
  if (!info) return;
  if (info->uri) free(info->uri);
  if (info->name) free(info->name);
  free(info);
}

FileInfo* CommandLineArguments(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s <file_path>\n", argv[0]);
    return NULL;
  }

  char *path = realpath(argv[1], NULL);
  if (!path) {
    LOG("Error resolving path %s", argv[1]);
    return NULL;
  }
  defer { free(path); };

  char *uri = CreateUriList(path);
  if (!uri) {
    LOG("Error creating uri");
    return NULL; 
  }
  defer { if (uri) free(uri); };

  FileInfo *result = calloc(1, sizeof(FileInfo));
  if (!result) {
    LOG("Memory allocation failed");
    return NULL; 
  }
  defer { if (result) FileInfoFree(result); };

  result->uri = uri;
  uri = NULL; 

  char *name_ptr = strrchr(path, '/');
  if (name_ptr) name_ptr++;
  else name_ptr = path;

  result->name = strdup(name_ptr);

  if (!result->name) {
    LOG("String duplication failed");
    return NULL;
  }

  LOG("Dragging: %s, Name: %s", result->uri, result->name);
 
  FileInfo *retval = result;
  result = NULL;

  return retval;
}
