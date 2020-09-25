#include "Logger.hpp"
#include <typelib/registry.hh>
#include <rtt/rtt-config.h>
#include <rtt/base/PortInterface.hpp>
#include <rtt/types/Types.hpp>
#include <rtt/typelib/TypelibMarshallerBase.hpp>

#include <rtt/base/InputPortInterface.hpp>

#include "Logfile.hpp"
#include <fstream>
#include <time.h>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace logger;
using namespace std;
using namespace Logging;
using base::Time;
using RTT::types::TypeInfo;
using RTT::log;
using RTT::endlog;
using RTT::Error;
using RTT::Warning;
using RTT::Info;

struct Logger::ReportDescription
{
    std::string name;
    std::string type_name;
    std::string cxx_type_name;

    // We don't own this one
    Typelib::Registry* registry;

    orogen_transports::TypelibMarshallerBase* typelib_marshaller;
    orogen_transports::TypelibMarshallerBase::Handle* marshalling_handle;
    std::vector<logger::StreamMetadata> metadata;
    RTT::base::DataSourceBase::shared_ptr sample;
    RTT::base::InputPortInterface* read_port;
    int time_field_offset;
    Logging::StreamLogger* logger;
};

Logger::Logger(std::string const& name, TaskCore::TaskState initial_state)
    : LoggerBase(name, initial_state)
    , m_registry(0)
    , m_file(0)
{
    m_registry = new Typelib::Registry;
}

Logger::~Logger()
{
    clear();
    delete m_registry;
}

bool Logger::startHook()
{
    if(_file.value().empty())
    {
      log(Error) << "Could not create log file. Task property _file is empty." << endlog();
      return false;
    }

    if (_overwrite_existing_files.get() && _auto_timestamp_files.get())
    {
        log(Error) << "The properties overwrite_existing_files and auto_timestamp_files are both set to true, but are mutually exclusive." << endlog();
        return false;
    }

    _current_file.set(_file.value());

    if (boost::filesystem::exists(_current_file.get()))
    {
        if (!handleExistingFile())
        {
            return false;
        }
    }

    // The registry has been loaded on construction
    // Now, create the output file
    auto_ptr<ofstream> io(new ofstream(_current_file.get().c_str()));
    auto_ptr<Logfile>  file(new Logfile(*io));

    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        it->logger = new Logging::StreamLogger(
                it->name, it->type_name, it->cxx_type_name,*(it->registry), it->metadata, *file);
    }

    m_io   = io.release();
    m_file = file.release();
    return true;
}

void Logger::updateHook()
{
    Time stamp = Time::now();
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        while (it->read_port->read(it->sample, false) == RTT::NewData)
        {
            it->typelib_marshaller->refreshTypelibSample(it->marshalling_handle);
            if (!it->logger)
            {
                it->logger = new Logging::StreamLogger(
                        it->name, it->type_name, it->cxx_type_name, *m_registry, it->metadata, *m_file);
            }

            size_t payload_size = it->typelib_marshaller->getMarshallingSize(it->marshalling_handle);
	    // in case there is a time field specified for the datatype, use that
	    // one to stamp the log sample instead of base::Time::now()
	    if( it->time_field_offset >= 0 )
		stamp = *reinterpret_cast<base::Time*>(
			reinterpret_cast<char*>(
			    it->sample->getRawPointer()) + it->time_field_offset);

            it->logger->writeSampleHeader(stamp, payload_size);
            it->typelib_marshaller->marshal(it->logger->getStream(), it->marshalling_handle);
        }
    }
}

void Logger::stopHook()
{
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        delete it->logger;
        it->logger = 0;
    }
    m_io->flush();
    delete m_file;
    m_file = 0;
    delete m_io;
    m_io = 0;
}


