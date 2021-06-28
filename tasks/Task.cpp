/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include "rtc/rtc.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/Server.h>
#include <seasocks/WebSocket.h>

#include <string.h>
#include <iostream>
#include <json/json.h>

#include <list>
#include <map>

using namespace controldev_webrtc;
using namespace controldev;
using namespace seasocks;

struct controldev_webrtc::Client {
    WebSocket *connection = nullptr;
    std::string id = "";
    Json::FastWriter fast;

    bool isEmpty() {
        return connection == nullptr;
    }


};
struct controldev_webrtc::JoystickHandler : WebSocket::Handler {
    Client pending;
    Client controlling;
    Task *task = nullptr;
    Json::FastWriter fast;
    Statistics statistic;
    std::shared_ptr<rtc::DataChannel> new_dc(nullptr);
    rtc::PeerConnection *new_pc = nullptr;
    bool last_offer_successful = false;

    void handleNewConnection(Client newer, Client other) {
        Json::Value feedback;
        if (other.isEmpty()) {
            feedback["connection_state"]["state"] = "new";
        } else {
            feedback["connection_state"]["state"] = "lost";
            feedback["connection_state"]["peer"] = newer.id.empty() ? "pending connection"
                                                                      : newer.id;
            other.connection->send(fast.write(feedback));
            other.connection->close();
            feedback["connection_state"]["state"] = "stolen";
            feedback["connection_state"]["peer"] = other.id.empty() ? "pending connection"
                                                                      : other.id;
        }
        newer.connection->send(fast.write(feedback));
    };

    void onConnect(WebSocket *socket) override {
        Client new_pending;
        new_pending.connection = socket;
        handleNewConnection(new_pending, pending);
        pending = new_pending;
    }

    void onData(WebSocket *socket, const char *data) override {
        Json::Value msg;
        if (socket != pending.connection) {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
            return;
        }

        if (!task->parseIncomingMessage(data)) {
            msg["error"] = "parsing failed";
            socket->send(fast.write(msg));
            return;
        }

        std::string type;
        if (!task->getTypeFromMessage(type)) {
            msg["error"] = "type field missing";
            socket->send(fast.write(msg));
            return;
        }

        switch (type) {
            case "offer":
                onOffer(socket);
                break;
            case "candidate":
                onCandidate(socket);
                break;
            default:
                msg["error"] = "unknown type";
                socket->send(fast.write(msg));
                break;
        }
    }

    void onDisconnect(WebSocket *socket) override {
        if (controlling.connection == socket) {
            controlling = Client();
        } else if (pending.connection == socket) {
            pending = Client();
        } else {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
        }
    }

    void onOffer(WebSocket *socket) {
        Json::Value msg;

        /** Usually we would like to free the memory associated with new_pc from last offer before getting a new offer,
         * but if the last offer was succesful, i.e. new_pc reached the connected state, then new_pc would
         * be pointing to the same memory location as task->m_pc, so we just set new_pc to nullptr in this case.
         */
        if (last_offer_successful) {
            new_pc = nullptr;
            last_offer_successful = false;
        }
        else {
            delete new_pc;
        }

        std::string id;
        if (!task->getIdFromMessage(id)) {
            msg["type"] = "offer";
            msg["error"] = "id field missing";
            socket->send(fast.write(msg));
            return;
        }

        std::string otherId;
        if (!task->getOtherIdFromMessage(otherId)) {
            msg["type"] = "offer";
            msg["error"] = "other_id field missing";
            socket->send(fast.write(msg));
            return;
        }

        if (otherId != _component_id.get()) {
            msg["type"] = "offer";
            msg["error"] = "wrong target id";
            socket->send(fast.write(msg));
            return;
        }

        std::string offer;
        if (!task->getOfferFromMessage(offer)) {
            msg["type"] = "offer";
            msg["error"] = "sdp field missing";
            socket->send(fast.write(msg));
            return;
        }

        createPeerConnection(socket, id);

        try
        {
            new_pc->setRemoteDescription(rtc::Description(offer))
        }
        catch(const std::exception& e)
        {
            delete new_pc;
            LOG_ERROR_S << e.what();
            LOG_ERROR_S << std::endl;
            msg["type"] = "offer";
            msg["error"] = e.what();
            socket->send(fast.write(msg));
        }
    }

