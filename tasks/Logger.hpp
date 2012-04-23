#ifndef LOGGER_LOGGER_TASK_HPP
#define LOGGER_LOGGER_TASK_HPP

#include "logger/LoggerBase.hpp"
#include <rtt/os/MutexLock.hpp>

namespace orogen_transports {
    class TypelibMarshallerBase;
}
namespace Logging {
    class StreamLogger;
    class Logfile;
}
namespace Typelib {
    class Registry;
}
namespace RTT {
    namespace base {
        class InputPortInterface;
    }
}

namespace logger {
    class Logger : public LoggerBase
    {
	friend class LoggerBase;

    protected:

        Typelib::Registry* m_registry;
        std::ofstream*     m_io;
        Logging::Logfile*  m_file;
    
        bool startHook();
        void updateHook();
        void stopHook();

    public:
        Logger(std::string const& name = "logger::Logger", TaskCore::TaskState initial_state = Stopped);
        ~Logger();

        /* Handler for the createLoggingPort operation
         */
        virtual bool createLoggingPort(::std::string const & port_name,
                ::std::string const & type_name,
                ::std::vector< ::logger::StreamMetadata > const & metadata);

        /**
         * Report all the data ports of a component.
         */
        bool reportComponent( const std::string& component );
            
        /**
         * Unreport the data ports of a component.
         */
        bool unreportComponent( const std::string& component );

        /**
         * Report a specific data port of a component.
         */
        bool reportPort(const std::string& component, const std::string& port );

        /**
         * Unreport a specific data port of a component.
         */
        bool unreportPort(const std::string& component, const std::string& port );

        /**
         * This real-time function makes copies of the data to be
         * reported.
         */
        void snapshot();

        /** Removes all reporting ports and channels */
        void clear();

        bool removeLoggingPort(std::string const& stream_name);

    private:
        typedef RTT::DataFlowInterface::Ports Ports;

        struct ReportDescription;
        void deleteReport(ReportDescription& report);

        bool addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name);
        bool addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name, std::vector<logger::StreamMetadata> const& metadata);

        /**
         * Stores the 'datasource' of all reported items as properties.
         */
        typedef std::vector<ReportDescription> Reports;
        Reports root;
    };
}

#endif

