//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/fileftt.hpp>

#include <radtime.hpp>
#include <p3d/loadmanager.hpp>
#include <p3d/utility.hpp>
#include <stdio.h>
#include <string.h>

static const int CACHE_SIZE = 192 * 1024;
static const int CYCLE_SIZE = 8192;
static const int COMPRESSED_BLOCK_SIZE = 4096;

static radTime64 gLastTime = 0;
#ifdef RAD_XBOX
static const unsigned gYieldTime = 2500;  // microseconds
#else
static const unsigned gYieldTime = 5200;  // microseconds
#endif

static char globalCache[ (CACHE_SIZE * 2) + 1024];

#ifndef RAD_PS2
    static char globalUncompressedCache[ 4096 ];  // nv:  danger - this should be LZR_BLOCK_SIZE
#else
    static char* globalUncompressedCache = (char*)0x70002500; // nv:  Simpson's hack to put load buffer in SPR
                                                              // magic address to avoid mfifo, and Pure3D shaders that use SPR
#endif

//--------------------------------------------------------------------------
void tFileFTT::SetYieldTime(unsigned ms)
{
    // nv:  nop
}

//--------------------------------------------------------------------------
tFileFTT::tFileFTT(const char* filename, bool synchronous) :
     m_pIRadFile( NULL ),
     fileSize(0),
     compressedFileSize(0),
     position(0),
     filePosition(0),
     currentCache(0),
     uncompressedcache(NULL),
     currentBuffer(NULL),
     currentPos(0),
     currentSize(0),
     cycle_size(0),
     compressedBuffer(0),
     compressedPos(0),
     compressedSize(0),
     nextSize(0),
     bufferSize(0)
{
    SetFilename(filename);
    rAssert(!synchronous);
    uncompressedcache = NULL;
}

//--------------------------------------------------------------------------
tFileFTT::~tFileFTT()
{
    if(m_pIRadFile != NULL)
    {
        m_pIRadFile->Release( );
    }
}

//--------------------------------------------------------------------------
bool tFileFTT::EndOfFile()
{
    OpenFile();
    return position >= fileSize;
}

//--------------------------------------------------------------------------
unsigned tFileFTT::GetSize()
{
    OpenFile();
    return fileSize;
}

//--------------------------------------------------------------------------
void tFileFTT::SetPosition(int offset)
{
    P3DASSERT(!compressed && "SetPosition not implmented for compressed files");

    int signedoffset = offset - position;

    if(((currentPos + signedoffset) >= 0) && ((currentPos + signedoffset) <= currentSize))
    {
        currentPos += signedoffset;
        position = offset;
    }
    else
    {
        filePosition = offset & 0xfffff800;
        m_pIRadFile->SetPositionAsync( filePosition );
        ReadBuffer(cache[!currentCache]);

        position = offset;
        FillBuffer();
    }
}

//--------------------------------------------------------------------------
void tFileFTT::Advance(unsigned offset)
{
    OpenFile();
    if ( offset == 0 )
    {
        return;
    }
    if(position>=fileSize)return;
    if(offset>static_cast<unsigned>(fileSize-position))offset=fileSize-position;

    if(currentPos + (int)offset <= currentSize)
    {
        currentPos += offset;
        position += offset;
    }
    else
    {
        while(offset > 0)
        {
            int size = offset;
            if( currentPos + size > currentSize)
            {
                size = currentSize - currentPos;
            }
            currentPos += size;
            position += size;
            offset -= size;
            if(offset > 0)
            {
                FillBuffer();
            }
        }
    }
}

//--------------------------------------------------------------------------
unsigned tFileFTT::GetPosition()
{
    return position;
}

//--------------------------------------------------------------------------
void tFileFTT::OnFileOperationsComplete(void*)
{
     done = true;
}

//--------------------------------------------------------------------------
void tFileFTT::WaitForCompletion( void )
{
    int i = 0;
    unsigned int time = radTimeGetMicroseconds64();
    while(!m_pIRadFile->CheckForCompletion())
    {
        i++;
        p3d::loadManager->SwitchTask();
        gLastTime = radTimeGetMicroseconds64();
    }
    if(i>0)
    {
        time = radTimeGetMicroseconds64()-time;
        rReleasePrintf("P3D Loading Starved for %d frames, %d ms\n",i,(time/1000));
    }
}

