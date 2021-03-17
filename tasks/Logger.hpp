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

        /** Timestamp output file for automatic renaming capability
         *  Sets _current_file attribute */
        std::string timestampFile(std::string const& file) const;
        
        /** Handle file naming/overwriting if output file already exists */
        bool handleExistingFile(std::string const& file, std::string &currentFile) const;

        bool setFile(std::string const& value);

    private:
        typedef RTT::DataFlowInterface::Ports Ports;

        struct ReportDescription;
        void deleteReport(ReportDescription& report);

        bool addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name);
        bool addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name, std::vector<logger::StreamMetadata> const& metadata);

        void updateLoggers(std::auto_ptr<std::ofstream> &io);
        bool computeCurrentFile(std::string const& file, std::string &currentFile) const;

        /**
         * Stores the 'datasource' of all reported items as properties.
         */
        typedef std::vector<ReportDescription> Reports;
        Reports root;
    };
}

#endif