    void onCandidate(WebSocket *socket) {
        Json::Value msg;

        if (!new_pc) {
            msg["type"] = "candidate";
            msg["error"] = "must send offer before candidates";
            socket->send(fast.write(msg));
            return;
        }

        std::string id;
        if (!task->getIdFromMessage(id)) {
            msg["type"] = "candidate";
            msg["error"] = "id field missing";
            socket->send(fast.write(msg));
            return;
        }

        std::string otherId;
        if (!task->getOtherIdFromMessage(otherId)) {
            msg["type"] = "candidate";
            msg["error"] = "other_id field missing";
            socket->send(fast.write(msg));
            return;
        }

        if (otherId != _component_id.get()) {
            msg["type"] = "cadidate";
            msg["error"] = "wrong target id";
            socket->send(fast.write(msg));
            return;
        }

        std::string candidate;
        if (!task->getCandidateFromMessage(candidate)) {
            msg["type"] = "candidate";
            msg["error"] = "candidate field missing";
            socket->send(fast.write(msg));
            return;
        }

        new_pc->addRemoteCandidate(rtc::Candidate(candidate));
    }

    void createPeerConnection(WebSocket *socket, string id) {
        rtc::Configuration config;
        config.iceServers.emplace_back(_ice_server.get());

        new_pc = new rtc::PeerConnection(config);

        new_pc->onStateChange([&task, new_dc](PeerConnection::State state) {
            if (state == PeerConnection::State::Connected) {
                if (task->m_pc) {
                    delete task->m_pc;
                }
                if (task->m_dc) {
                    delete task->m_dc;
                }
                task->m_pc = this;
                task->m_dc = new_dc;

                handleNewConnection(pending, controlling);
                controlling = pending();
                pending = Client();
                new_dc = nullptr;
                last_offer_successful = true;
            }
        });

        // Send local description to the remote peer
        new_pc->onLocalDescription([socket, id](rtc::Description sdp) {
            Json::Value answer_msg;
            answer_msg["type"] = "answer";
            answer_msg["id"] = _component_id.get();
            answer_msg["other_id"] = id;
            answer_msg["sdp"] = string(sdp);
            socket.connection->send(fast.write(answer_msg));
        });

        // Send candidate to the remote peer
        new_pc->onLocalCandidate([socket, id](rtc::Candidate candidate) {
            Json::Value candidate_msg;
            candidate_msg["type"] = "candidate";
            candidate_msg["id"] = _component_id.get();
            candidate_msg["other_id"] = id;
            candidate_msg["candidate"] = candidate.candidate();
            socket.connection->send(fast.write(candidate_msg));
        });

        new_pc->onDataChannel([&new_dc](std::shared_ptr<rtc::DataChannel> incoming) {
            new_dc = incoming;
            new_dc->onOpen([]() {
                new_dc->send("Hello from " + _component_id.get() );
            });

            new_dc->onClosed([]() { cout << "DataChannel from " << _component_id.get() << " closed" << endl; });

            new_dc->onMessage([](variant<binary, string> data) {
                Json::Value msg;

                if (holds_alternative<string>(data)) {
                    bool result = task->parseIncomingMessage(get<string>(data));

                    result = result && task->handleControlMessage();
                    msg["result"] = result;
                    ++task->received;

                    // Increment errors count if the result is false, do nothing otherwise
                    task->errors += result ? 0 : 1;
                    statistic.received = task->received;
                    statistic.errors = task->errors;
                    statistic.time = base::Time::now();
                    task->_statistics.write(statistic);
                    new_dc->send(fast.write(msg));
                }
                else{
                    msg["error"] = "message must be of string type";
                    new_dc->send(fast.write(msg));
                }
            });
        });
    }
};

struct controldev_webrtc::MessageDecoder {
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    MessageDecoder() : reader(rbuilder.newCharReader()) {

    }

    static std::string mapFieldName(Mapping::Type type) {
        if (type == Mapping::Type::Button) {
            return "buttons";
        }
        if (type == Mapping::Type::Axis) {
            return "axes";
        }
        throw "Failed to get Field Name. The type set is invalid";
    }

    bool parseJSONMessage(char const* data, std::string &errors) {
        return reader->parse(data, data+std::strlen(data), &jdata, &errors);
    }

