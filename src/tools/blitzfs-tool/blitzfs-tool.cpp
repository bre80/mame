// license:BSD-3-Clause
/***************************************************************************

    Blitz (97,99,2K) CHD/Filesystem Tool

****************************************************************************/
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <functional>

#include "chd.h"
#include "blitzfs.h"
#include "blitzcksum.h"


using namespace std;




////////////////////// PARSE IMPORT FILE //////////////////////

enum class ParseType {
  INVALID,
  UPDATE,
  APPEND
};

struct ParseState
{
  ParseType type = ParseType::INVALID;
  string line;
};

struct ParseResult
{
  bool success = false;

  struct {
    int lineno = 0;
    string line;
  } err;
};


static void trimstr(char *str)
{
  auto len = strlen(str);
  int count = 0;
  for (int i = 0; i < len; ++i) {
    if (isspace(str[i]))
      count++;
    else
      break;
  }

  if (count > 0) {
    memmove(str, str+count, len-count);
    str[len-count] = '\0';
  }

  len -= count;
  auto *p = str + len - 1;
  for (int i = 0; i < len; ++i) {
    if (isspace(*p))
      *p = '\0';
    else
      break;
    --p;
  }
}


bool process_line(char *line, ParseState &state)
{
  bool success = true;
  // remove comment
  char *comment = strchr(line, '#');
  if (comment) {
    *comment = '\0';
    comment++;
  }

  trimstr(line);

  state.line = "";

  if (line[0] == '[')
  {
    state.line = "";
    if (!strcmp(line, "[update]"))
      state.type = ParseType::UPDATE;
    else if (!strcmp(line, "[append]"))
      state.type = ParseType::APPEND;
    else {
      state.type = ParseType::INVALID;
      state.line = line;
      success = false;
    }
  }
  else
  {
    char *p = nullptr;
    bool empty = true;

    for (int i = 0; empty && i < strlen(line); ++i)
    {
      empty = isspace(line[i]);
      if (!empty && !p)
        p = line+i;
    }

    if (p)
      state.line = p;
  }

  return success;
}


struct ImportFile {
  string path;
  string name;
};

ImportFile parseImportFile(std::string name)
{
  ImportFile file;

  file.name = name;

  auto pos = name.find_last_of("/");
  if (pos != std::string::npos)
  {
    file.path = name.substr(0,pos);
    file.name = name.substr(pos+1);
  }
  return file;
}

struct Imports
{
  vector<ImportFile> update;
  vector<ImportFile> append;
};

ParseResult parse_import_file(const string &filename, Imports &imports)
{
  ParseResult result;

  FILE *file = fopen(filename.c_str(), "r");
  if (!file)
  {
    result.err.line = "Failed to open file: " + filename;
    perror(filename.c_str());
    return result;
  }

  bool success = true;

  ParseState state;
  int lineno = 1;
  while (!feof(file) && success)
  {
    char *line = NULL;
    size_t size = 0;
    if (::getline(&line, &size, file) > 0)
    {
      auto len = strlen(line);
      if (len > 1 && line[len-1] == '\n') {
        line[len-1] = '\0';
      }

      result.err.line = line;
      result.err.lineno = lineno;

      success = process_line(line, state);
      if (success)
      {
        switch(state.type)
        {
          case ParseType::UPDATE:
          if (!state.line.empty())
            imports.update.push_back(parseImportFile(state.line));
          break;

          case ParseType::APPEND:
          if (!state.line.empty())
            imports.append.push_back(parseImportFile(state.line));
          break;

          default: break;
        }
      }
    }

    if (line) free(line);

    ++lineno;
  }
  fclose(file);

  result.success = success;
  return result;
}


////////////////////// PARSE IMPORT FILE: END //////////////////////










using namespace std;
using blitz::BlitzFS;

//////////////////////////////
struct Args {
  string chd;
  string action;
  vector<string> args;
};

static void usage();

//////////////////////////////
static bool parse_args(Args &args, int argc, char **argv)
{
  string arg;
  if (argc < 3)
    return false;

  args.chd = argv[1];
  args.action = argv[2];

  for (int i = 3; i < argc; ++i) {
    args.args.push_back(argv[i]);
  }

  return true;
}


static const char* MONTHS[] = {
  "---",
  "JAN",
  "FEB",
  "MAR",
  "APR",
  "MAY",
  "JUN",
  "JUL",
  "AUG",
  "SEP",
  "OCT",
  "NOV",
  "DEC"
};

