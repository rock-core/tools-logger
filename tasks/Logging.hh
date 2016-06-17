/* 
 * Copyright (c) 2008 DFKI
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

#ifndef OROCOS_LOGGER_H
#define OROCOS_LOGGER_H

#include <base/Time.hpp>
#include <stdint.h>

/** Log files are made of blocks. Each block begins with a common block header. 
 * Block are organized in streams, which all begin with a data stream declaration.
 * Data blocks begin  with a data stream declaration block, which includes the
 * stream name, the stream type name and (optionally) a type registry which defines
 * the type, in Typelib XML format. If no registry is available, the type registry 
 * is of zero size.
 *
 * All pocosim-defined values are saved in little endian. Only the data payload
 * in data blocks is saved in native endian. The log reader, pocosim-log, is
 * able to swap endianness if needed. 
 *
 * <table>
 *  <caption>Prologue (12 bytes)</caption>
 *  <tr><td>Offset</td><td>Size</td><td>Field</td></tr>
 *  <tr><td>+0</td><td>7</td><td>POCOSIM (see Logging::FORMAT_MAGIC) </td></tr>
 *  <tr><td>+7</td><td>4</td><td>Format version (currently 2, see Logging::FORMAT_VERSION)</td></tr>
 *  <tr><td>+11</td><td>1</td><td>Endianness (1 = big, 0 = little)</td></tr>
 * </table>
 *
 * <table>
 *  <caption>Block Header (8 bytes)</caption>
 *  <tr><td>Offset</td><td>Size</td><td>Field</td></tr>
 *  <tr><td>+0</td><td>1</td><td>block type (Logging::BlockType)</td></tr>
 *  <tr><td>+1</td><td>1</td><td>padding</td></tr>
 *  <tr><td>+2</td><td>2</td><td>stream index</td></tr>
 *  <tr><td>+4</td><td>4</td><td>data size</td></tr>
 * </table>
 *
 * <table>
 *  <caption>Control Stream Declaration Block</caption>
 *  <tr><td>Offset</td><td>Size</td><td>Field</td></tr>
 *  <tr><td>+0  </td><td>1</td><td>Logging::ControlStreamType</td></tr>
 * </table>
 *
 * <table>
 *  <caption>Data Stream Declaration Block</caption>
 *  <tr><td>Offset</td><td>Size</td><td>Field</td></tr>
 *  <tr><td>+0  </td><td>1</td><td>Logging::DataStreamType</td></tr>
 *  <tr><td>+1  </td><td>4</td><td>stream name size</td></tr>
 *  <tr><td>+3  </td><td>i</td><td>stream name</td></tr>
 *  <tr><td></td><td>4</td><td>stream type name size</td></tr>
 *  <tr><td></td><td></td><td>stream type name</td></tr>
 *  <tr><td></td><td>4</td><td>type registry size</td></tr>
 *  <tr><td></td><td></td><td>type registry</td></tr>
 * </table>
 *
 * <table>
 *  <caption>Sample Block (25 bytes)</caption>
 *  <tr><td>Offset</td><td>Size</td><td>Field</td></tr>
 *  <tr><td>+4 </td><td>4</td><td>real time (sec)</td></tr>
 *  <tr><td>+8 </td><td>4</td><td>real time (usec)</td></tr>
 *  <tr><td>+20</td><td>4</td><td>logical time (sec)</td></tr>
 *  <tr><td>+24</td><td>4</td><td>logical time (usec)</td></tr>
 *  <tr><td>+32</td><td>4</td><td>data size</td></tr>
 *  <tr><td>+36</td><td>1</td><td>compression flag</td></tr>
 *  <tr><td>+37</td><td>data_size</td><td>data</td></tr>
 * </table>
 */

namespace Logging
{
    // Version 1 is the same format without a prologue, and without the compression
    // flag in data blocks
    static const int FORMAT_VERSION = 2;
    extern const char FORMAT_MAGIC[];

    struct Prologue
    {
	char magic[7];
	uint8_t  padding;

	uint32_t version;
	uint32_t flags;
	Prologue()
	    : padding(0), version(0), flags(0)
	{
	    for (int i = 0; i < 7; ++i)
		magic[i] = FORMAT_MAGIC[i];
	}
    };

    struct BlockHeader
    {   
        uint8_t  type;
        uint8_t  padding;
        uint16_t stream_idx;
        uint32_t data_size;
    };
    static const int BLOCK_HEADER_SIZE = 8;

    struct SampleHeader
    {
        base::Time realtime;
        base::Time timestamp;
        uint32_t  data_size;
	uint8_t   compressed;
    };
    static const int SAMPLE_HEADER_SIZE = 21;

    enum BlockType   
    { 
	UnknownBlockType = 0,
	StreamBlockType = 1,  /// stream declaration block
	DataBlockType = 2,    /// a data block in an already declared stream
	ControlBlockType = 3  /// a control block
    };
    enum StreamType 
    { 
	UnknownStreamType = 0, 
	DataStreamType = 1, 
	ControlStreamType = 2
    };
    enum CommandType 
    { 
	SetTimeBase = 0,
	SetTimeOffset = 1
    };

};

#endif

