#include "blitzfs.h"
#include "blitzcksum.h"
#include <cstdio>
#include <vector>
#include <functional>
#include <sys/stat.h>
#include "chd.h"

using blitz::BlitzFS;
using BlitzFile = BlitzFS::BlitzFile;

//#if defined(BLITZ_99) || defined(BLITZ_2K)
//#define ENABLE_CRC // BLITZ99, BLITZ2K
//#endif

// special files
#define GAMEREV_INF "GAMEINF.REV"
#define AUDITS_FMT  "AUDITS.FMT"
#define ADJUST_FMT  "ADJUST.FMT"

#define SWAP32(n) ((((n) >> 24) & 0xff) |    \
                   (((n) << 8) & 0xff0000) | \
                   (((n) >> 8) & 0xff00) |   \
                   (((n) << 24) & 0xff000000));

#define FILES_PER_TOC (170)
#define SIZEOF_FILE_ENTRY (12 + 3 * 4)
#define BLITZFZ_BLOCKSIZE (0x1000)
#define BLITZFS_BLOCKADDR(n) (m_toc + ((n)-3) * BLITZFZ_BLOCKSIZE)
#define ALIGN_NEXT_BLOCKADDR(a) ((((a) + BLITZFZ_BLOCKSIZE*3) & ~(BLITZFZ_BLOCKSIZE-1)) - m_toc)
#define ALIGN_NEXT_BLOCK(a) ((ALIGN_NEXT_BLOCKADDR(a) / BLITZFZ_BLOCKSIZE)+3)


static bool check_extension(const std::string &s, const std::string ext)
{
  bool found = false;
  auto pos = s.find_last_of(".");
  if (pos == s.size()-ext.size()-1)
    found = s.substr(pos+1) == ext;
  return found;
}




//////////////////////////////
BlitzFS::BlitzFS(chd_file &file)
: m_file(file), m_toc(0)
{
}

//////////////////////////////
bool BlitzFS::init()
{
  m_entries.clear();

  uint32_t magic1 = 0, magic2 = 0;
  (void)m_file.read_bytes(0x000, &magic1, 4);
  (void)m_file.read_bytes(0x600, &magic2, 4);

  magic1 = SWAP32(magic1);
  magic2 = SWAP32(magic2);

  if (magic1 != 0x54524150 || magic2 != 0x80120134)
    return false;

  // There is a checksum, info block at offset $600
  // First TOC is located at offset $60C = value,  TOC at (value+1)*0x200
  uint32_t toc = 0;
  (void)m_file.read_bytes(0x60c, &toc, 4);
  m_toc = (toc+1) * 0x200;

  uint64_t addr = m_toc;

  int tocIndex = 0;
  bool reading = true;
  while (reading)
  {

    FileEntry entry;
    for (int i = 0; i < FILES_PER_TOC; ++i)
    {
      if (readEntry(addr, entry))
      {
        entry.meta.toc = tocIndex;
        entry.meta.index = i;
        m_entries.push_back(entry);
      }

      addr += SIZEOF_FILE_ENTRY;
    }

    uint32_t data[4];
    m_file.read_bytes(addr, data, sizeof(data));
    if (data[3])
    {
      addr = BLITZFS_BLOCKADDR(data[3]); // address of next TOC table
      ++tocIndex;
    }
    else
      reading = false;
  }

  if (m_entries.size() > 0)
  {
    // These three files must exist.
    auto *gameinf = findFile(GAMEREV_INF);
    if (gameinf)
    {
      std::string err;
      BlitzFile file;
      if (readFile(gameinf->name, file, err))
      {
        uint8_t *data = file.bytes.data()+4;
        const char *BLITZ2K = "NFL Blitz 2000";
        const char *BLITZ99 = "Blitz 99";
        const char *BLITZ97 = "NFL BLITZ 1";
        if (!memcmp(data, BLITZ2K, strlen(BLITZ2K))) {
          m_hasCksum = true;
          m_version = Version::BLITZ2K;
        }
        else if (!memcmp(data, BLITZ99, strlen(BLITZ99))) {
          m_hasCksum = true;
          m_version = Version::BLITZ99;
        }
        else if (!memcmp(data, BLITZ97, strlen(BLITZ97))) {
          m_hasCksum = false;
          m_version = Version::BLITZ97;
        }
      }
    }
//    printf("HAS_CKSUM: %s\n", m_hasCksum ? "YES" : "NO");
  }

  return m_entries.size() > 0;
}