bool tFileFTT::GetData(void* buf, unsigned count, DataType type)
{
    if( m_pIRadFile == NULL )
    {
        OpenFile();
    }

    const unsigned elementSize=static_cast<unsigned>(type);
    if(!elementSize || count>0x7fffffffU/elementSize)return false;
    int numBytes = static_cast<int>(count*elementSize);
    if(position<0 || position>fileSize || numBytes>fileSize-position)
    {
        if(buf && count && count<=0x7fffffffU/static_cast<unsigned>(type))
            memset(buf,0,count*type);
        return false;
    }

     // No data to read
     if ( numBytes == 0 )
     {
          return true;
     }

     if(currentPos + numBytes <= currentSize)
     {
         memcpy(buf, &currentBuffer[ currentPos ], numBytes);
         currentPos += numBytes;
         position += numBytes;
     }
     else
     {
         char* output = (char*) buf;
         while(numBytes > 0)
         {
             int size = numBytes;
             if( currentPos + size > currentSize)
             {
                 size = currentSize - currentPos;
             }
             if(size<=0)
             {
                 // An exhausted cache is normal in the middle of a file.
                 // Refill before deciding the read failed. Returning here
                 // produced zero chunk lengths at the 192 KiB boundary.
                 if(size<0 || position>=fileSize)return false;
                 FillBuffer();
                 if(currentSize<=currentPos)return false;
                 continue;
             }

             memcpy(output, &currentBuffer[ currentPos ], size);
             output += size;
             currentPos += size;
             position += size;
             numBytes -= size;
             
             if(numBytes > 0)
             {
                 FillBuffer();
             }
         }
     }

     // Reorder the bits in reverse order when needed
    #ifndef RAD_PS2
        EndianSwap(buf, count, type);
    #endif

    // Check to see if we have given this process 
    // enough time to process in a given thread instance
    // if so cycle to the main thread

    radTime64 now = radTimeGetMicroseconds64();
    radTime64 elapsed = now - gLastTime;
    if( elapsed >= gYieldTime )
    {
        p3d::loadManager->SwitchTask();
        gLastTime = radTimeGetMicroseconds64();
    }

    return true;
}

//--------------------------------------------------------------------------
void tFileFTT::SetCompressed(bool b)
{
    compressed = b;
    if(b)
    {
        compressedPos = currentPos;
        compressedBuffer = currentBuffer;
        compressedSize = currentSize;
        
        uncompressedcache = globalUncompressedCache;
        currentBuffer = uncompressedcache;
        currentPos = 0;
        currentSize = 0;
    }
}

//--------------------------------------------------------------------------
void tFileFTT::SetUncompressedSize(int size)
{
    compressedFileSize = fileSize;
    fileSize = size;
}

//--------------------------------------------------------------------------
void tFileFTT::AdvanceCycle(int numBytes)
{
     // nv: nop - inlined into GetData
}


//--------------------------------------------------------------------------
void tFileFTT::ReadBuffer(char* buf)
{
    unsigned int bytesToRead =  bufferSize;
    int size = fileSize;

    if (compressed)
    {
        size = compressedFileSize;
    }
        
    if (filePosition >= size)
    {
        nextSize=0;
        return;
    }

    P3DASSERT((filePosition & 0x7ff) == 0);

    //
    // Do an optimal read at the end of the file.
    //
    if (filePosition + bytesToRead >= (unsigned int)(size) )
    {
        bytesToRead = size - filePosition;
        m_pIRadFile->ReadAsync(buf, ::radMemoryRoundUp( bytesToRead, m_pIRadFile->GetOptimalSize( ) ) );
    }
    else
    {
        m_pIRadFile->ReadAsync(buf, bytesToRead);
    }

    filePosition += bytesToRead;
    nextSize = bytesToRead;
}

void tFileFTT::Decompress (const unsigned char* input, unsigned int inputsize,
                            unsigned char* output, unsigned int outputsize)
{
    unsigned int outputcount = 0;

    while(outputcount < outputsize)
    {
        radTime64 now = radTimeGetMicroseconds64();
        radTime64 elapsed = now - gLastTime;
        if( elapsed >= gYieldTime )
        {
            p3d::loadManager->SwitchTask();
            gLastTime = radTimeGetMicroseconds64();
        }

        unsigned int code = *input++;

        if(code > 15)
        {
            // a match
            int matchlength = code & 15;

            if(matchlength == 0)
            {
                matchlength += 15;
                while (*input == 0)
                {
                    matchlength += 255;
                    input++;
                }
                matchlength += *input++;
            }

            int offset = (code >> 4) | (*input++) << 4;
            unsigned char* match_ptr = output - offset;

            // shortest match is 4 characters, so we can unroll the loop
            int len = matchlength>>2;
            matchlength -= len<<2;

            do
            {
                *output++ = *match_ptr++;
                *output++ = *match_ptr++;
                *output++ = *match_ptr++;
                *output++ = *match_ptr++;
                outputcount += 4;
            } while(--len);

            while(matchlength)
            {
                *output++ = *match_ptr++;
                outputcount++;
                matchlength--;
            }
        }
        else
        {
            // A literal run
            int runlength = code;
            
            if(runlength == 0)
            {
                while (*input == 0)
                {
                    runlength += 255;
                    input++;
                }
                runlength += *input++;

                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                *output++ = *input++;
                outputcount += 15;
            }

            do
            {
                *output++ = *input++;
                outputcount++;
            } while(--runlength);
        }
    }
}

