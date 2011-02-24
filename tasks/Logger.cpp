#include "Logger.hpp"
#include <typelib/registry.hh>
#include <rtt/rtt-config.h>
#include <utilmm/configfile/pkgconfig.hh>
#include <utilmm/stringtools.hh>
#include <typelib/pluginmanager.hh>
#include <rtt/base/PortInterface.hpp>
#include <rtt/types/Types.hpp>
#include <rtt/typelib/TypelibMarshallerBase.hpp>

#include <rtt/base/InputPortInterface.hpp>

#include "Logfile.hpp"
#include <fstream>

using namespace logger;
using namespace std;
using namespace Logging;
using base::Time;
using RTT::types::TypeInfo;
using RTT::log;
using RTT::endlog;
using RTT::Error;
using RTT::Info;

struct Logger::ReportDescription
{
    std::string name;
    std::string type_name;
    Typelib::Registry* registry;
    orogen_transports::TypelibMarshallerBase* typelib_marshaller;
    orogen_transports::TypelibMarshallerBase::Handle* marshalling_handle;
    RTT::base::DataSourceBase::shared_ptr sample;
    RTT::base::InputPortInterface* read_port;
    Logging::StreamLogger* logger;
};

Logger::Logger(std::string const& name, TaskCore::TaskState initial_state)
    : LoggerBase(name, initial_state)
    , m_registry(0)
    , m_file(0)
{
    loadRegistry();

    // Register a fake protocol object to be able to marshal samples
    // "the typelib way"
}

Logger::~Logger()
{
    delete m_registry;
}

bool Logger::startHook()
{
    if (_file.value().empty())
        return false;

    // The registry has been loaded on construction
    // Now, create the output file
    auto_ptr<Logfile>  file( new Logfile(_file.value()) );

    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        it->logger = new Logging::StreamLogger(
                it->name, it->type_name, *(it->registry), *file);
    }

    m_file = file.release();
    return true;
}

void Logger::updateHook()
{
    Time stamp = Time::now();
    bool did_something = true;
    while(did_something)
    {
        did_something = false;
        for (Reports::iterator it = root.begin(); it != root.end(); ++it)
        {
            if (it->read_port->read(it->sample, false) == RTT::NewData)
            {
                did_something = true;
                it->typelib_marshaller->refreshTypelibSample(it->marshalling_handle);
                if (!it->logger)
                {
                    it->logger = new Logging::StreamLogger(
                            it->name, it->type_name, *m_registry, *m_file);
                }

                size_t payload_size = it->typelib_marshaller->getMarshallingSize(it->marshalling_handle);
                unsigned char *buf = it->logger->getSampleBuffer( payload_size );
                it->typelib_marshaller->marshal(buf, payload_size, it->marshalling_handle);
                it->logger->writeSample(stamp, payload_size, buf);
            }
        }
    }

    // will flush the buffers in a regular interval to prevent data loss
    // and potentially save fingers from freezing... again...
    m_file->checkFlush();
}

void Logger::stopHook()
{
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        delete it->logger;
        it->logger = 0;
    }
    delete m_file;
    m_file = 0;
}


bool Logger::createLoggingPort(const std::string& portname, const std::string& typestr)
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
    return addLoggingPort(ip, portname);
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
    TypeInfo const* type = reader->getTypeInfo();
    orogen_transports::TypelibMarshallerBase* transport =
        dynamic_cast<orogen_transports::TypelibMarshallerBase*>(type->getProtocol(orogen_transports::TYPELIB_MARSHALLER_ID));
    if (! transport)
    {
        log(Error) << "cannot report ports of type " << type->getTypeName() << " as no typekit generated by orogen defines it" << endlog();
        return false;
    }

    if (! m_registry->has(transport->getMarshallingType()))
    {
        log(Error) << "cannot report ports of type " << type->getTypeName() << " as I can't find a typekit Typelib registry that defines it" << endlog();
        return false;
    }

    ports()->addEventPort(reader->getName(), *reader);

    try {
        ReportDescription report;
        report.name         = reader->getName();
        report.type_name    = transport->getMarshallingType();
        report.read_port    = reader;
        report.registry     = m_registry->minimal(report.type_name);
        report.marshalling_handle = transport->createSample();
        report.typelib_marshaller = transport;
        report.sample = transport->getDataSource(report.marshalling_handle);
        report.logger       = NULL;

        root.push_back(report);
    } catch ( RTT::internal::bad_assignment& ba ) {
        return false;
    }
    return true;
}

bool Logger::removeLoggingPort(std::string const& port_name)
{
    for (Reports::iterator it = root.begin(); it != root.end(); ++it)
    {
        if ( it->read_port->getName() == port_name )
        {
            ports()->removePort(port_name);
            delete it->read_port;
            it->typelib_marshaller->deleteHandle(it->marshalling_handle);
            delete it->logger;
            delete it->registry;
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

#define TASK_LIBRARY_NAME_PATTERN_aux0(target) #target
#define TASK_LIBRARY_NAME_PATTERN_aux(target) "-typekit-" TASK_LIBRARY_NAME_PATTERN_aux0(target)
#define TASK_LIBRARY_NAME_PATTERN TASK_LIBRARY_NAME_PATTERN_aux(OROCOS_TARGET)
void Logger::loadRegistry()
{
    string const pattern = TASK_LIBRARY_NAME_PATTERN;
    delete m_registry;
    m_registry = new Typelib::Registry;

    // List all known toolkits and load the ones that have a .tlb file defined.
    list<string> packages = utilmm::pkgconfig::packages();
    for (list<string>::const_iterator it = packages.begin(); it != packages.end(); ++it)
    {
        if (it->size() > pattern.size() && string(*it, it->size() - pattern.size()) == pattern)
        {
            utilmm::pkgconfig pkg(*it);
            string tlb = pkg.get("type_registry");
            if (!tlb.empty())
            {
                try {
                    auto_ptr<Typelib::Registry> registry( Typelib::PluginManager::load("tlb", tlb) );
                    m_registry->merge(*registry.get());
                    log(Info) << "loaded " << tlb << " in the data logger registry" << endlog();
                }
                catch(...)
                {
                    log(Error) << "cannot load registry file " << tlb << endlog();
                }
            }
        }
    }
}

