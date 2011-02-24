/* 
 * Copyright (c) 2005-2006 LAAS/CNRS <openrobots@laas.fr>
 *	Sylvain Joyeux <sylvain.joyeux@m4x.org>
 *
 * All rights reserved.
 *
 * Redistribution and use  in source  and binary  forms,  with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   1. Redistributions of  source  code must retain the  above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice,  this list of  conditions and the following disclaimer in
 *      the  documentation  and/or  other   materials provided  with  the
 *      distribution.
 *
 * THIS  SOFTWARE IS PROVIDED BY  THE  COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY  EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING,  BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES  OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR  PURPOSE ARE DISCLAIMED. IN  NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR      CONTRIBUTORS  BE LIABLE FOR   ANY    DIRECT, INDIRECT,
 * INCIDENTAL,  SPECIAL,  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF  SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE   OF THIS SOFTWARE, EVEN   IF ADVISED OF   THE POSSIBILITY OF SUCH
 * DAMAGE.
 */


#include "Logfile.hpp"
#include <sstream>
#include <cassert>
#include <boost/static_assert.hpp>
#include <string.h>

#include <typelib/registry.hh>
#include <typelib/pluginmanager.hh>

#include <boost/format.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

using namespace std;
using boost::mutex;
namespace endian = utilmm::endian;

BOOST_STATIC_ASSERT(( sizeof(Logging::Prologue) == 16 ));

const char Logging::FORMAT_MAGIC[] = "POCOSIM";

void Logging::writePrologue( File& file )
{
    Prologue prologue;
    prologue.version    = endian::to_little<uint32_t>(Logging::FORMAT_VERSION);
#if defined(WORDS_BIGENDIAN)
    prologue.flags = 1;
#else
    prologue.flags = 0;
#endif

    file.write(reinterpret_cast<char*>(&prologue), sizeof(prologue));
}

namespace Logging
{
    inline void write_buffer( int fd, const void* buf, long len )
    {
        //sync_file_range( fd, 0, 0, SYNC_FILE_RANGE_WRITE );
        long written = 0;
        while( written < len )
        {
            long res = ::write( fd, reinterpret_cast<const char *>(buf) + written, len - written );
            if( res >= 0 )
                written += res;

            if( res == -1 )
                throw std::runtime_error( strerror( errno ) ); 
        }
        posix_fadvise( fd, 0, 0, POSIX_FADV_DONTNEED );
    }

    File::File(std::string& file_name, size_t buffer_size)
    {
	m_fd = open( file_name.c_str(), 
		O_CREAT|O_WRONLY|O_TRUNC, 
		S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );

	if( m_fd == -1 )
	{
	    throw std::runtime_error( 
		    boost::str( boost::format("Could not open file %s for writing. %s")
			% file_name % strerror( errno ) ) );
	}

        // initialize buffer
        buffer = new unsigned char[buffer_size];
        this->buffer_size = buffer_size;
        buffer_pos = 0;
    }

    File::~File()
    {
        flush();
	if( m_fd != -1 && close( m_fd ) )
	{
	    perror("could not close log file.");
	}
        delete [] buffer;
    }

    void File::flush() 
    {
        // call write if there is stuff in the buffer
        if( buffer_pos > 0 )
        {
            write_buffer( m_fd, buffer, buffer_pos );
            buffer_pos = 0;
        }
	last_flush = base::Time::now();
    }

    void File::write( const void* buf, long len )
    {
        if( len > buffer_size )
        {
            flush();
            // write directly
            write_buffer( m_fd, buf, len );
        }
        else
        {
            // see if we need to flush to fit the data
            if( buffer_size - buffer_pos < len )
                flush();

            // copy the data into the buffer otherwise
            memcpy( buffer + buffer_pos, buf, len );
            buffer_pos += len;
        }
    }


    Logfile::Logfile(std::string& file_name)
        : m_file( file_name ), m_stream_idx(0)
    {
        writePrologue( m_file );
    }