bool Logger::createLoggingPort(const std::string& portname, const std::string& typestr, std::vector<logger::StreamMetadata> const& metadata)
{
    RTT::types::TypeInfoRepository::shared_ptr ti = RTT::types::TypeInfoRepository::Instance();
    RTT::types::TypeInfo* type = ti->type(typestr);
    if (! type)
    {
	cerr << "cannot find " << typestr << " in the type info repository" << endl;
	return false;
    }
    
    RTT::base::PortInterface *pi = ports()->getPort(portname);
    if(pi) {
	cerr << "port with name " << portname << " already exists" << endl;
	return false;
    }

    RTT::base::InputPortInterface *ip= type->inputPort(portname);
    return addLoggingPort(ip, portname, metadata);
}


bool Logger::reportComponent( const std::string& component ) {
    // Users may add own data sources, so avoid duplicates
    //std::vector<std::string> sources                = comp->data()->getNames();
    TaskContext* comp = this->getPeer(component);
    if ( !comp )
    {
        log(Error) << "no such component " << component << endlog();
        return false;
    }

    Ports ports   = comp->ports()->getPorts();
    for (Ports::iterator it = ports.begin(); it != ports.end() ; ++it)
        this->reportPort( component, (*it)->getName() );
    return true;
}


bool Logger::unreportComponent( const std::string& component ) {
    TaskContext* comp = this->getPeer(component);
    if (!comp)
    {
        log(Error) << "no such component " << component << endlog();
        return false;
    }

    Ports ports   = comp->ports()->getPorts();
    for (Ports::iterator it = ports.begin(); it != ports.end() ; ++it) {
        this->unreportPort(component, (*it)->getName());
    }
    return true;
}

// report a specific connection.
bool Logger::reportPort(const std::string& component, const std::string& port ) {
    TaskContext* comp = this->getPeer(component);
    if ( !comp )
    {
        log(Error) << "no such component " << component << endlog();
        return false;
    }

    RTT::base::OutputPortInterface* writer = dynamic_cast<RTT::base::OutputPortInterface*>(comp->ports()->getPort(port));
    if ( !writer )
    {
        log(Error) << "component " << component << " does not have a port named " << port << ", or it is a read port" << endlog();
        return false;
    }

    
    std::string portname(component + "." + port);
    RTT::base::PortInterface *pi = ports()->getPort(portname);
    
    if(pi) // we are already reporting this port
    {
        log(Info) << "port " << port << " of component " << component << " is already logged" << endlog();
        return true;
    }

    // Create the corresponding read port
    RTT::base::InputPortInterface* reader = static_cast<RTT::base::InputPortInterface*>(writer->antiClone());
    reader->setName(portname);
    writer->createBufferConnection(*reader, 25);

    return addLoggingPort(reader, portname);
}

bool Logger::addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name)
{
    std::vector<logger::StreamMetadata> metadata;
    return addLoggingPort(reader, stream_name, metadata);
}
bool Logger::addLoggingPort(RTT::base::InputPortInterface* reader, std::string const& stream_name, std::vector<logger::StreamMetadata> const& metadata)
{
    TypeInfo const* type = reader->getTypeInfo();
    orogen_transports::TypelibMarshallerBase* transport =
        dynamic_cast<orogen_transports::TypelibMarshallerBase*>(type->getProtocol(orogen_transports::TYPELIB_MARSHALLER_ID));
    if (! transport)
    {
        log(Error) << "cannot report ports of type " << type->getTypeName() << " as no typekit generated by orogen defines it" << endlog();
        return false;
    }

    m_registry->merge(transport->getRegistry());
    if (! m_registry->has(transport->getMarshallingType()))
    {
        log(Error) << "cannot report ports of type " << type->getTypeName() << " as I can't find a typekit Typelib registry that defines it" << endlog();
        return false;
    }

    ports()->addEventPort(reader->getName(), *reader);

    // if there is a request to use the time field of a datatype 
    // we need to look up the offset to that field in the registry
    std::string time_name;
    BOOST_FOREACH( const StreamMetadata& md_item, metadata )
    {
	if( md_item.key == "rock_timestamp_field" )
	    time_name = md_item.value;
    }
    
    int time_field_offset = -1;
    if( !time_name.empty() )
    {
	const Typelib::Type *tl_type = m_registry->get( type->getTypeName() );
	if( !tl_type )
	{
	    log(Error) << "could not get typelib type for " << type->getTypeName() << endlog();
	    return false;
	}

	const Typelib::Compound *tl_compound = dynamic_cast<const Typelib::Compound*>( tl_type );
	if( !tl_compound )
	{
	    log(Error) << "typelib type for " << type->getTypeName() << " is not a compound type." << endlog();
	    return false;
	}

	const Typelib::Field *tl_field = tl_compound->getField( time_name );
	if( !tl_field || tl_field->getType().getName() != "/base/Time" )
	{
	    log(Error) << type->getTypeName() << " does not have a " << time_name << " field of type base::Time." << endlog();
	    return false;
	}

	time_field_offset = tl_field->getOffset();
    }

    try {
        ReportDescription report;
        report.name         = reader->getName();
        report.type_name    = transport->getMarshallingType();
        report.cxx_type_name = type->getTypeName();
        report.read_port    = reader;
        report.registry     = m_registry->minimal(report.type_name);
        report.marshalling_handle = transport->createSample();
        report.typelib_marshaller = transport;
        report.metadata = metadata;
        report.sample = transport->getDataSource(report.marshalling_handle);
	report.time_field_offset = time_field_offset;
        report.logger       = NULL;

        root.push_back(report);
    } catch ( RTT::internal::bad_assignment& ba ) {
        return false;
    }
    return true;
}

