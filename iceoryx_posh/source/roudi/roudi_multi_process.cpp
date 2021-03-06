// Copyright (c) 2019 by Robert Bosch GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iceoryx_posh/internal/roudi/roudi_multi_process.hpp"
#include "iceoryx_posh/internal/log/posh_logging.hpp"
#include "iceoryx_posh/internal/runtime/message_queue_interface.hpp"
#include "iceoryx_posh/internal/runtime/runnable_property.hpp"
#include "iceoryx_posh/roudi/introspection_types.hpp"
#include "iceoryx_posh/roudi/memory/roudi_memory_manager.hpp"
#include "iceoryx_posh/runtime/port_config_info.hpp"
#include "iceoryx_utils/cxx/convert.hpp"
#include "iceoryx_utils/fixed_string/string100.hpp"

namespace iox
{
namespace roudi
{
RouDiMultiProcess::RouDiMultiProcess(RouDiMemoryInterface& roudiMemoryInterface,
                                     PortManager& portManager,
                                     const MonitoringMode monitoringMode,
                                     const bool killProcessesInDestructor,
                                     const MQThreadStart mqThreadStart)
    : m_killProcessesInDestructor(killProcessesInDestructor)
    , m_runThreads(true)
    , m_roudiMemoryInterface(&roudiMemoryInterface)
    , m_portManager(&portManager)
    , m_prcMgr(*m_roudiMemoryInterface, portManager)
    , m_mempoolIntrospection(*m_roudiMemoryInterface->introspectionMemoryManager()
                                  .value(), /// @todo create a RouDiMemoryManagerData struct with all the pointer
                             *m_roudiMemoryInterface->segmentManager().value(),
                             m_prcMgr.addIntrospectionSenderPort(IntrospectionMempoolService, MQ_ROUDI_NAME))
    , m_monitoringMode(monitoringMode)
{
    m_processIntrospection.registerSenderPort(
        m_prcMgr.addIntrospectionSenderPort(IntrospectionProcessService, MQ_ROUDI_NAME));
    m_prcMgr.initIntrospection(&m_processIntrospection);
    m_processIntrospection.run();
    m_mempoolIntrospection.start();

    // since RouDi offers the introspection services, also add it to the list of processes
    m_processIntrospection.addProcess(getpid(), MQ_ROUDI_NAME);

    // run the threads
    m_processManagementThread = std::thread(&RouDiMultiProcess::processThread, this);
    pthread_setname_np(m_processManagementThread.native_handle(), "ProcessMgmt");

    if(mqThreadStart == MQThreadStart::IMMEDIATE) {
        startMQThread();
    }

}

RouDiMultiProcess::~RouDiMultiProcess()
{
    shutdown();
}

void RouDiMultiProcess::startMQThread() 
{
    m_processMQThread = std::thread(&RouDiMultiProcess::mqThread, this);
    pthread_setname_np(m_processMQThread.native_handle(), "MQ-processing");
}

void RouDiMultiProcess::shutdown()
{
    m_processIntrospection.stop();
    m_portManager->stopPortIntrospection();
    // roudi will exit soon, stopping all threads
    m_runThreads = false;

    if (m_killProcessesInDestructor)
    {
        m_prcMgr.killAllProcesses();
    }

    if (m_processManagementThread.joinable())
    {
        LogDebug() << "Joining 'ProcessMgmt' thread...";
        m_processManagementThread.join();
        LogDebug() << "...'ProcessMgmt' thread joined.";
    }
    if (m_processMQThread.joinable())
    {
        LogDebug() << "Joining 'MQ-processing' thread...";
        m_processMQThread.join();
        LogDebug() << "...'MQ-processing' thread joined.";
    }
}

void RouDiMultiProcess::cyclicUpdateHook()
{
    // default implementation; do nothing
}

void RouDiMultiProcess::processThread()
{
    while (m_runThreads)
    {
        m_prcMgr.run();

        cyclicUpdateHook();
    }
}

void RouDiMultiProcess::mqThread()
{
    runtime::MqInterfaceCreator roudiMqInterface{MQ_ROUDI_NAME};

    // the logger is intentionally not used, to ensure that this message is always printed
    std::cout << "RouDi is ready for clients" << std::endl;

    while (m_runThreads)
    {
        // read RouDi message queue
        runtime::MqMessage message;
        /// @todo do we really need timedReceive? an alternative solution would be to close the message queue,
        /// which also results in a return from mq_receive, and check the relevant errno and shutdown RouDi
        if (!roudiMqInterface.timedReceive(m_messageQueueTimeout, message))
        {
            // TODO: errorHandling
        }
        else
        {
            auto cmd = runtime::stringToMqMessageType(message.getElementAtIndex(0).c_str());
            std::string processName = message.getElementAtIndex(1);

            processMessage(message, cmd, processName);
        }
    }
}

void RouDiMultiProcess::parseRegisterMessage(const runtime::MqMessage& message,
                                             int& pid,
                                             uid_t& userId,
                                             int64_t& transmissionTimestamp)
{
    cxx::convert::fromString(message.getElementAtIndex(2).c_str(), pid);
    cxx::convert::fromString(message.getElementAtIndex(3).c_str(), userId);
    cxx::convert::fromString(message.getElementAtIndex(4).c_str(), transmissionTimestamp);
}


void RouDiMultiProcess::processMessage(const runtime::MqMessage& message,
                                       const iox::runtime::MqMessageType& cmd,
                                       const std::string& processName)
{
    switch (cmd)
    {
    case runtime::MqMessageType::SERVICE_REGISTRY_CHANGE_COUNTER:
    {
        m_prcMgr.sendServiceRegistryChangeCounterToProcess(processName);
        break;
    }
    case runtime::MqMessageType::REG:
    {
        if (message.getNumberOfElements() != 5)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::REG\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            int pid;
            uid_t userId;
            int64_t transmissionTimestamp;

            parseRegisterMessage(message, pid, userId, transmissionTimestamp);

            registerProcess(processName, pid, {userId}, transmissionTimestamp, getUniqueSessionIdForProcess());
        }
        break;
    }
    case runtime::MqMessageType::IMPL_SENDER:
    {
        if (message.getNumberOfElements() != 5)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::IMPL_SENDER\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            capro::ServiceDescription service(cxx::Serialization(message.getElementAtIndex(2)));
            cxx::Serialization portConfigInfoSerialization(message.getElementAtIndex(4));

            m_prcMgr.addSenderForProcess(processName,
                                         service,
                                         message.getElementAtIndex(3),
                                         iox::runtime::PortConfigInfo(portConfigInfoSerialization));
        }
        break;
    }
    case runtime::MqMessageType::IMPL_RECEIVER:
    {
        if (message.getNumberOfElements() != 5)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::IMPL_RECEIVER\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            capro::ServiceDescription service(cxx::Serialization(message.getElementAtIndex(2)));
            cxx::Serialization portConfigInfoSerialization(message.getElementAtIndex(4));

            m_prcMgr.addReceiverForProcess(processName,
                                           service,
                                           message.getElementAtIndex(3),
                                           iox::runtime::PortConfigInfo(portConfigInfoSerialization));
        }
        break;
    }
    case runtime::MqMessageType::IMPL_INTERFACE:
    {
        if (message.getNumberOfElements() != 4)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::IMPL_INTERFACE\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            capro::Interfaces interface = StringToEInterfaces(message.getElementAtIndex(2));

            m_prcMgr.addInterfaceForProcess(processName, interface, message.getElementAtIndex(3));
        }
        break;
    }
    case runtime::MqMessageType::IMPL_APPLICATION:
    {
        if (message.getNumberOfElements() != 2)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::IMPL_APPLICATION\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            m_prcMgr.addApplicationForProcess(processName);
        }
        break;
    }
    case runtime::MqMessageType::CREATE_RUNNABLE:
    {
        if (message.getNumberOfElements() != 3)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::CREATE_RUNNABLE\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            runtime::RunnableProperty runnableProperty(message.getElementAtIndex(2));
            m_prcMgr.addRunnableForProcess(processName, runnableProperty.m_name);
        }
        break;
    }
    case runtime::MqMessageType::FIND_SERVICE:
    {
        if (message.getNumberOfElements() != 3)
        {
            LogError() << "Wrong number of parameter for \"MqMessageType::FIND_SERVICE\" from \"" << processName
                       << "\"received!";
        }
        else
        {
            capro::ServiceDescription service(cxx::Serialization(message.getElementAtIndex(2)));

            m_prcMgr.findServiceForProcess(processName, service);
        }
        break;
    }
    case runtime::MqMessageType::KEEPALIVE:
    {
        m_prcMgr.updateLivlinessOfProcess(processName);
        break;
    }
    default:
    {
        LogError() << "Unknown MQ Command [" << runtime::mqMessageTypeToString(cmd) << "]";

        m_prcMgr.sendMessageNotSupportedToRuntime(processName);
        break;
    }
    }
}

bool RouDiMultiProcess::registerProcess(
    const std::string& name, int pid, posix::PosixUser user, int64_t transmissionTimestamp, const uint64_t sessionId)
{
    bool monitorProcess = (m_monitoringMode == MonitoringMode::ON);

    return m_prcMgr.registerProcess(name, pid, user, monitorProcess, transmissionTimestamp, sessionId);
}

uint64_t RouDiMultiProcess::getUniqueSessionIdForProcess()
{
    static uint64_t sessionId = 0;
    return ++sessionId;
}

void RouDiMultiProcess::mqMessageErrorHandler()
{
}

} // namespace roudi
} // namespace iox