//////////////////////////////
bool BlitzFS::readEntry(uint64_t addr, FileEntry &entry) const
{
  bool valid = false;

  char name[13] = {0};
  m_file.read_bytes(addr, name, 12);
  uint32_t *pname = (uint32_t *)name; // swap words
  for (int i = 0; i < 3; ++i)
  {
    pname[i] = SWAP32(pname[i]);
  }

  if (isprint(name[0]))
  {
    valid = true;
    entry.meta.addr = addr;
    entry.name.assign(name);
    addr += 12;
    m_file.read_bytes(addr, &entry.filesize, 4);
    addr += 4;
    m_file.read_bytes(addr, &entry.timestamp, 4);
    addr += 4;
    m_file.read_bytes(addr, &entry.block, 4);
    addr += 4;
  }

  return valid;
}

//////////////////////////////
bool BlitzFS::writeEntry(const FileEntry &entry)
{
  bool success = false;

  if (entry.meta.addr > 0)
  {
    char name[13] = { 0 };
    strncpy(name, entry.name.c_str(), sizeof(name));

    uint32_t *pname = (uint32_t *)name; // swap words
    for (int i = 0; i < 3; ++i)
    {
      pname[i] = SWAP32(pname[i]);
    }

    auto addr = entry.meta.addr;

    success = true;
    // write name
    success = success && m_file.write_bytes(addr, name, 12) == CHDERR_NONE;
    addr += 12;
    success = success && m_file.write_bytes(addr, &entry.filesize, 4) == CHDERR_NONE;
    addr += 4;
    success = success && m_file.write_bytes(addr, &entry.timestamp, 4) == CHDERR_NONE;
    addr += 4;
    success = success && m_file.write_bytes(addr, &entry.block, 4) == CHDERR_NONE;
    addr += 4;
  }

  return success;
}


//////////////////////////////
bool BlitzFS::updateTOC()
{
  bool success = true;

  for (auto &entry : m_entries)
  {
    success = writeEntry(entry);
    if (!success)
      break;
  }

  return success;
}

//////////////////////////////
// case-insensitive
auto BlitzFS::findFile(const std::string &name) const -> const FileEntry*
{
  return const_cast<BlitzFS*>(this)->findFile(name);
}

// case-insensitive
auto BlitzFS::findFile(const std::string &name) -> FileEntry*
{
  FileEntry *entry = nullptr;

  std::string upperName = name;
  std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

  for (auto &e : m_entries)
  {
    if (e.name == upperName)
    {
      entry = &e;
      break;
    }
  }
  return entry;
}

//////////////////////////////
bool BlitzFS::repair()
{
  bool success = false;

  if (version() == Version::BLITZ2K)
  {
    // fix a know problem in blitz2k.chd
    // filesystem fix: ADJUST.FMT/AUDITS.FMT
//    printf("Updating ADJUST.FMT\n");
    auto *audits = findFile(AUDITS_FMT);
    auto *adjust = findFile(ADJUST_FMT);
    if (audits && adjust)
    {
      success = true;
      if (audits->block == adjust->block)
      {
        adjust->block = audits->block + 0x10; // this should reserve plenty of space for adjust.fmt to grow
        success = updateTOC();
      }
    }
  }

  return success;
}

//////////////////////////////
bool BlitzFS::readFileCksum(const std::string &fileName, uint32_t &cksum, std::string &err)
{
  bool valid = false;

  auto *entry = findFile(fileName);
  if (entry)
  {
    if (hasCksum(fileName))
    {
      // read first 4 bytes
      uint64_t addr = BLITZFS_BLOCKADDR(entry->block);
      auto chderr = m_file.read_bytes(addr, &cksum, 4);
      if (chderr == CHDERR_NONE)
        valid = true;
      else
        err = chd_file::error_string(chderr);
    }
    else
      err = "File doesn't contain a checksum";
  }
  else
    err = "Failed to locate file: " + fileName;

  return valid;
}