void Logger::clear()
{
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        ports()->removePort(it->read_port->getName());
        deleteReport(*it);
    }
    root.clear();
}

void Logger::deleteReport(ReportDescription& report)
{
    delete report.read_port;
    report.typelib_marshaller->deleteHandle(report.marshalling_handle);
    delete report.logger;
    delete report.registry;
}

bool Logger::removeLoggingPort(std::string const& port_name)
{
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        if ( it->read_port->getName() == port_name )
        {
            ports()->removePort(port_name);
            deleteReport(*it);
            root.erase(it);
            return true;
        }
    }

    return false;
}

bool Logger::unreportPort(const std::string& component, const std::string& port )
{

    std::string name = component + "." + port;
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        if ( it->name == name )
        {
            removeLoggingPort(it->read_port->getName());
            return true;
        }
    }
    return false;
}

void Logger::snapshot()
{
    // execute the copy commands (fast).
    if( this->engine()->getActivity() )
        this->engine()->getActivity()->trigger();
}

void Logger::renameFile()
{
    // create timestamp
    time_t now = time(0);
    tm *t_ptr = localtime(&now);
    char suffix[21];
    strftime(suffix, sizeof(suffix), "%F_%H-%M-%S", t_ptr);
    // append suffix to previous _current_file.get()
    vector<string> strs;
    boost::split(strs, _file.value(), boost::is_any_of("."));
    if ( strs.size() == 1) {
        strs.insert(strs.end(), std::string(suffix));
    } else {
        strs.insert(strs.end()-1, std::string(suffix));
    }
    std::string timestamped_str = boost::algorithm::join(strs, ".");
    _current_file.set(timestamped_str);
}

bool Logger::handleExistingFile()
{
    if (!_overwrite_existing_files.get() && !_auto_timestamp_files.get())
    {
        log(Error) << "File " << _current_file.get() << " already exists. Neither overwrite nor auto-timestamp allowed by task properties." << endlog();
        return false;
    }

    if (_overwrite_existing_files.get() && !_auto_timestamp_files.get())
    {
        log(Info) << "File " << _current_file.get() << " already exists. Overwriting existing file." << endlog();
        return true;
    }

    if (!_overwrite_existing_files.get() && _auto_timestamp_files.get())
    {
        log(Warning) << "File " << _current_file.get() << " already exists." << endlog();
        renameFile();
        while(boost::filesystem::exists(_current_file.get()))
        {
            log(Warning) << "Timestamped file " << _current_file.get() << " already exists. Retrying in 1 second." << endlog();
            usleep(1*1000000);
            renameFile();
        }
        log(Warning) << "Writing log to " << _current_file.get() << " instead." << endlog();
        return true;
    }
}
