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

using namespace std;
namespace endian = utilmm::endian;

BOOST_STATIC_ASSERT(( sizeof(Logging::Prologue) == 16 ));

const char Logging::FORMAT_MAGIC[] = "POCOSIM";

void Logging::writePrologue(std::ostream& stream)
{
    Prologue prologue;
    prologue.version    = endian::to_little<uint32_t>(Logging::FORMAT_VERSION);
#if defined(WORDS_BIGENDIAN)
    prologue.flags = 1;
#else
    prologue.flags = 0;
#endif

    stream.write(reinterpret_cast<char*>(&prologue), sizeof(prologue));
}


namespace Logging
{
    Logfile::Logfile(std::ostream& stream)
        : m_stream(stream)
        , m_stream_idx(0)
    {
        writePrologue(stream);
    }

    int Logfile::newStreamIndex()
    { return m_stream_idx++; }

    void Logfile::writeStreamDeclaration(int stream_index, StreamType type,
            std::string const& name, std::string const& type_name,
            std::string const& type_def,
            std::vector<logger::StreamMetadata> const& metadata)
    {
        // Do the marshalling of the metadata into a YAML-like document
        std::string metadata_yaml;
        {
            std::ostringstream yaml_io;
            for (unsigned int i = 0; i < metadata.size(); ++i)
                yaml_io << metadata[i].key << ": " << metadata[i].value << "\n";
            metadata_yaml = yaml_io.str();
        }

        long payload_size = 1 + 4 + name.size() + 4 + type_name.size()
            + 4 + type_def.size()
            + 4 + metadata_yaml.size();

        BlockHeader block_header = { StreamBlockType, 0xFF, stream_index, payload_size };
        *this 
            << block_header
            << static_cast<uint8_t>(type)
            << name
            << type_name;
        if (!type_def.empty())
        {
            *this << type_def;
            *this << metadata_yaml;
        }
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
        m_stream.write(reinterpret_cast<const char*>(payload_data), payload_size);
    }

    std::ostream& Logfile::getStream() { return m_stream; }





    StreamLogger::StreamLogger(std::string const& name, const std::string& type_name, std::vector<logger::StreamMetadata> const& metadata, Logfile& file)
        : m_name(name), m_type_name(type_name)
        , m_type_def()
        , m_metadata(metadata)
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

    StreamLogger::StreamLogger(std::string const& name, const std::string& type_name, Typelib::Registry const& registry, std::vector<logger::StreamMetadata> const& metadata, Logfile& file)
        : m_name(name), m_type_name(type_name)
        , m_type_def(Typelib::PluginManager::save("tlb", registry))
        , m_metadata(metadata)
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
        m_file.writeStreamDeclaration(m_stream_idx, DataStreamType,
                m_name, m_type_name, m_type_def, m_metadata);
    }

    std::ostream& StreamLogger::getStream()
    { return m_file.getStream(); }

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
}