//////////////////////////////
bool BlitzFS::readFile(const std::string &fileName, BlitzFile &file, std::string &err)
{
  bool success = false;

  auto *entry = findFile(fileName);
  if (entry)
  {
    bool removeCksum = hasCksum(entry->name);

    // read the file bytes from the FS
    size_t size = (entry->filesize * 4);
    uint64_t addr = BLITZFS_BLOCKADDR(entry->block);
    if (removeCksum)
    {
      size -= 4;
      addr += 4;  // get start of file after checksum
    }
    file.bytes.resize(size);
    auto chderr = m_file.read_bytes(addr, file.bytes.data(), file.bytes.size());
    if (chderr == CHDERR_NONE)
    {
      file.name = entry->name;
      success = true;
    }
    else
      err = chd_file::error_string(chderr);
  }
  else
    err = "Failed to locate file: " + fileName;

  return success;
}

//////////////////////////////
// update file and checksum
bool BlitzFS::writeFile(const BlitzFile &file, std::string &err)
{
  bool success = false;
  auto *entry = findFile(file.name);
  if (entry)
  {
    bool prependCksum = hasCksum(entry->name);
    auto size = file.bytes.size();

    auto fileSize = (entry->filesize * 4);
    if (prependCksum)
      fileSize -= 4;

    if (size <= fileSize)
    {
      if (size != fileSize) {
        auto diff = fileSize - size;
        fprintf(stderr, "WARN: filesize mismatch: expected %u found %ld (padding %ld byte%s)\n", fileSize, size, diff, diff>1?"s":"");
      }

      uint64_t addr = BLITZFS_BLOCKADDR(entry->block);

      if (prependCksum)
      {
       auto cksum = compute_checksum(file.bytes.data(), file.bytes.size());
        m_file.write_bytes(addr, &cksum, 4); // write file checksum
        addr += 4;
      }

      (void)m_file.write_bytes(addr, file.bytes.data(), file.bytes.size()); // write file bytes

      addr += file.bytes.size();
      // zero fill bytes if necessary
      for (auto i = size; i < fileSize; ++i)
      {
        uint8_t zero = 0;
        (void)m_file.write_bytes(addr++, &zero, 1);
      }

      success = true;
    }
    else
      err = "Filesize mismatch";
  }
  else
    err = "Unable to locate file: " + file.name;

  return success;
}

//////////////////////////////
bool BlitzFS::extract(const std::string &fileName, const std::string &outdir, std::string &err)
{
  bool success = false;

  BlitzFile file;
  if (readFile(fileName, file, err))
  {
    auto path = outdir + "/" + file.name;
    FILE *f = fopen(path.c_str(), "w");
    if (f)
    {
      success = (fwrite(file.bytes.data(), file.bytes.size(), 1, f) == 1);
      if (!success)
        err = "Failed to write bytes to file: " + path;
      fclose(f);
    }
    else
      err = "Failed to open file: " + path;
  }

  return success;
}

//////////////////////////////
bool BlitzFS::update(const std::string &fileName, const std::string &indir, std::string &err)
{
  bool success = false;

  auto *entry = findFile(fileName);
  if (entry)
  {
    auto path = indir.empty() ? entry->name : indir + "/" + entry->name;
    FILE *f = fopen(path.c_str(), "r");
    if (f)
    {
      fseek(f, 0, SEEK_END);
      auto size = ftell(f);
      fseek(f, 0, SEEK_SET);

      auto fileSize = (entry->filesize * 4);
      if (hasCksum(entry->name))
        fileSize -= 4;

      if (size <= fileSize)
      {
        struct stat st;
        if (stat(path.c_str(), &st) == 0)
        {
          struct tm *t = localtime(&st.st_mtime);
          if (t)
          {
            entry->ff_ftime_sec   = t->tm_sec / 2;
            entry->ff_ftime_min   = t->tm_min;
            entry->ff_ftime_hours = t->tm_hour;

            entry->ff_fdate_day   = t->tm_mday;
            entry->ff_fdate_month = t->tm_mon+1;
            entry->ff_fdate_year  = t->tm_year - 2000;
            writeEntry(*entry);
          }
        }

        BlitzFile file;
        file.name = entry->name;
        file.bytes.resize(size);
        if (fread(file.bytes.data(), file.bytes.size(), 1, f) == 1)
        {
          success = writeFile(file, err);
        }
        else
          err = "Failed reading source file";
      }
      else
        err = "Filesize mismatch, source is too large";

      fclose(f);
    }
    else
      err = "Failed to open file: " + path;
  }
  else
    err = "Failed to locate file: " + fileName;

  return success;
}