//--------------------------------------------------------------------------
void tFileFTT::FillBuffer()
{
    if (compressed)
    {
        struct 
        {
            P3D_U32 compressed;
            P3D_U32 uncompressed;
        } sizes;
            
        if (compressedPos + 8 <= compressedSize)
        {
            memcpy((char*)&sizes, &compressedBuffer[ compressedPos ], 8);
            compressedPos += 8;
        } 
        else 
        {
            char* buf = (char*)&sizes;
            int size = compressedSize - compressedPos;
            memcpy(buf, &compressedBuffer[ compressedPos ], size);
            buf += size;

            WaitForCompletion();

            currentCache = !currentCache;
            compressedBuffer = cache[currentCache];
            compressedPos = 0;
            compressedSize = nextSize;
            ReadBuffer(cache[!currentCache]);
            memcpy(buf, &compressedBuffer[ compressedPos ], 8 - size);
            compressedPos += 8 - size;
        }

        EndianSwap(&sizes, 2, 4);

        if (compressedPos + (int)sizes.compressed <= compressedSize)
        {
            Decompress((unsigned char*)&compressedBuffer[ compressedPos ],
                                 (unsigned int)(sizes.compressed),
                                 (unsigned char*)uncompressedcache, 
                                 (unsigned int)(sizes.uncompressed));
            compressedPos += sizes.compressed;
            currentPos = 0;
            currentSize = COMPRESSED_BLOCK_SIZE;
        } 
        else 
        {
            char* buf = cache[currentCache];
            int size = compressedSize - compressedPos;
            memcpy(buf, &compressedBuffer[ compressedPos ], size);
            buf += size;

            WaitForCompletion();

            currentCache = !currentCache;
            compressedBuffer = cache[currentCache];
            compressedPos = 0;
            compressedSize = nextSize;
            memcpy(buf, &compressedBuffer[ compressedPos ],  (int)sizes.compressed - size);
            compressedPos += sizes.compressed - size;
            
            Decompress((unsigned char*)cache[!currentCache],
                                 (unsigned int)(sizes.compressed),
                                 (unsigned char*)uncompressedcache,
                                 (unsigned int)(sizes.uncompressed));

            currentPos = 0;
            currentSize = COMPRESSED_BLOCK_SIZE;

            ReadBuffer(cache[!currentCache]);
        }
    } 
    else 
    {
        WaitForCompletion();

        currentCache = !currentCache;
        currentBuffer = cache[currentCache];
        currentPos = 0;
        currentSize = nextSize;
    
        ReadBuffer(cache[!currentCache]);
    }
}

//--------------------------------------------------------------------------
void tFileFTT::OpenFile( void )
{
    if ( m_pIRadFile == NULL )
    {
        gLastTime = radTimeGetMicroseconds64();

        radFileOpen( &m_pIRadFile, this->GetFullFilename() );
        P3DASSERT(m_pIRadFile);

        WaitForCompletion( );
        P3DASSERT( m_pIRadFile->IsOpen( ));
     
        fileSize = m_pIRadFile->GetSize( );

        // Align to a 128 byte boundary for improved PS2 performance.
        cache[0] = (char*) ::radMemoryRoundUp( (uintptr_t)globalCache, 0x80);
        cache[1] = cache[0] + CACHE_SIZE;
        bufferSize = rmt::Min(CACHE_SIZE, fileSize);

        unsigned int bytesToRead = rmt::Min(CACHE_SIZE * 2, fileSize);
        P3DASSERT((filePosition & 0x7ff) == 0);

        // Do an optimal read at the end of the file.
        if (filePosition + bytesToRead >= (unsigned int)(fileSize) )
        {
            bytesToRead = fileSize - filePosition;
            m_pIRadFile->ReadAsync(cache[0], ::radMemoryRoundUp( bytesToRead, m_pIRadFile->GetOptimalSize( ) ) );
        }
        else
        {
            m_pIRadFile->ReadAsync(cache[0], bytesToRead);
        }

        filePosition += bytesToRead;

        currentCache = 0;
        currentBuffer = cache[0];
        currentPos = 0;
        currentSize = rmt::Min(CACHE_SIZE, fileSize);
        nextSize = rmt::Max(0,rmt::Min(fileSize - CACHE_SIZE, CACHE_SIZE));

        WaitForCompletion();
    }
}

