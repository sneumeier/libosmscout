#ifndef OSMSCOUT_FILEWRITER_H
#define OSMSCOUT_FILEWRITER_H

/*
  This source is part of the libosmscout library
  Copyright (C) 2009  Tim Teulings

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <cstdio>
#include <string>
#include <vector>

#include <osmscout/private/CoreImportExport.h>

#include <osmscout/CoreFeatures.h>

#include <osmscout/GeoCoord.h>
#include <osmscout/ObjectRef.h>
#include <osmscout/Types.h>

#include <osmscout/util/Exception.h>

namespace osmscout {

  /**
    \ingroup File

    FileScanner implements platform independent writing to data in files.
    It uses C standard library FILE internally and wraps it to offer
    a number of convenience methods.
    */
  class OSMSCOUT_API FileWriter
  {
  private:
    std::string          filename;    //!< The filename
    std::FILE            *file;       //!< The low level FILE object
    bool                 hasError;    //!< Flag for signaling that the stream has errors
    std::vector<int32_t> deltaBuffer; //!< Temporary storage for deltas for storing of std::vector<GeoCoord>
    std::vector<uint8_t> byteBuffer;  //!< Temporary data buffer for storing of std::vector<GeoCoord>

  public:
    FileWriter();
    virtual ~FileWriter();

    void Open(const std::string& filename);
    void Close();
    void CloseFailsafe();
    inline bool IsOpen() const
    {
      return file!=NULL;
    }

    inline bool HasError() const
    {
      return file==NULL || hasError;
    }

    std::string GetFilename() const;

    FileOffset GetPos();
    void SetPos(FileOffset pos);
    void GotoBegin();

    void Write(const char* buffer, size_t bytes);

    void Write(const std::string& value);

    void Write(bool boolean);

    void Write(int8_t number);
    void Write(int16_t number);
    void Write(int32_t number);
    void Write(int64_t number);

    void Write(uint8_t number);
    void Write(uint16_t number);
    void Write(uint32_t number);
    void Write(uint64_t number);

    void Write(uint16_t number, size_t bytes);
    void Write(uint32_t number, size_t bytes);
    void Write(uint64_t number, size_t bytes);

    void Write(const ObjectFileRef& ref);

    void WriteFileOffset(FileOffset offset);
    void WriteFileOffset(FileOffset offset,
                         size_t bytes);

    void WriteNumber(int16_t number);
    void WriteNumber(int32_t number);
    void WriteNumber(int64_t number);

    void WriteNumber(uint16_t number);
    void WriteNumber(uint32_t number);
    void WriteNumber(uint64_t number);

    void WriteCoord(const GeoCoord& coord);
    void WriteInvalidCoord();

    void Write(const std::vector<GeoCoord>& nodes);

    void WriteTypeId(TypeId id, uint8_t maxBytes);

    void Flush();
    void FlushCurrentBlockWithZeros(size_t blockSize);
  };

  extern OSMSCOUT_API bool IsValidToWrite(const std::vector<GeoCoord>& nodes);

  /**
   * Efficiently (in disk space handling) write a number of (sorted by file offset) ObjectFileRefs.
   *
   * Note that the delta between two offsets is limited by sizeof(FileOffset)-2 bits.
   */
  class OSMSCOUT_API ObjectFileRefStreamWriter
  {
  private:
    FileWriter& writer;
    FileOffset  lastFileOffset;

  public:
    ObjectFileRefStreamWriter(FileWriter& writer);

    void Reset();

    void Write(const ObjectFileRef& ref);
  };
}

#endif
