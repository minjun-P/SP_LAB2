//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     FALL 2025
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author 박민준
/// @studid 2018-14751
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

/// @brief output control flags
#define F_DEPTH    0x1        ///< print directory tree
#define F_Filter   0x2        ///< pattern matching

/// @brief maximum numbers
#define MAX_DIR 64            ///< maximum number of supported directories
#define MAX_PATH_LEN 1024     ///< maximum length of a path
#define MAX_DEPTH 20          ///< maximum depth of directory tree (for -d option)
int max_depth = MAX_DEPTH;    ///< maximum depth of directory tree (for -d option)
#define MAX_PATH_DISPLAY_LEN 54
#define MAX_PATH_DISPLAY_LEN_SUMMARY 68

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};

/// @brief print strings used in the output
const char *print_formats[8] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "Invalid pattern syntax",
  "%-68s   %14llu %9llu\n", // Summary line
};

const char* plural(int count, const char* singular, const char* plural) {
    return (count == 1) ? singular : plural;
}
const char* summary_line(struct summary* stats) {
    static char buffer[100];
    snprintf(buffer, sizeof(buffer), "%d %s, %d %s, %d %s, %d %s, and %d %s",
             stats->files, plural(stats->files, "file", "files"),
             stats->dirs, plural(stats->dirs, "directory", "directories"),
             stats->links, plural(stats->links, "link", "links"),
             stats->fifos, plural(stats->fifos, "pipe", "pipes"),
             stats->socks, plural(stats->socks, "socket", "sockets"));
    if (strlen(buffer) > MAX_PATH_DISPLAY_LEN_SUMMARY) {
        buffer[MAX_PATH_DISPLAY_LEN_SUMMARY - 3] = '.';
        buffer[MAX_PATH_DISPLAY_LEN_SUMMARY - 2] = '.';
        buffer[MAX_PATH_DISPLAY_LEN_SUMMARY - 1] = '.';
        buffer[MAX_PATH_DISPLAY_LEN_SUMMARY] = '\0';
    }
    return buffer;
}



char filetype_char(mode_t mode) {
    if (S_ISREG(mode))  return ' ';
    if (S_ISDIR(mode))  return 'd';
    if (S_ISLNK(mode))  return 'l';
    if (S_ISFIFO(mode)) return 'f';
    if (S_ISSOCK(mode)) return 's';
    return ' ';
}

const char* pattern = NULL;  ///< pattern for filtering entries

/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
/// @param format optional format string (printf format) or NULL
void panic(const char* msg, const char* format)
{
  if (msg) {
    if (format) fprintf(stderr, format, msg);
    else        fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}




/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *get_next(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}


/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sort by name
  return strcmp(e1->d_name, e2->d_name);
}

// Filtering 코드
bool match(const char* str, const char* pattern) {
  while(*str != '\0') {
    if (submatch(str, pattern)) {
      return true;
    }
    str++;
  }
}

bool submatch(const char* s, const char* p) {
  if (*s == '\0' && *p == '\0') return true;
  if (*s != *p) return false;
  
  return submatch(s+1, p+1);
}