//////////////////////////////
bool BlitzFS::prepareAppend()
{
  auto it = std::find_if(m_entries.begin(), m_entries.end(),[](auto &e) -> bool {
    return e.name == ADJUST_FMT;
  });

  // this file MUST exist!
  if (it == m_entries.end()) {
    return false;
  }

  // clear out previously appended entries
  for (auto it2 = it+1; it2 != m_entries.end(); ++it2)
  {
    auto &e = *it2;
    e.name = "";
    e.filesize = 0;
    e.block = 0;
    e.timestamp = 0;
  }

  // update the TOC and remove files from our internal entries list
  bool success = false;
  if (updateTOC())
  {
    m_entries.erase(it+1, m_entries.end());
    success = true;
  }

  return success;
}

//////////////////////////////
bool BlitzFS::append(const std::string &fileName, const std::string &indir,  std::string &err)
{
  auto path = indir.empty() ? fileName : indir + "/" + fileName;

  if (fileName.length() > 12) {
    err = "Filename is too long, must be no more than 12 characters: " + fileName;
    return false;
  }

  struct stat st;
  if(stat(path.c_str(), &st) != 0)
  {
    err = "Unable to get filesize for file: " + path;
    return false;
  }

  uint32_t filesize = st.st_size;

  auto e = m_entries.back();
//  printf("Appending %s after %s\n", fileName.c_str(), e.name.c_str());

  bool success = false;

  if (e.meta.index < (FILES_PER_TOC-1))
  {
    FileEntry entry;
    entry.meta.addr = e.meta.addr + SIZEOF_FILE_ENTRY;
    entry.meta.index = e.meta.index + 1;
    entry.meta.toc = e.meta.toc;
    entry.name = fileName;
    std::transform(entry.name.begin(), entry.name.end(), entry.name.begin(), ::toupper);

    // four-byte align
    if (filesize & 0x3)
      filesize = (filesize & ~3) + 4;

    entry.filesize = (filesize / 4); // TODO: pad to next word boundary  (add 4 bytes for checksum)

    if (hasCksum(fileName))
      entry.filesize += 1;


    uint64_t blockAddr = BLITZFS_BLOCKADDR(e.block) + e.filesize*4;
//    printf("e.block: %d (0x%x), addr=%llu,0x%llx\n", e.block, e.block, BLITZFS_BLOCKADDR(e.block),BLITZFS_BLOCKADDR(e.block));
//    printf("e.filesize: %d, bytes=%d  (origsize=%d 0x%x)\n", entry.filesize, entry.filesize*4, filesize, filesize);
//    printf("blockAddr: %llu, 0x%llx\n", blockAddr, blockAddr);
    entry.block = ALIGN_NEXT_BLOCK(blockAddr);
//    printf("entry.block: %d (0x%x)\n", entry.block, entry.block);
    // TODO: special case, if e is ADJUST_FMT, add a bit more padding as this file can be variable size
    entry.timestamp = 0; // TODO: create timestamp from sturuct struct stat buffer: st_mtime/st_mtimespec

    // add the new entry
    m_entries.push_back(entry);

    success = this->writeEntry(entry);
    if (success)
      success = update(fileName, indir, err);
  }
  else
  {
    err = "Too many files added to TOC entry, extending new TOC entry not yet implemented";
    success = false;
  }

  return success;
}


//////////////////////////////
bool BlitzFS::hasCksum(const std::string &name) const
{
  bool hasCksum = m_hasCksum;

  std::string upperName = name;
  std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

  if (hasCksum)
  {
    // exclude these extensions
    for (auto &ext : { "FMT","ENV","REV","SND","PRC","INF"} )
    {
      if (check_extension(upperName,ext))
        hasCksum = false;
    }
  }
//  if (hasCksum)
//    hasCksum = !(upperName == GAMEREV_INF || upperName == AUDITS_FMT || upperName == ADJUST_FMT);

  return hasCksum;
}

//////////////////////////////
uint64_t BlitzFS::usedSize() const
{
  uint64_t size = 0;

  for (auto &e : m_entries)
  {
    size_t filesize = (e.filesize * 4); // filesize in bytes
    uint64_t addr = BLITZFS_BLOCKADDR(e.block);
    size = std::max(size,addr+filesize);
  }

  return size;
}

//////////////////////////////
uint64_t BlitzFS::totalSize() const
{
  return m_file.logical_bytes();
  }