//////////////////////////////
struct Action {
  string name;
  string desc;
  bool writable;
  std::function<int(BlitzFS&, vector<string> &args)> handler;
};

//////////////////////////////
struct Action ACTIONS[] = {

  //////////////////////////////
  {
    "repair",
    "repair Blitz2K filesystem: AUDITS.FMT/ADJUST.FMT file entry fix",
    true,
    [](BlitzFS &fs, vector<string> &args) -> int {

      bool ok = fs.repair();
      printf("FILE SYSTEM REPAIRED: %s\n", ok ? "OK" : "FAILED");
      return 0;
    }
  },

  //////////////////////////////
  {
    "info",
    "show information about the filesystem/CHD",
    false,
    [](BlitzFS &fs, vector<string> &args) -> int {

      auto &chd = fs._chd();

      fprintf(stderr, "SHA1: %s\n", chd.sha1().as_string().c_str());
      fprintf(stderr, "RAW SHA1: %s\n", chd.raw_sha1().as_string().c_str());
      fprintf(stderr, "PARENT SHA1: %s\n", chd.parent_sha1().as_string().c_str());
      fprintf(stderr, "Logical Size: %llu bytes (%.2f GB)\n", chd.logical_bytes(), chd.logical_bytes() / (float)(0x40000000));
      fprintf(stderr, "\n");

      fprintf(stderr, "DISK SIZE: %llu (%llu MB)\n", fs.totalSize(), fs.totalSize() / 1024 / 1024);
      fprintf(stderr, "USED SIZE: %llu (%llu MB)\n", fs.usedSize(),fs.usedSize()  / 1024 / 1024);
      fprintf(stderr, "Free Space: %.2f%%  (%llu bytes free)\n",
        100 - ((float)fs.usedSize() / fs.totalSize()) * 100,
        fs.totalSize() - fs.usedSize());
      fprintf(stderr, "\n");

      using blitz::Version;

      const char *verstr = "(unknown)";
      switch(fs.version())
      {
        case Version::BLITZ97: verstr = "Blitz97"; break;
        case Version::BLITZ99: verstr = "Blitz99"; break;
        case Version::BLITZ2K: verstr = "Blitz2K"; break;
        default: break;
      }

      fprintf(stderr, "Version: %s\n", verstr);
      fprintf(stderr, "%lu files found\n", fs.entries().size());

      return 0;
    }
  },


  #define _TIME_FMT "%s %2d,%04d  %02d:%02d:%02d"
  #define _TIME_VAR(e) \
      MONTHS[(e).ff_fdate_month], \
      (e).ff_fdate_day, \
      ( (e).ff_fdate_year > 0 ? 1980 + (e).ff_fdate_year : 0 ), \
      (e).ff_ftime_hours, \
      (e).ff_ftime_min, \
      (e).ff_ftime_sec*2


  //////////////////////////////
  {
    "list",
    "list files",
    false,
    [](BlitzFS &fs, vector<string> &args) -> int {

      size_t count = 0;



      printf("  TOC  EntryAddr   CKSUM       Filename            Timestamp         Block          Filesize\n");
      printf("----------------+---------+----------------+-----------------------+---------+-----------------------\n");
      for (auto &entry : fs.entries())
      {
        auto name = entry.name;

        bool show = true;

        if(args.size() > 0)
        {
          show = false;
          for (auto &arg : args) {
            auto a = arg;
            std::transform(a.begin(), a.end(), a.begin(),::toupper);
            show = (name.find(a) != std::string::npos);
            if (show) break;
          }
        }


        if (show)
        {
          std::string err;
          char cksumstr[9] = "        ";
          uint32_t cksum = 0;
          if (fs.readFileCksum(entry.name, cksum, err))
            snprintf(cksumstr, sizeof(cksumstr), "%08X", cksum);

          printf("[%2X,%2X] %08llX: %s  %-15s: "  _TIME_FMT ", %08X, %08X\t(%d bytes)\n",
            entry.meta.toc,
            entry.meta.index,
            entry.meta.addr,
            cksumstr,
            entry.name.c_str(),
            _TIME_VAR(entry), // entry.timestamp,
            entry.block,
            entry.filesize,
            entry.filesize*4);
          ++count;
        }
      }
      printf("\n");
      auto size = fs.entries().size();
      if (count == size)
        printf("%lu file%s.\n", size, size>1?"s":"");
      else
        printf("%lu file%s found.  (%lu total file%s)\n", count, count>1?"s":"", size, size>1?"s":"");
      return 0;
    }
  },

