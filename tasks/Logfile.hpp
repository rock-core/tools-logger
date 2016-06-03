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


#ifndef LOGOUTPUT_H
#define LOGOUTPUT_H

#include "Logging.hh"

#include <vector>
#include <iosfwd>
#include <typelib/endian_swap.hh>
#include <logger/LoggerTypes.hpp>

namespace Typelib {
    class Registry;
}
namespace Logging
{
    class Logfile
    {
        template<class T> friend Logfile& operator << (Logfile& output, const T& value);
        static const size_t nstream = static_cast<size_t>(-1);

        std::ostream& m_stream;
        int m_stream_idx;

    private:
        template<class T>
        void write(const T& data) 
        { 
	    T little_endian = Typelib::Endian::to_little(data);
	    m_stream.write( reinterpret_cast<const char*>(&little_endian), sizeof(T) ); 
	}
       

    public:
        Logfile(std::ostream& stream);

        std::ostream& getStream();

        int newStreamIndex();

        void writeStreamDeclaration(int stream_index, StreamType type,
                std::string const& name, std::string const& type_name,
                std::string const& cxx_type_name, std::string const& type_def,
                std::vector<logger::StreamMetadata> const& metadata);
        void writeSampleHeader(int stream_index, base::Time const& realtime, base::Time const& logical, size_t size);
        void writeSample(int stream_index, base::Time const& realtime, base::Time const& logical, void* payload_data, size_t payload_size);
    };

    namespace details
    {
        template <typename T> struct static_failure;
    }

    /** Writes the file prologue */
    void writePrologue(std::ostream& stream);

    template<class T>
    Logfile& operator << (Logfile& output, const T& value)
    {
        details::static_failure<T> IF_YOU_HAVE_AN_ERROR_HERE_YOU_USED_THE_STREAM_OPERATOR_WITH_AN_INVALID_TYPE;
        return output;
    }

    template<>
    inline Logfile& operator << (Logfile& output, const uint8_t& value)
    { output.write(value); return output; }

    template<>
    inline Logfile& operator << (Logfile& output, const uint16_t& value)
    { output.write(value); return output; }

    template<>
    inline Logfile& operator << (Logfile& output, const uint32_t& value)
    { output.write(value); return output; }

    template<>
    inline Logfile& operator << (Logfile& output, const std::string& value)
    {
        uint32_t length(value.length());
        output.write(length);
        output.m_stream.write(reinterpret_cast<const char*>(value.c_str()), length);
        return output;
    }

    template<>
    inline Logfile& operator << (Logfile& output, const base::Time& time)
    {
	timeval tv = time.toTimeval();	
	output << (uint32_t)tv.tv_sec << (uint32_t)tv.tv_usec;
        return output;
    }

    template<>
    inline Logfile& operator << (Logfile& output, const BlockHeader& header)
    {
	output
	    << header.type
	    << header.padding
	    << header.stream_idx
	    << header.data_size;
	return output;
    }

    template<>
    inline Logfile& operator << (Logfile& output, const SampleHeader& header)
    {
	output
	    << header.realtime
	    << header.timestamp
	    << header.data_size
	    << header.compressed;
	return output;
    }

    template<>
    inline Logfile& operator << (Logfile& output, const BlockType& type)
    { return output << static_cast<uint8_t>(type); }

    template<>
    inline Logfile& operator << (Logfile& output, const StreamType& type)
    { return output << static_cast<uint8_t>(type); }

    template<>
    inline Logfile& operator << (Logfile& output, const CommandType& type)
    { return output << static_cast<uint8_t>(type); }


    /** The objects of this class can be used to easily log samples for a given
     * stream.
     */
    class StreamLogger
    {
        std::string const m_name;
        std::string const m_type_name;
        std::string const m_cxx_type_name;
        std::string const m_type_def;
        std::vector<logger::StreamMetadata> m_metadata;
        int const m_stream_idx;
        size_t const m_type_size;
        base::Time m_sampling;
        base::Time m_last;

        Logfile m_file;

    public:
        /** Create a new logger, with no type definition
         *
         * This logger will define a new named stream in \c stream, but without
         * saving the type definition as the other constructor would.
         *
         * @arg name the stream name
         * @arg type_name the stream type name
         * @arg stream the stream object
         */
        StreamLogger(std::string const& name, std::string const& type_name, std::string const& cxx_type_name, std::vector<logger::StreamMetadata> const& metadata, Logfile& file);

        /** Create a new logger, with type definition
         *
         * This logger will define a new named stream in \c stream, in which a type
         * definition string is saved, allowing to re-read the log file without
         * having to care about the type definition and (possible) type changes.
         *
         * @arg name the stream name
         * @arg type_name the stream type name
         * @arg registry the Typelib registry which defines the associated type name
         * @arg stream the stream object
         */
        StreamLogger(std::string const& name, std::string const& type_name, std::string const& cxx_type_name, Typelib::Registry const& type_def, std::vector<logger::StreamMetadata> const& metadata, Logfile& file);

        /** Registers the sample stream in the output file */
        void registerStream();

        /** Sets the sampling period. It is used in update() to filter out
         * samples that are too near from each other
         */
        void setSampling(base::Time const& period);

        bool writeSampleHeader(const base::Time& timestamp, size_t size);

        std::ostream& getStream();
    };
}

#endif

