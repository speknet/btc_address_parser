// Copyright (c) 2020 gladcow
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block.h>
#include <chainparams.h>
#include <crypto.h>
#include <array>
#include <cstring>
#include <unistd.h>
#include "tinyformat.h"

using namespace btc_utils;

static const unsigned int MAX_SIZE = 0x02000000;

template <typename... Args>
static inline void log_printf(const char* fmt, const Args&... args)
{
     std::string log_msg;
     try {
         log_msg = tfm::format(fmt, args...);
     } catch (tinyformat::format_error& fmterr) {
         /* Original format string will have newline so don't add one here */
         log_msg = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + fmt;
     }
     std::cout << log_msg << std::endl;
}

std::string compose_block_file_path(std::string db_path, uint32_t index)
{
   std::string fname = strprintf("%s%05u.dat", "blk", index);
   if(db_path.empty())
      return fname;
   if(db_path.back() == '/')
      return db_path + fname;
   return db_path + "/" + fname;
}

/** Non-refcounted RAII wrapper around a FILE* that implements a ring buffer to
 *  deserialize from. It guarantees the ability to rewind a given number of bytes.
 *
 *  Will automatically close the file when it goes out of scope if not null.
 *  If you need to close the file early, use file.fclose() instead of fclose(file).
 */
class buffered_file_t
{
private:
    FILE *src;            //!< source file
    uint64_t nSrcPos;     //!< how many bytes have been read from source
    uint64_t nReadPos;    //!< how many bytes have been read from this
    uint64_t nReadLimit;  //!< up to which position we're allowed to read
    uint64_t nRewind;     //!< how many bytes we guarantee to rewind
    std::vector<char> vchBuf; //!< the buffer

protected:
    //! read data from the source to fill the buffer
    bool Fill() {
        unsigned int pos = nSrcPos % vchBuf.size();
        unsigned int readNow = vchBuf.size() - pos;
        unsigned int nAvail = vchBuf.size() - (nSrcPos - nReadPos) - nRewind;
        if (nAvail < readNow)
            readNow = nAvail;
        if (readNow == 0)
            return false;
        size_t nBytes = fread((void*)&vchBuf[pos], 1, readNow, src);
        if (nBytes == 0) {
            throw std::ios_base::failure(feof(src) ? "CBufferedFile::Fill: end of file" : "CBufferedFile::Fill: fread failed");
        }
        nSrcPos += nBytes;
        return true;
    }

public:
    buffered_file_t(FILE *fileIn, uint64_t nBufSize, uint64_t nRewindIn) :
        nSrcPos(0), nReadPos(0), nReadLimit(std::numeric_limits<uint64_t>::max()), nRewind(nRewindIn), vchBuf(nBufSize, 0)
    {
        if (nRewindIn >= nBufSize)
            throw std::ios_base::failure("Rewind limit must be less than buffer size");
        src = fileIn;
    }

    ~buffered_file_t()
    {
        fclose();
    }

    // Disallow copies
    buffered_file_t(const buffered_file_t&) = delete;
    buffered_file_t& operator=(const buffered_file_t&) = delete;

    void fclose()
    {
        if (src) {
            ::fclose(src);
            src = nullptr;
        }
    }

    //! check whether we're at the end of the source file
    bool eof() const {
        return nReadPos == nSrcPos && feof(src);
    }

    //! read a number of bytes
    void read(unsigned char *pch, size_t nSize) {
        if (nSize + nReadPos > nReadLimit)
            throw std::ios_base::failure("Read attempted past buffer limit");
        while (nSize > 0) {
            if (nReadPos == nSrcPos)
                Fill();
            unsigned int pos = nReadPos % vchBuf.size();
            size_t nNow = nSize;
            if (nNow + pos > vchBuf.size())
                nNow = vchBuf.size() - pos;
            if (nNow + nReadPos > nSrcPos)
                nNow = nSrcPos - nReadPos;
            memcpy(pch, &vchBuf[pos], nNow);
            nReadPos += nNow;
            pch += nNow;
            nSize -= nNow;
        }
    }

    uint8_t readdata8()
    {
       uint8_t obj;
       read(&obj, 1);
       return obj;
    }

