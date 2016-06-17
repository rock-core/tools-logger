#ifndef LOGGER_TYPES_HPP
#define LOGGER_TYPES_HPP

#include <base/Time.hpp>

namespace logger {
    /** DEPRECATED. This predated the annotation / metadata system */
    enum MarkerType{
        SingleEvent=0,
        Stop,
        Start
    };

    /** DEPRECATED. This predated the annotation / metadata system */
    struct Marker {
        int id;
        logger::MarkerType type;
        std::string comment;
        base::Time time;
    };

    /** Structure used to store metadata information / events in log files. It
     * is used by creating a new log port, usually called "annotations", with
     * the metadata rock_stream_type=annotations and this type
     *
     * When metadata is associated with a particular stream, this scheme assumes
     * that stream names are unique in a single file after discussion, we
     * decided that it is a sane constraint, as most of the tooling would be
     * made a lot more complex otherwise. Moreover, the stream index is not
     * stable if you extract data out of a file.
     */
    struct Annotations
    {
        /** The time of this annotation. Assign to a null type (base::Time()) if
         * no specific time is associated
         */
        base::Time time;
        /** If the annotation applies to a particular stream, the name of the
         * stream. Can be left empty for general information
         */
        std::string stream_name;
        /** The application-specific key. See
         * http://rock.opendfki.de/wiki/WikiStart/OngoingWork/LogMetadata for
         * some standard annotations
         */
        std::string key;
        /** The application-specific value. See
         * http://rock.opendfki.de/wiki/WikiStart/OngoingWork/LogMetadata for
         * some standard annotations
         */
        std::string value;
    };

    /** Structure passed to createLoggingPort to add metadata to streams
     */
    struct StreamMetadata
    {
        std::string key;
        std::string value;
    };
};


#endif
