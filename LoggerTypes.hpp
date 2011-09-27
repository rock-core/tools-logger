#ifndef LOGGER_TYPES_HPP
#define LOGGER_TYPES_HPP

#include <base/time.h>
#include <vector>

namespace logger {
    enum MarkerType{
        SingleEvent=0,
        Stop,
        Start
    };

    struct Marker {
        int id;
        logger::MarkerType type;
        std::string comment;
        base::Time time;
    };

    /** A key-value pair used in the Metadata type
     */
    struct KeyValuePair {
        std::string key;
        std::string value;
    };

    /** This structure is used to create metadata streams, i.e. data that is
     * associated with a certain stream in the logfile
     *
     * The stream is usually named "metadata", and its static metadata contains
     * "stream_type=rock.dynamic_metadata".
     */
    struct Metadata {
        /** The stream to which this metadata is associated
         */
        std::string stream_name;
        /** A set of key-value pairs
         */
        std::vector<KeyValuePair> data;
    };
};


#endif