    uint16_t readdata16()
    {
       uint16_t obj;
       read((unsigned char*)&obj, 2);
       return le16toh(obj);
    }

    uint32_t readdata32()
    {
       uint32_t obj;
       read((unsigned char*)&obj, 4);
       return le32toh(obj);
    }

    uint64_t readdata64()
    {
       uint64_t obj;
       read((unsigned char*)&obj, 8);
       return le64toh(obj);
    }

    uint64_t read_compact_int()
    {
        uint8_t ci_size = readdata8();
        uint64_t res = 0;
        if (ci_size < 253)
        {
            res = ci_size;
        }
        else if (ci_size == 253)
        {
            res = readdata16();
            if (res < 253)
                throw std::runtime_error("non-canonical compact int");
        }
        else if (ci_size == 254)
        {
            res = readdata32();
            if (res < 0x10000u)
                throw std::runtime_error("non-canonical compact int");
        }
        else
        {
            res = readdata64();
            if (res < 0x100000000ULL)
                throw std::runtime_error("non-canonical compact int");
        }
        if (res > (uint64_t)MAX_SIZE)
            throw std::runtime_error("compact int is too large");
        return res;
    }

    void unserialize(unsigned char& val)
    {
       val = readdata8();
    }

    void unserialize(uint32_t& val)
    {
       val = readdata32();
    }

    void unserialize(uint64_t& val)
    {
       val = readdata64();
    }

    template<typename T, typename A>
    void unserialize(std::vector<T, A>& v)
    {
       v.clear();
       uint64_t v_size = read_compact_int();
       v.resize(v_size);
       for (uint64_t i = 0; i < v_size; i++)
           v[i].unserialize(*this);
    }

    void unserialize(std::vector<unsigned char>& v)
    {
       v.clear();
       uint64_t v_size = read_compact_int();
       v.resize(v_size);
       for (uint64_t i = 0; i < v_size; i++)
           unserialize(v[i]);
    }

    void unserialize(std::vector<std::vector<unsigned char> >& v)
    {
       v.clear();
       uint64_t v_size = read_compact_int();
       v.resize(v_size);
       for (uint64_t i = 0; i < v_size; i++)
           unserialize(v[i]);
    }

    void unserialize(uint256_t& val)
    {
       read(val.data(), val.size());
    }

    //! return the current reading position
    uint64_t GetPos() const {
        return nReadPos;
    }

    //! rewind to a given reading position
    bool SetPos(uint64_t nPos) {
        size_t bufsize = vchBuf.size();
        if (nPos + bufsize < nSrcPos) {
            // rewinding too far, rewind as far as possible
            nReadPos = nSrcPos - bufsize;
            return false;
        }
        if (nPos > nSrcPos) {
            // can't go this far forward, go as far as possible
            nReadPos = nSrcPos;
            return false;
        }
        nReadPos = nPos;
        return true;
    }

    bool Seek(uint64_t nPos) {
        long nLongPos = nPos;
        if (nPos != (uint64_t)nLongPos)
            return false;
        if (fseek(src, nLongPos, SEEK_SET))
            return false;
        nLongPos = ftell(src);
        nSrcPos = nLongPos;
        nReadPos = nLongPos;
        return true;
    }

    //! prevent reading beyond a certain position
    //! no argument removes the limit
    bool SetLimit(uint64_t nPos = std::numeric_limits<uint64_t>::max()) {
        if (nPos < nReadPos)
            return false;
        nReadLimit = nPos;
        return true;
    }

    template<typename T>
    buffered_file_t& operator>>(T&& obj) {
        // Unserialize from this stream
        obj.unserialize(*this);
        return (*this);
    }

    //! search for a given byte in the stream, and remain positioned on it
    void FindByte(char ch) {
        while (true) {
            if (nReadPos == nSrcPos)
                Fill();
            if (vchBuf[nReadPos % vchBuf.size()] == ch)
                break;
            nReadPos++;
        }
    }
};

