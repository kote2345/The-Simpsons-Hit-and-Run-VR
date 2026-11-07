//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _FILE_HPP
#define _FILE_HPP

#include <p3d/p3dtypes.hpp>
#include <p3d/refcounted.hpp>
#include <radload/loader.hpp>
#include <radload/utility/stream.hpp>

class tFile : public radLoadStream
{
public:
    //new interface
    enum DataType
    {
        BYTE = 1, WORD = 2, DWORD = 4, QWORD = 8
    };

    virtual bool     EndOfFile(void) = 0;
    virtual void     SetPosition(int) { rAssert(false); }
    virtual void     Advance(unsigned) = 0;
     
    virtual bool GetData(void* buf, unsigned count, DataType type = BYTE) = 0;
    
    inline P3D_U8  GetByte(void) { P3D_U8 tmp=0; GetData(&tmp, 1, BYTE); return tmp; }
    inline P3D_U16 GetWord(void) { P3D_U16 tmp=0; GetData(&tmp, 1, WORD); return tmp; }
    inline P3D_U32 GetDWord(void) { P3D_U32 tmp=0; GetData(&tmp, 1, DWORD); return tmp; }
    inline P3D_U64 GetQWord(void) { P3D_U64 tmp; GetData(&tmp, 1, QWORD); return tmp; }

    inline bool Read(void* data, unsigned count, unsigned size) { return GetData(data,count,static_cast<DataType>(size)); }

    char* GetFullFilename(void) {return fullFilename;}
    char* GetFilename(void)     {return filename;}
    char* GetExtension(void)    {return extension;}

    virtual void SetCompressed(bool b) {compressed = b;}
    bool GetCompressed(void)   {return compressed;}
    virtual void SetUncompressedSize(int size) {}

protected:
    tFile();
    ~tFile();

    void SetFilename(const char*);

    char* fullFilename;
    char* filename;
    char* extension;

    bool compressed;

    void UncompressBlock(unsigned char* input, unsigned int inputsize, unsigned char* output, unsigned int outputsize);
};

class tFileMem : public tFile
{
public :
    tFileMem();
    tFileMem(unsigned char* data, unsigned len = ~0U, bool destroy = false);
    ~tFileMem();

    // tFile overrides
    virtual bool     EndOfFile(void);
    virtual unsigned GetSize(void);
    virtual unsigned GetPosition(void);
    virtual void     Advance(unsigned);

    bool GetData(void* buf, unsigned count, DataType type = BYTE);

    // local funcitons
    unsigned char* GetMemory(void);

    // need this to be public so you can set a filename for memory 
    void SetFilename(const char* c) { tFile::SetFilename(c);}

    void SetCompressed(bool b);
    void SetUncompressedSize(int size);

    virtual bool GetEndianSwap(void) { return dataStream->GetEndianSwap(); }
    // returns prevoius state of the endian swap bool
    virtual bool SetEndianSwap(bool swap) { m_endianSwap = swap; return dataStream->SetEndianSwap(swap); }

protected:
    radLoadDataStream* dataStream;
    bool del;

    unsigned int length;
};

#endif //#ifndef _TFILE_HPP