    int Logfile::newStreamIndex()
    { return m_stream_idx++; }

    void Logfile::writeStreamDeclaration(int stream_index, StreamType type, std::string const& name, std::string const& type_name, std::string const& type_def)
    {
        long payload_size = 1 + 4 + name.size() + 4 + type_name.size();
        if (!type_def.empty())
            payload_size += 4 + type_def.size();

        BlockHeader block_header = { StreamBlockType, 0xFF, stream_index, payload_size };
        *this 
            << block_header
            << static_cast<uint8_t>(type)
            << name
            << type_name;
        if (!type_def.empty())
            *this << type_def;
    }

    void Logfile::writeSampleHeader(int stream_index, base::Time const& realtime, base::Time const& logical, size_t payload_size)
    {
        BlockHeader block_header = { DataBlockType, 0xFF, stream_index, SAMPLE_HEADER_SIZE + payload_size };
        *this << block_header;

        SampleHeader sample_header = { realtime, logical, payload_size, 0 };
        *this << sample_header;
    }

    void Logfile::writeSample(int stream_index, base::Time const& realtime, base::Time const& logical, void* payload_data, size_t payload_size)
    {
        writeSampleHeader(stream_index, realtime, logical, payload_size);
        m_file.write(reinterpret_cast<const char*>(payload_data), payload_size);
    }

    void Logfile::checkFlush() 
    {
	// make sure the buffers are flushed in a regular interval
	const base::Time interval( base::Time::fromSeconds( 1.0 ) );
	if( (base::Time::now() - interval) > m_file.last_flush )
	    m_file.flush();
    }



    StreamLogger::StreamLogger(std::string const& name, const std::string& type_name, Logfile& file)
        : m_name(name), m_type_name(type_name)
        , m_type_def()
        , m_stream_idx(file.newStreamIndex())
        , m_type_size(0)
        , m_file(file)
    { 
        registerStream();
    }

    static size_t getTypeSize(Typelib::Registry const& registry, std::string const& name)
    {
        Typelib::Type const* type = registry.get(name);
        if (type)
            return type->getSize();
        return 0;
    }

    StreamLogger::StreamLogger(std::string const& name, const std::string& type_name, Typelib::Registry const& registry, Logfile& file)
        : m_name(name), m_type_name(type_name)
        , m_type_def(Typelib::PluginManager::save("tlb", registry))
        , m_stream_idx(file.newStreamIndex())
        , m_type_size(getTypeSize(registry, type_name))
        , m_file(file)
    {
        registerStream();
    }

    void StreamLogger::setSampling(base::Time const& period)
    { m_sampling = period; }


    void StreamLogger::registerStream()
    {
        m_file.writeStreamDeclaration(m_stream_idx, DataStreamType, m_name, m_type_name, m_type_def);
    }

    bool StreamLogger::writeSampleHeader(const base::Time& timestamp, size_t size)
    {
        if (!m_last.isNull() && !m_sampling.isNull() && (timestamp - m_last) < m_sampling)
            return false;

        if (size == 0)
            size = m_type_size;

        m_file.writeSampleHeader(m_stream_idx, base::Time::now(), timestamp, size);
        m_last = timestamp;
        return true;
    }

    bool StreamLogger::writeSample(const base::Time& timestamp, size_t size, unsigned char* data)
    {
        if (!m_last.isNull() && !m_sampling.isNull() && (timestamp - m_last) < m_sampling)
            return false;

        if (size == 0)
            size = m_type_size;

        m_file.writeSample(m_stream_idx, base::Time::now(), timestamp, data, size);
        m_last = timestamp;
        return true;
    }

    unsigned char* StreamLogger::getSampleBuffer( size_t size )
    {
	if( m_sample_buffer.size() < size )
	    m_sample_buffer.resize( size );
	return &m_sample_buffer.front();
    }

}
