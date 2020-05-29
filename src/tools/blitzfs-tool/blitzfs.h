#ifndef BLITZ_FS_H
#define BLITZ_FS_H

#include <string>
#include <vector>
#include <functional>

// forward declaration
class chd_file;
//////////////////////////////
namespace blitz
{
  enum class Version {
    UNKNOWN,
    BLITZ97,
    BLITZ99,
    BLITZ2K
  };

  class BlitzFS
  {
  public:
    struct FileEntry
    {
      std::string name;  // char name[12]
      uint32_t filesize = 0; // filesize in words
      union {
        uint32_t timestamp = 0;  // timestamp?
        struct {
        //  unsigned char unk;
        //  unsigned char ff_attrib;  /* actual attributes of the file found */
          unsigned short ff_ftime_sec:5;  /* hours:5, minutes:6, (seconds/2):5 */
          unsigned short ff_ftime_min:6;  /* hours:5, minutes:6, (seconds/2):5 */
          unsigned short ff_ftime_hours:5;  /* hours:5, minutes:6, (seconds/2):5 */

          unsigned short ff_fdate_day:5;  /* (year-1980):7, month:4, day:5 */
          unsigned short ff_fdate_month:4;  /* (year-1980):7, month:4, day:5 */
          unsigned short ff_fdate_year:7;  /* (year-1980):7, month:4, day:5 */
        };
      };
      uint32_t block = 0;

      struct {
        uint64_t addr = 0; // address of file entry
        int toc   = 0; // which table-of-contents entry this file belongs to
        int index = 0; // index into toc
      } meta;
    };

    using FileBuf = std::vector<uint8_t>;

    struct BlitzFile
    {
      std::string name;
      FileBuf bytes;
    };

  public:
    using FileEntryVec = std::vector<FileEntry>;

    BlitzFS(chd_file&);

    bool init();

    Version version() const { return m_version; }

    uint64_t toc() const { return m_toc; }

    // call this to fix AUDITS.FMT/ADJUST.FMT block address error in blitz2k.chd
    bool repair();

    bool readFileCksum(const std::string &fileName, uint32_t &cksum, std::string &err);

    bool readFile (const std::string &fileName, BlitzFile &file, std::string &err);
    bool writeFile(const BlitzFile &file, std::string &err); // update file and checksum

    bool extract(const std::string &fileName, const std::string &outdir, std::string &err);
    bool update (const std::string &fileName, const std::string &indir,  std::string &err);

    bool prepareAppend();
    bool append(const std::string &fileName, const std::string &indir,  std::string &err);

    const FileEntryVec& entries() const { return m_entries; }

    bool hasCksum(const std::string &name) const;
    void setHasCksum(bool hasCksum) { m_hasCksum = hasCksum; }

    uint64_t usedSize() const;
    uint64_t totalSize() const;

    chd_file& _chd() { return m_file; }

    // re-write the table of contents
    bool updateTOC();

  protected:
    bool readEntry(uint64_t addr, FileEntry &entry) const;
    bool writeEntry(const FileEntry &entry);

    const FileEntry* findFile(const std::string &name) const;
    FileEntry* findFile(const std::string &name);

  private:
    chd_file &m_file;
    Version m_version = Version::UNKNOWN;
    FileEntryVec m_entries;
    bool m_hasCksum = true;
    uint64_t m_toc = 0;
  };

}


#endif