void ParseBlockFile(FILE* f, int& nLoaded, FILE* addrout)
{
   try {
       // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
       buffered_file_t blkdat(f, 2*MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE+8);
       uint64_t nRewind = blkdat.GetPos();
       while (!blkdat.eof()) {
           blkdat.SetPos(nRewind);
           nRewind++; // start one byte further next time, in case of failure
           blkdat.SetLimit(); // remove former limit
           unsigned int nSize = 0;
           try {
               // locate a header
               std::array<unsigned char, MESSAGE_START_SIZE> buf;
               blkdat.FindByte(message_start()[0]);
               nRewind = blkdat.GetPos()+1;
               blkdat.read(buf.data(), MESSAGE_START_SIZE);
               if (memcmp(buf.data(), message_start(), MESSAGE_START_SIZE))
                   continue;
               // read size
               blkdat.read((unsigned char*)&nSize,  sizeof(nSize));
               if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE)
                   continue;
           } catch (const std::exception&) {
               // no valid block header found; don't complain
               break;
           }
           try {
               // read block
               uint64_t nBlockPos = blkdat.GetPos();
               blkdat.SetLimit(nBlockPos + nSize);
               blkdat.SetPos(nBlockPos);
               block_t block;
               blkdat >> block;
               nRewind = blkdat.GetPos();

               for(const auto& tx: block.txes_)
               {
                  for(const auto& out: tx.vout)
                  {
                     std::vector<std::string> addrs = out.addresses();
                     for(const auto& addr: addrs)
                     {
                        fwrite(addr.c_str(), 1, addr.size(), addrout);
                        fwrite("\n", 1, 1, addrout);
                     }
                  }
               }
               if(nLoaded % 100 == 1)
                  log_printf("Block %i is read", nLoaded++);
           } catch (const std::exception& e) {
               log_printf("%s: Deserialize or I/O error - %s", __func__, e.what());
           }
       }
   } catch (const std::runtime_error& e) {
       log_printf("System error: %s", e.what());
   }
}

void print_usage()
{
   std::cout << "Usage:" << std::endl;
   std::cout << "addr_parser [-m|-t|-r] [-p db_path] [-o output_file]" << std::endl;
   std::cout << "where" << std::endl;
   std::cout << "-m - parse BTC mainnet data, default option" << std::endl;
   std::cout << "-t - parse BTC testnet data" << std::endl;
   std::cout << "-r - parse BTC regtest data" << std::endl;
   std::cout << "db_path - path to the directory with block files (e.g. ${HOME}/.bitcoin/blocks),  default value is current directory" << std::endl;
   std::cout << "output_file - file to write parsed addresses, default value addresses.txt" << std::endl;
}

int main(int argc, char* argv[])
{
   std::string db_path;
   std::string out_file = "addresses.txt";
   char c;
   bool option_found = false;

   while ((c = getopt(argc, argv, "mtrp:o:?")) != -1)
   {
     switch (c)
     {
         case 'm':
            btc_utils::g_network = btc_utils::network_t::mainnet;
            break;
         case 't':
            btc_utils::g_network = btc_utils::network_t::testnet;
            break;
         case 'r':
            btc_utils::g_network = btc_utils::network_t::regtest;
            break;
         case 'p':
            if (!optarg)
            {
               std::cout << "p option requires argument" << std::endl;
               print_usage();
               return 1;
            }
            db_path = optarg;
            break;
         case 'o':
           if (!optarg)
           {
              std::cout << "o option requires argument" << std::endl;
              print_usage();
              return 1;
           }
            out_file = optarg;
            break;
         case '?':
            print_usage();
            return 1;
         default:
            print_usage();
            return 1;
      }
   }
   if (optind < argc)
   {
      print_usage();
      return 1;
   }

   unsigned int nFile = 0;
   int blocks = 0;
   FILE* out = fopen(out_file.c_str(), "w");
   if (!out) {
       log_printf("Error: Unable to open file %s\n", out_file);
       return 1;
   }
   while (true) {
       std::string block_file = compose_block_file_path(db_path, nFile);
       FILE* file = fopen(block_file.c_str(), "rb");
       if (!file) {
           log_printf("Error: Unable to open file %s\n", block_file.c_str());
           break;
       }
       log_printf("Processing block file blk%05u.dat...", nFile);
       ParseBlockFile(file, blocks, out);
       nFile++;
       fflush(out);
   }
   fclose(out);
   log_printf("Processing finished");
   return 0;
}
