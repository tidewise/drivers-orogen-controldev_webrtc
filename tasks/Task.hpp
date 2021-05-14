/* Generated from orogen/lib/orogen/templates/tasks/Task.hpp */

#ifndef CONTROLDEV_WEBSOCKET_TASK_TASK_HPP
#define CONTROLDEV_WEBSOCKET_TASK_TASK_HPP

#include "controldev_websocket/TaskBase.hpp"
#include <controldev/RawCommand.hpp>

#include <vector>
#include <thread>
#include <memory>

namespace seasocks {
    class WebSocket;
    class Server;
}

namespace controldev_webrtc{
    struct Client;
    struct JoystickHandler;
    struct MessageDecoder;

    /*! \class Task
     * \brief The task context provides and requires services. It uses an ExecutionEngine to perform its functions.
     * Essential interfaces are operations, data flow ports and properties. These interfaces have been defined using the oroGen specification.
     * In order to modify the interfaces you should (re)use oroGen and rely on the associated workflow.
     *
     * \details
     * The name of a TaskContext is primarily defined via:
     \verbatim
     deployment 'deployment_name'
         task('custom_task_name','controldev_webrtc::Task')
     end
     \endverbatim
     *  It can be dynamically adapted when the deployment is called with a prefix argument.
     */
    class Task : public TaskBase
    {
        friend class TaskBase;
        friend struct JoystickHandler;

        MessageDecoder *decoder = nullptr;

        seasocks::Server *server = nullptr;
        std::thread *thread = nullptr;
        controldev::RawCommand raw_cmd_obj;
        bool parseIncomingMessage(char const* data);
        bool handleControlMessage();
        bool getIdFromMessage(std::string &out_str);
        bool getTypeFromMessage(std::string &out_str);
        bool getOtherIdFromMessage(std::string &out_str);
        bool getOfferFromMessage(std::string &out_str);
        bool getCandidateFromMessage(std::string &out_str);
        bool updateRawCommand();


        std::vector<Mapping> axis;
        std::vector<ButtonMapping> button;

        bool is_controlling = false;
        int errors = 0;
        int received = 0;

    public:
        rtc::PeerConection *m_pc;
        std::shared_ptr<rtc::DataChannel> m_dc;

        /** TaskContext constructor for Task
         * \param name Name of the task. This name needs to be unique to make it identifiable via nameservices.
         * \param initial_state The initial TaskState of the TaskContext. Default is Stopped state.
         */
        Task(std::string const& name = "controldev_webrtc::Task", TaskCore::TaskState initial_state = Stopped);

        ~Task();

        bool configureHook();

        bool startHook();

        void updateHook();

        void errorHook();

        void stopHook();

        void cleanupHook();
    };
}

#endif
