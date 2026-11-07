//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/file.hpp>
#include <p3d/error.hpp>
#include <p3d/utility.hpp>

#include <string.h>
#include <p3d/lzr.hpp>

tFile::tFile()
{
    fullFilename = filename = extension = NULL;
    compressed = false;
}

tFile::~tFile()
{
    p3d::FreeTemp(fullFilename);
}

void tFile::SetFilename(const char* n)
{
    if(fullFilename)
        p3d::FreeTemp(fullFilename);

    fullFilename = filename = extension = NULL;

    fullFilename = (char*)p3d::MallocTemp(strlen(n) + 1);
    strcpy(fullFilename, n);
    
    int i;

    filename = fullFilename;

    for(i = strlen(fullFilename); i>= 0; i--)
    {
        if((fullFilename[i] == '\\') || (fullFilename[i] == '/'))
        {
            filename = &fullFilename[i+1];
            break;
        }
    }

    extension = &fullFilename[strlen(fullFilename)];

    for(i = strlen(fullFilename); i>= 0; i--)
    {
        if(fullFilename[i] == '.')
        {
            extension = &fullFilename[i];
            break;
        }
    }
}

//----------------------------------------------------------

void
tFile::UncompressBlock(unsigned char* input, unsigned int inputsize, unsigned char* output, unsigned int outputsize)
{

    lzr_decompress(input,inputsize,output,outputsize);

}


//----------------------------------------------------------
#if 0
#include <p3d/utility.hpp>
static inline void AsyncTest(int c)
{
    static int count = 0;
    count += c;
    if(count > 16000)
    {
        p3d::platform->CycleTasks();
        count = 0;
    }
}
#else
#define AsyncTest(x) 
#endif


tFileMem::tFileMem(): dataStream(NULL), del(false), length(0)
{
}

tFileMem::tFileMem(unsigned char* data, unsigned len, bool d)
: dataStream(NULL), del(d), length(0)
{
    dataStream = new radLoadDataStream(data,len,d);
    dataStream->AddRef();
    SetFilename("temp.p3d");
}

tFileMem::~tFileMem()
{
    if(dataStream)
    {
        dataStream->Release();
    }
}

bool tFileMem::GetData(void* buf, unsigned count, DataType type)
{
    return dataStream->Read(buf, count, static_cast<unsigned int>(type));
}

bool tFileMem::EndOfFile(void)              
{ 
    return !(GetPosition() < GetSize());
}

unsigned tFileMem::GetSize(void)                
{ 
    return dataStream->GetSize();
}

void tFileMem::Advance(unsigned offset)  
{ 
    Read( NULL, offset, 1 );
} 

unsigned tFileMem::GetPosition(void)            
{ 
    return dataStream->GetPosition();
}

unsigned char* tFileMem::GetMemory()
{
    return dataStream->GetMemory();
}

void tFileMem::SetCompressed(bool b)
{
    if(b)
    {
        // Check that the uncompressed size has been set first
        rAssert(length);
        unsigned char* oldmem = GetMemory();
        unsigned char* position = oldmem;
        unsigned char* newmem = new unsigned char[length];
        unsigned char* output = newmem;
        unsigned int total = 0;

        while( total < length )
        {
            int inputsize = GetDWord();
            int outsize = GetDWord();
            UncompressBlock(position, inputsize, output, outsize);
            total += outsize;
            output += outsize;
            position += inputsize;
        }
        dataStream->Release();
        dataStream = new radLoadDataStream(newmem, length, del);
        
    }
}

void tFileMem::SetUncompressedSize(int size)
{
    length = size;
}

