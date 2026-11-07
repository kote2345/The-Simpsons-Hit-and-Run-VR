//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/chunkfile.hpp>
#include <p3d/error.hpp>
#include <constants/chunkids.hpp>
#include <stdio.h>

static const int HEADER_SIZE = 12;

tChunkFile::tChunkFile(tFile* file) : stackTop(-1), realFile(NULL)
{
    tRefCounted::Assign(realFile,file);

    P3D_U32  fileChunk;
    GetData(&fileChunk, 4);
    
    switch(fileChunk)
    {
        case Pure3D::DATA_FILE_COMPRESSED_SWAP :
            realFile->SetEndianSwap(!realFile->GetEndianSwap());
            // fallthrough
        case Pure3D::DATA_FILE_COMPRESSED :
            int fileSize;
            GetData(&fileSize, 1, tFile::DWORD);
            realFile->SetUncompressedSize(fileSize);
            realFile->SetCompressed(true);
            GetData(&fileChunk, 1, tFile::DWORD);
            break;
        default :
            break;
    }

    switch(fileChunk)
    {
        case Pure3D::DATA_FILE :
            break;
        case Pure3D::DATA_FILE_SWAP :
            realFile->SetEndianSwap(!realFile->GetEndianSwap());
            fileChunk = Pure3D::DATA_FILE;
            break;
        default :
            break;
    }

    BeginChunk(fileChunk);
}

tChunkFile::~tChunkFile()
{
    tRefCounted::Release(realFile);
}

bool tChunkFile::ChunksRemaining(void)
{
    Chunk* chunk = &chunkStack[stackTop];
    const unsigned position=realFile->GetPosition();
    const unsigned declaredEnd=chunk->startPosition+chunk->chunkLength;
    // Compressed streams expose logical chunk offsets, while some backends
    // report the physical compressed size. Never mix those coordinate spaces.
    const unsigned fileEnd=realFile->GetCompressed()?declaredEnd:realFile->GetSize();
    const unsigned end=declaredEnd<fileEnd?declaredEnd:fileEnd;
    // Replacement assets may retain a short export trailer. It is not a
    // child chunk unless a complete 12-byte header fits in the parent.
    return chunk->chunkLength>chunk->dataLength && position<=end &&
           end-position>=HEADER_SIZE;
}

unsigned tChunkFile::BeginChunk(void)
{
    stackTop++;
    P3DASSERT(stackTop < CHUNK_STACK_SIZE);

    // advance to the start of the subchunks
    if(stackTop != 0)
    {
        // assert that this chunk has subchunks
        P3DASSERT(chunkStack[stackTop-1].dataLength < chunkStack[stackTop-1].chunkLength);

        unsigned start = chunkStack[stackTop-1].startPosition + chunkStack[stackTop-1].dataLength;
        if(realFile->GetPosition() < start)
            realFile->Advance(start - realFile->GetPosition());
    }

    // read chunk header
    chunkStack[stackTop].startPosition = realFile->GetPosition();
    chunkStack[stackTop].id = GetUInt();
    chunkStack[stackTop].dataLength = GetUInt();
    chunkStack[stackTop].chunkLength = GetUInt();

    return chunkStack[stackTop].id;
}

unsigned tChunkFile::BeginChunk(unsigned chunkID)
{
    stackTop++;
    P3DASSERT(stackTop == 0); // only the p3d file chunk should call this version of the funciton

    chunkStack[stackTop].startPosition = realFile->GetPosition() - 4;
    chunkStack[stackTop].id = chunkID;
    chunkStack[stackTop].dataLength = GetUInt();
    chunkStack[stackTop].chunkLength = GetUInt();

    return chunkStack[stackTop].id;
}

void tChunkFile::EndChunk(void)
{
    unsigned start = chunkStack[stackTop].startPosition;
    unsigned chLength = chunkStack[stackTop].chunkLength;
    unsigned dataLength = chunkStack[stackTop].dataLength;

    P3DASSERT(stackTop > 0);
    P3DASSERT(realFile->GetPosition() <=  start + chLength); 
//   P3DASSERT(oldChunkFormat || (chLength == dataLength) || (realFile->GetPosition() == start+chLength));
    realFile->Advance((start + chLength) - realFile->GetPosition());
    stackTop--;
}

unsigned tChunkFile::GetCurrentID(void)
{
    return chunkStack[stackTop].id;
}

unsigned tChunkFile::GetCurrentDataLength(void)
{
    return chunkStack[stackTop].dataLength - HEADER_SIZE;
}

unsigned tChunkFile::GetCurrentOffset(void)
{
    return realFile->GetPosition() - chunkStack[stackTop].startPosition - HEADER_SIZE;
}
    
tFile* tChunkFile::BeginInset(void)
{
    return realFile;
}

void   tChunkFile::EndInset(tFile* f)
{
    P3DASSERT(realFile->GetPosition() <= chunkStack[stackTop].startPosition + chunkStack[stackTop].chunkLength);
}

