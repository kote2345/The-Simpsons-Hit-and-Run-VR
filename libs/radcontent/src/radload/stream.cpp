//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <radload/utility/stream.hpp>
#include <string.h>
#include <radfile.hpp>
#include <radthread.hpp>

// GLOBAL UTILITY FUNCTIONS --------------------------------------------------

// Stream creation functions
void radLoadCreateStream( radLoadStream** stream, radLoadOptions* options, radLoadUpdatableRequest* request )
{
    *stream = new radLoadLoaderStream( options, request );
    (*stream)->AddRef();
}

void radLoadCreateFileStream( radLoadStream** stream, const char* filename, radLoadUpdatableRequest* request )
{
    *stream = new radLoadFileStream( filename );
    (*stream)->AddRef();
    if( request )
    {
        request->SetStream( *stream );
    }
}

void radLoadCreateDataStream( radLoadStream** stream, unsigned char* buffer, unsigned int size, bool del, radLoadUpdatableRequest* request )
{
    *stream = new radLoadDataStream( buffer, size, del );
    (*stream)->AddRef();
    if( request )
    {
        request->SetStream( *stream );
    }
}

// Data retrieval functions
radLoadByte radLoadStreamGetByte( radLoadStream* stream )
{
    radLoadByte temp;
    stream->Read( &temp, 1, radLoadStream::BYTE );
    return temp;
}

radLoadWord radLoadStreamGetWord( radLoadStream* stream )
{
    radLoadWord temp;
    stream->Read( &temp, 1, radLoadStream::WORD );
    return temp;
}

radLoadDWord radLoadStreamGetDWord( radLoadStream* stream )
{
    radLoadDWord temp;
    stream->Read( &temp, 1, radLoadStream::DWORD );
    return temp;
}

radLoadQWord radLoadStreamGetQWord( radLoadStream* stream )
{
    radLoadQWord temp;
    stream->Read( &temp, 1, radLoadStream::QWORD );
    return temp;
}


// radLoadStream -------------------------------------------------------------
void radLoadStream::EndianSwap( void* data, unsigned int count, unsigned int dataSize )
{
    if (!m_endianSwap) return;
    if (dataSize == 1) return;

    unsigned char *d = (unsigned char *)data;

    unsigned int a;
    for (a = 0; a < count; a++)
    {
        unsigned char *src = d + (a * dataSize);
        unsigned char *dest = src + (dataSize - 1);
        while (src < dest)
        {
            unsigned char temp = *src;
            *src = *dest;
            *dest = temp;
            ++src;
            --dest;
        }
    }
}




// radLoadBaseStream ---------------------------------------------------------
radLoadBaseStream::radLoadBaseStream( unsigned int size, radLoadUpdatableRequest* request )
:
m_size( size ),
m_position( 0 )
{
    if( request )
    {
        //request->SetStream( this );
    }
}



// radLoadDataStream ---------------------------------------------------------
radLoadDataStream::radLoadDataStream( unsigned char* buffer, unsigned int size, bool del, radLoadUpdatableRequest* request )
:
radLoadBaseStream( size, request ),
m_pBuffer( buffer ),
m_bDelete( del )
{
}

radLoadDataStream::~radLoadDataStream()
{
}

bool radLoadDataStream::Read( void* data, unsigned int count, unsigned int dataSize )
{
    if(!dataSize || m_position>m_size || count>(m_size-m_position)/dataSize)return false;
    unsigned int numBytes = count*dataSize;
    if( m_position + numBytes > m_size )
    {
        return false;
    }
    if( data )
    {
        memcpy( data, m_pBuffer+m_position, numBytes );
        EndianSwap(data, count, dataSize);
    }
    m_position += numBytes;
    return true;
}

unsigned char* radLoadDataStream::GetMemory()
{
    return m_pBuffer;
}



// radLoadFileStream ---------------------------------------------------------

// Internal callback.
class radLoadFileStream::Callback : public radLoadObject, public IRadFileCompletionCallback
{
public:
    Callback( radLoadFileStream* stream ) : m_pStream( stream ) {}
    virtual void OnFileOperationsComplete( void* pUserData );
    
    void AddRef() { radLoadObject::AddRef(); }
    void Release() { radLoadObject::Release(); }
protected:
    radLoadFileStream* m_pStream;
};

radLoadFileStream::radLoadFileStream( const char* filename, radLoadUpdatableRequest* request )
:
radLoadBaseStream( 0, request ),
m_bDone( true ),
m_bSync( false ),
m_yieldMillis( 0.0f ),
m_yieldSize( 0 ),
m_pFile( NULL ),
m_pCallback( NULL )
{
    ::radFileOpen( &m_pFile, filename );
    m_pCallback = new Callback( this );
    m_pCallback->AddRef();
    m_pFile->GetSizeAsync( &m_size );
    WaitForCompletion();
}

radLoadFileStream::~radLoadFileStream()
{
    m_pCallback->Release();
    m_pFile->Release();
}

bool radLoadFileStream::Read( void* data, unsigned int count, unsigned int dataSize )
{
    if(!dataSize || m_position>m_size || count>(m_size-m_position)/dataSize)return false;
    unsigned int numBytes = count*dataSize;
    if( m_position + numBytes > m_size )
    {
        return false;
    }
    if( data )
    {
        m_pFile->ReadAsync( data, numBytes );
    }
    else
    {
        m_pFile->SetPositionAsync( m_position+numBytes );
    }
    m_position += numBytes;
    WaitForCompletion();
    EndianSwap(data, count, dataSize);

    return true;
}

unsigned int radLoadFileStream::GetSize()
{
    unsigned int size = 0;
    m_pFile->GetSizeAsync( &size );
    WaitForCompletion();
    return size;
}

unsigned int radLoadFileStream::GetPosition()
{
    unsigned int position = 0;
    m_pFile->GetPositionAsync( &position );
    WaitForCompletion();
    return position;
}

void radLoadFileStream::Callback::OnFileOperationsComplete( void* pUserData )
{
    radLoadFileStream* stream = static_cast<radLoadFileStream*>(pUserData);
    stream->m_bDone = true;
}

void radLoadFileStream::WaitForCompletion()
{
    if( m_bSync )
    {
        m_pFile->WaitForCompletion();
    }
    else
    {
        m_bDone = false;
        m_pFile->AddCompletionCallback( m_pCallback, this );
        while( !m_bDone )
        {
            radLoad->SwitchTasks();
        }
    }
}

// radLoadLoaderStream -------------------------------------------------------
radLoadLoaderStream::radLoadLoaderStream( radLoadOptions* options, radLoadUpdatableRequest* request )
:
radLoadFileStream( options->filename, request )
{
    // This is sort of a pointless assert, since it'll crash before this trying
    // to call the radLoadFileStream constructor anyways.
    rAssert( options );

    // We don't want any other streams in the mix besides this one.
    rAssert( !options->stream );
    
    SetSyncLoading( options->syncLoad );
    SetYieldMillis( options->yieldMillis );
    SetYieldSize( options->yieldSize );
}

radLoadLoaderStream::~radLoadLoaderStream()
{
}