  //////////////////////////////
  {
    "extract",
    "extract files from CHD disk image, usage: <out-dir> <file> [<file>]",
    false,
    [](BlitzFS &fs, vector<string> &args) -> int {

      if (args.size() < 2) {
        fprintf(stderr, "usage: <out-dir> <file> [<file>]\n");
        return 1;
      }

      int result = 0;
      string outdir = args[0];

      for (int i = 1; i < args.size(); ++i)
      {
        auto file = args[i];
        std::string err;
        if(fs.extract(file, outdir, err)) {
          printf("File Extracted: %s/%s\n", outdir.c_str(), file.c_str());
        }
        else {
          fprintf(stderr, "%s\n", err.c_str());
          result = 1;
        }
      }
      return result;
    }
  },


  //////////////////////////////
  {
    "import",
    "import files into CHD disk image: usage <import.cfg>",
    true,
    [](BlitzFS &fs, vector<string> &args) -> int {

      if (args.size() != 1) {
        fprintf(stderr, "usage: <import.cfg>\n");
        return 1;
      }

      int ret = 0;
      struct Imports imports;

      auto result = parse_import_file(args[0], imports);
      if (result.success)
      {
        if (imports.update.size() > 0)
          printf("Updating Files:\n");
        for (auto &file : imports.update)
        {
          std::string err;
          std::string path = file.path.empty() ? file.name : file.path+"/"+file.name;
          if (fs.update(file.name, file.path, err)) {
            printf("File Updated: %-15s  [%s]\n", file.name.c_str(), path.c_str());
          }
          else {
            fprintf(stderr, "%s\n", err.c_str());
            ret = 1;
          }
        }

        if (imports.append.size() > 0)
        {
          if (fs.prepareAppend())
          {
            printf("Appending Files:\n");
            for (auto &file : imports.append)
            {
              std::string err;
              std::string path = file.path.empty() ? file.name : file.path+"/"+file.name;
              if (fs.append(file.name, file.path, err)) {
                printf("File Appended: %-15s  [%s]\n", file.name.c_str(), path.c_str());
              }
              else {
                fprintf(stderr, "%s\n", err.c_str());
                ret = 1;
              }
            }
          }
          else {
            fprintf(stderr, "Failed to prepare FS to append files\n");
            ret = 1;
          }
        }
      }
      else
      {
        if (result.err.lineno == 0)
          fprintf(stderr, "Error: %s\n", result.err.line.c_str());
        else
          fprintf(stderr, "Error around line %d: %s\n", result.err.lineno, result.err.line.c_str());
        ret = 1;
      }
      return ret;
    }
  }
};


//////////////////////////////
static void usage()
{
  fprintf(stderr, "usage: <chd-file> <action> [args]\n");
  fprintf(stderr, "actions:\n");
  for (auto &action: ACTIONS)
  {
    fprintf(stderr, "  %15s: %s\n", action.name.c_str(), action.desc.c_str());
  }
}


//////////////////////////////
int main(int argc, char **argv)
{
  int result = 0;

  Args args;

  if (!parse_args(args, argc, argv)) {
    usage();
    return 1;
  }

  Action *action = nullptr;
  for (auto &a : ACTIONS)
  {
    if(args.action == a.name)
    {
      action = &a;
      break;
    }
  }

  if (!action)
  {
    fprintf(stderr, "Unknown action: %s\n", args.action.c_str());
    usage();
    return 1;
  }

  chd_file file;
  string chdpath = args.chd;

  auto err = file.open(chdpath.c_str(), action->writable);
  if (err == CHDERR_NONE)
  {
    blitz::BlitzFS fs(file);
    if (!fs.init()) {
      fprintf(stderr, "Failed to initialize Blitz FileSystem\n");
      return 1;
    }

    result = action->handler(fs, args.args);
  }
  else
  {
    fprintf(stderr, "Failed to open CHD, code=%d (%s)\n", (int)err, chd_file::error_string(err));
    if (action->writable && file.compressed())
      fprintf(stderr, "Unable to open compressed CHD as writable\n");
    result = 1;
  }

  return result;
}