/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  // 1. 폴더 열기
  DIR *dir = opendir(dn);
  if (!dir) {
    perror("opendir");
    return;
  }

  const int depth = strlen(pstr) / 2;
  if (depth > max_depth) {
    closedir(dir);
    return;
  }



  // 2. 디렉토리 엔트리 읽기
  struct dirent *entry;
  
  int entry_count = 0;
  int capacity = 10;
  struct dirent* entry_list = malloc(sizeof(struct dirent) * capacity);

  if (!entry_list) {
    closedir(dir);
    panic("Memory allocation failed", NULL);
  }
  while ((entry = get_next(dir)) != NULL) {
    if (entry_count >= capacity) {
      capacity *= 2;
      struct dirent *tmp = realloc(entry_list, sizeof(struct dirent) * capacity);
      if (!tmp) {
        free(entry_list);
        closedir(dir);
        panic("Memory allocation failed", NULL);
      }
      entry_list = tmp;
    }
    entry_list[entry_count++] = *entry;
  }
  // 정렬
  qsort(entry_list, entry_count, sizeof(struct dirent), dirent_compare);
  // 3. 엔트리 출력
  for (int i = 0; i < entry_count; i++) {
    struct dirent target = entry_list[i];
    struct stat st;
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", dn, target.d_name);
    lstat(full_path, &st);
    if (S_ISDIR(st.st_mode)) stats->dirs++;
    else if (S_ISREG(st.st_mode)) stats->files++;
    else if (S_ISLNK(st.st_mode)) stats->links++;
    else if (S_ISFIFO(st.st_mode)) stats->fifos++;
    else if (S_ISSOCK(st.st_mode)) stats->socks++;
    stats->size += st.st_size;
    stats->blocks += st.st_blocks;
    char name_with_prefix[MAX_PATH_LEN];
    snprintf(name_with_prefix, sizeof(name_with_prefix), "%s%s", pstr, target.d_name);
    size_t len = strlen(name_with_prefix);
    if (len > MAX_PATH_DISPLAY_LEN) {
      name_with_prefix[MAX_PATH_DISPLAY_LEN - 3] = '.';
      name_with_prefix[MAX_PATH_DISPLAY_LEN - 2] = '.';
      name_with_prefix[MAX_PATH_DISPLAY_LEN - 1] = '.';
      name_with_prefix[MAX_PATH_DISPLAY_LEN] = '\0';
    }

    
    printf(print_formats[2], name_with_prefix, getpwuid(st.st_uid)->pw_name, getgrgid(st.st_gid)->gr_name, st.st_size, st.st_blocks, filetype_char(st.st_mode));
    if (S_ISDIR(st.st_mode)) {
      char new_prefix[MAX_PATH_LEN];
      snprintf(new_prefix, sizeof(new_prefix), "%s  ", pstr);
      char full_path[MAX_PATH_LEN];
      snprintf(full_path, sizeof(full_path), "%s/%s", dn, target.d_name);
      process_dir(full_path, new_prefix, stats, flags);
    }
  }
  closedir(dir);
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports \'?\', \'*\', and \'()\')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DEPTH, MAX_DIR);

  exit(EXIT_FAILURE);
}



/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat = { 0 }; // a structure to store the total statistics
  unsigned int flags = 0;

  //
  // parse arguments
  //

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if (!strcmp(argv[i], "-d")) {
        flags |= F_DEPTH;
        if (++i < argc && argv[i][0] != '-') {
          max_depth = atoi(argv[i]);
          if (max_depth < 1 || max_depth > MAX_DEPTH) {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        } 
        else {
          syntax(argv[0], "Missing depth value argument.");
        }
      }
      else if (!strcmp(argv[i], "-f")) {
        if (++i < argc && argv[i][0] != '-') {
          flags |= F_Filter;
          pattern = argv[i];
        }
        else {
          syntax(argv[0], "Missing filtering pattern argument.");
        }
      }
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      }
      else {
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  printf("Max Depth : %d\n", max_depth); // for debugging

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;

  // Directory 순회 시작
  for (int i =0; i<ndir; i++) {
    printf("%s", print_formats[0]);
    printf("%s", print_formats[1]);
    const char *curr_dir = directories[i];
    printf("%s\n", curr_dir);
    struct summary dstat = {0}; // directory statistics
    process_dir(curr_dir, "  ", &dstat, flags);
    printf("%s", print_formats[1]);

    // aggregate statistics
    tstat.dirs   += dstat.dirs;
    tstat.files  += dstat.files;
    tstat.links  += dstat.links;
    tstat.fifos  += dstat.fifos;
    tstat.socks  += dstat.socks;
    tstat.size   += dstat.size;
    tstat.blocks += dstat.blocks;
  
    printf(print_formats[4], summary_line(&dstat), dstat.size, dstat.blocks);
    if (ndir >1) {
      printf("\n");
    }
  }

  if (ndir > 1) {
    printf("Analyzed %d directories:\n"
      "  total # of files:        %16d\n"
      "  total # of directories:  %16d\n"
      "  total # of links:        %16d\n"
      "  total # of pipes:        %16d\n"
      "  total # of sockets:      %16d\n"
      "  total # of entries:      %16d\n"
      "  total file size:         %16llu\n"
      "  total # of blocks:       %16llu\n",
      ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks,
      tstat.files + tstat.dirs + tstat.links + tstat.fifos + tstat.socks, 
      tstat.size, tstat.blocks);
  }

  //
  // that's all, folks!
  //
  return EXIT_SUCCESS;
}