    bool validateControlMessage() {
        if (!jdata.isMember(mapFieldName(Mapping::Type::Axis)) ||
            !jdata.isMember(mapFieldName(Mapping::Type::Button)) ||
            !jdata.isMember("time")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateId() {
        if (!jdata.isMember("id")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the id field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateOtherId() {
        if (!jdata.isMember("other_id")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the other_id field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateOffer() {
        if (!jdata.isMember("sdp")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the sdp field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateAnswer() {
        if (!jdata.isMember("sdp")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the sdp field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateCandidate() {
        if (!jdata.isMember("candidate")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the candidate field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    double getValue(const Mapping &mapping) {
        auto const& field = jdata[mapFieldName(mapping.type)];
        if (!field.isValidIndex(mapping.index)) {
            throw std::out_of_range(
                "incoming raw command does not have a field " +
                std::to_string(mapping.index) + "in " + mapFieldName(mapping.type)
            );
        }
        return field[mapping.index].asDouble();
    }

    std::string getId( ) {
        auto const& field = jdata["id"];
        return field.asString();
    }

    std::string getOtherId( ) {
        auto const& field = jdata["other_id"];
        return field.asString();
    }

    std::string getOffer( ) {
        auto const& field = jdata["sdp"];
        return field.asString();
    }

    std::string getCandidate( ) {
        auto const& field = jdata["candidate"];
        return field.asString();
    }

    base::Time getTime() {
        return base::Time::fromMilliseconds(
            jdata["time"].asUInt64()
        );
    }

    bool validateType() {
        if (!jdata.isMember("type")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the type field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    std::string getType() {
        auto const& field = jdata["type"];
        return field.asString();
    }
};

bool Task::parseIncomingMessage(char const* data) {
    std::string errs;
    if (!decoder->parseJSONMessage(data, errs)) {
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }
    return true;
}

bool Task::handleControlMessage() {
    if (!decoder->validateControlMessage()) {
        return false;
    }

    if (!updateRawCommand()) {
        return false;
    }
    _raw_command.write(raw_cmd_obj);
    return true;
}

bool Task::getIdFromMessage(std::string &out_str) {
    if (!decoder->validateId()) {
        return false;
    }
    out_str = decoder->getId();
    return true;
}

bool Task::getTypeFromMessage(std::string &out_str) {
    if (!decoder->validateType()) {
        return false;
    }
    out_str = decoder->getType();
    return true;
}

bool Task::getOtherIdFromMessage(std::string &out_str) {
    if (!decoder->validateOtherId()) {
        return false;
    }
    out_str = decoder->getOtherId();
    return true;
}

bool Task::getOfferFromMessage(std::string &out_str) {
    if (!decoder->validateOffer()) {
        return false;
    }
    out_str = decoder->getOffer();
    return true;
}

bool Task::getCandidateFromMessage(std::string &out_str) {
    if (!decoder->validateCandidate()) {
        return false;
    }
    out_str = decoder->getCandidate();
    return true;
}

// Fill the Raw Command with the JSON data at decoder.
bool Task::updateRawCommand() {
    try {
        for (uint i = 0; i < axis.size(); ++i) {
            raw_cmd_obj.axisValue.at(i) = decoder->getValue(axis.at(i));
        }
        for (uint i = 0; i < button.size(); ++i) {
            raw_cmd_obj.buttonValue.at(i) =
                decoder->getValue(button.at(i)) > button.at(i).threshold;
        }
        raw_cmd_obj.time = decoder->getTime();
        return true;
    }
    // A failure here means that the client sent a bad message or the
    // mapping isn't good. Just inform the client: return false.
    catch (const std::exception &e) {
        LOG_ERROR_S << "Invalid message, got error: " << e.what() << std::endl;
        return false;
    }
}

Task::Task(std::string const& name, TaskCore::TaskState initial_state)
    : TaskBase(name, initial_state)
    , m_pc(0)
    , m_dc(0)
{
}

Task::~Task()
{
}

bool Task::configureHook()
{
    if (! TaskBase::configureHook()) {
        return false;
    }

    received = 0;
    errors = 0;

    button = _button_map.get();
    axis = _axis_map.get();

    raw_cmd_obj.buttonValue.resize(button.size(), 0);
    raw_cmd_obj.axisValue.resize(axis.size(), 0.0);

    decoder = new MessageDecoder();
    auto logger = std::make_shared<PrintfLogger>(Logger::Level::Debug);
    server = new Server (logger);

    auto handler = std::make_shared<JoystickHandler>();
    handler->task = this;
    server->addWebSocketHandler("/ws", handler, true);

    return true;
}
bool Task::startHook()
{
    if (! TaskBase::startHook()) {
        return false;
    }

    if (!server->startListening(_port.get())) {
        return false;
    }

    thread = new std::thread([this]{ this->server->loop(); });

    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
    server->terminate();
    thread->join();
    delete thread;
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
    delete server;
    delete decoder;
    delete m_pc;
}
