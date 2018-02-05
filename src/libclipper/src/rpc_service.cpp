#include <boost/bimap.hpp>
#include <boost/functional/hash.hpp>

#include <chrono>
#include <iostream>

#include <redox.hpp>

#include <clipper/config.hpp>
#include <clipper/datatypes.hpp>
#include <clipper/logging.hpp>
#include <clipper/metrics.hpp>
#include <clipper/redis.hpp>
#include <clipper/rpc_service.hpp>
#include <clipper/task_executor.hpp>
#include <clipper/threadpool.hpp>
#include <clipper/util.hpp>

using zmq::socket_t;
using zmq::message_t;
using zmq::context_t;
using std::shared_ptr;
using std::string;
using std::vector;

namespace clipper {

namespace rpc {

constexpr int INITIAL_REPLICA_ID_SIZE = 100;

void RPCDataStore::add_data(SharedPoolPtr<void> data) {
  std::lock_guard<std::mutex> lock(mtx_);
  void* data_ptr = data.get();
  data_items_.emplace(data_ptr, std::move(data));
}

void RPCDataStore::remove_data(void *data) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto search = data_items_.find(data);
  data_items_.erase(data);
}

RPCService::RPCService()
    : request_queue_(std::make_shared<Queue<RPCRequest>>()),
      active_(false),
      // The version of the unordered_map constructor that allows
      // you to specify your own hash function also requires you
      // to provide the initial size of the map. We define the initial
      // size of the map somewhat arbitrarily as 100.
      replica_ids_(std::unordered_map<VersionedModelId, int>({})) {
  msg_queueing_hist_ = metrics::MetricsRegistry::get_metrics().create_histogram(
      "internal:rpc_request_queueing_delay", "microseconds", 2056);
}

RPCService::~RPCService() { stop(); }

void RPCService::start(
    const string ip, const int port,
    std::function<void(VersionedModelId, int)> &&container_ready_callback,
    std::function<void(RPCResponse&)> &&new_response_callback) {
  container_ready_callback_ = container_ready_callback;
  new_response_callback_ = new_response_callback;
  if (active_) {
    throw std::runtime_error(
        "Attempted to start RPC Service when it is already running!");
  }
  const string address = "tcp://" + ip + ":" + std::to_string(port);
  active_ = true;
  // TODO: Propagate errors from new child thread for handling
  // TODO: Explore bind vs static method call for thread creation
  rpc_thread_ = std::thread([this, address]() { manage_service(address); });
}

void RPCService::stop() {
  if (active_) {
    active_ = false;
    rpc_thread_.join();
  }
}

int RPCService::send_message(vector<ByteBuffer> msg,
                             const uint32_t zmq_connection_id) {
  if (!active_) {
    log_error(LOGGING_TAG_RPC,
              "Cannot send message to inactive RPCService instance",
              "Dropping Message");
    return -1;
  }
  int id = message_id_;
  message_id_ += 1;
  long current_time_micros =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  RPCRequest request(zmq_connection_id, id, std::move(msg), current_time_micros);
  request_queue_->push(std::move(request));
  return id;
}

void RPCService::manage_service(const string address) {
  // Map from container id to unique routing id for zeromq
  // Note that zeromq socket id is a byte vector
  log_info_formatted(LOGGING_TAG_RPC, "RPC thread started at address: ",
                     address);
  boost::bimap<int, vector<uint8_t>> connections;
  // Initializes a map to associate the ZMQ connection IDs
  // of connected containers with their metadata, including
  // model id and replica id

  std::unordered_map<std::vector<uint8_t>, std::pair<VersionedModelId, int>,
                     std::function<size_t(const std::vector<uint8_t> &vec)>>
      connections_containers_map(INITIAL_REPLICA_ID_SIZE, hash_vector<uint8_t>);
  context_t context = context_t(1);
  socket_t socket = socket_t(context, ZMQ_ROUTER);
  socket.bind(address);
  // Indicate that we will poll our zmq service socket for new inbound messages
  zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
  uint32_t zmq_connection_id = 0;
  auto redis_connection = std::make_shared<redox::Redox>();
  Config &conf = get_config();
  while (!redis_connection->connect(conf.get_redis_address(),
                                    conf.get_redis_port())) {
    log_error(LOGGING_TAG_RPC, "RPCService failed to connect to Redis",
              "Retrying in 1 second...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  while (active_) {
    // Set poll timeout based on whether there are outgoing messages to
    // send. If there are messages to send, don't let the poll block at all.
    // If there no messages to send, let the poll block for 1 ms.
    int poll_timeout = 0;
    if (request_queue_->size() == 0) {
      poll_timeout = 1;
    }
    zmq_poll(items, 1, poll_timeout);
    if (items[0].revents & ZMQ_POLLIN) {
      // TODO: Balance message sending and receiving fairly
      // Note: We only receive one message per event loop iteration
      log_info(LOGGING_TAG_RPC, "Found message to receive");

      receive_message(socket, connections, connections_containers_map,
                      zmq_connection_id, redis_connection);
    }
    // Note: We send all queued messages per event loop iteration
    send_messages(socket, connections);
  }
  shutdown_service(socket);
}

void RPCService::shutdown_service(socket_t &socket) {
  size_t buf_size = 32;
  std::vector<char> buf(buf_size);
  socket.getsockopt(ZMQ_LAST_ENDPOINT, (void *)buf.data(), &buf_size);
  std::string last_endpoint = std::string(buf.begin(), buf.end());
  socket.unbind(last_endpoint);
  socket.close();
}

void RPCService::send_messages(
    socket_t &socket, boost::bimap<int, vector<uint8_t>> &connections) {
  while (request_queue_->size() > 0) {
    long current_time_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    RPCRequest request = request_queue_->pop();
    msg_queueing_hist_->insert(current_time_micros - std::get<3>(request));
    boost::bimap<int, vector<uint8_t>>::left_const_iterator connection =
        connections.left.find(std::get<0>(request));
    if (connection == connections.left.end()) {
      // Error handling
      log_error_formatted(LOGGING_TAG_CLIPPER,
                          "Attempted to send message to unknown container: ",
                          std::get<0>(request));
      continue;
    }

    message_t type_message(sizeof(int));
    static_cast<int *>(type_message.data())[0] =
        static_cast<int>(MessageType::ContainerContent);
    message_t id_message(sizeof(int));
    static_cast<int *>(id_message.data())[0] = std::get<1>(request);
    vector<uint8_t> routing_identity = connection->second;

    socket.send(routing_identity.data(), routing_identity.size(), ZMQ_SNDMORE);
    socket.send("", 0, ZMQ_SNDMORE);
    socket.send(type_message, ZMQ_SNDMORE);
    socket.send(id_message, ZMQ_SNDMORE);
    size_t cur_msg_num = 0;
    // subtract 1 because we start counting at 0
    size_t last_msg_num = std::get<2>(request).size() - 1;
    for (ByteBuffer &m : std::get<2>(request)) {
      SharedPoolPtr<void> data_ptr = std::get<0>(m);
      outbound_data_store_.add_data(data_ptr);

      size_t data_start = std::get<1>(m);
      size_t data_size = std::get<2>(m);

      message_t msg(static_cast<uint8_t*>(data_ptr.get()) + data_start,
                    data_size,
                    &RPCService::zmq_continuation,
                    &outbound_data_store_);

      if (cur_msg_num < last_msg_num) {
        // send the sndmore flag unless we are on the last message part
        socket.send(msg, ZMQ_SNDMORE);
      } else {
        socket.send(msg);
      }
      cur_msg_num += 1;
    }
  }
}

void RPCService::zmq_continuation(void *data, void* hint) {
  auto data_store = static_cast<RPCDataStore*>(hint);
  data_store->remove_data(data);
}

void RPCService::receive_message(
    socket_t &socket, boost::bimap<int, vector<uint8_t>> &connections,
    std::unordered_map<std::vector<uint8_t>, std::pair<VersionedModelId, int>,
                       std::function<size_t(const std::vector<uint8_t> &vec)>>
        &connections_containers_map,
    uint32_t &zmq_connection_id, std::shared_ptr<redox::Redox> redis_connection) {
  message_t msg_routing_identity;
  message_t msg_delimiter;
  message_t msg_type;
  socket.recv(&msg_routing_identity, 0);
  socket.recv(&msg_delimiter, 0);
  socket.recv(&msg_type, 0);

  const vector<uint8_t> connection_id(
      (uint8_t *)msg_routing_identity.data(),
      (uint8_t *)msg_routing_identity.data() + msg_routing_identity.size());

  MessageType type =
      static_cast<MessageType>(static_cast<int *>(msg_type.data())[0]);

  boost::bimap<int, vector<uint8_t>>::right_const_iterator connection =
      connections.right.find(connection_id);
  bool new_connection = (connection == connections.right.end());
  switch (type) {
    case MessageType::NewContainer: {
      message_t model_name;
      message_t model_version;
      message_t model_input_type;
      socket.recv(&model_name, 0);
      socket.recv(&model_version, 0);
      socket.recv(&model_input_type, 0);
      if (new_connection) {
        // We have a new connection with container metadata, process it
        // accordingly
        connections.insert(boost::bimap<int, vector<uint8_t>>::value_type(
            zmq_connection_id, connection_id));
        log_info(LOGGING_TAG_RPC, "New container connected");
        std::string name(static_cast<char *>(model_name.data()),
                         model_name.size());
        std::string version(static_cast<char *>(model_version.data()),
                            model_version.size());
        std::string input_type_str(static_cast<char *>(model_input_type.data()),
                                   model_input_type.size());

        InputType input_type =
            static_cast<InputType>(std::stoi(input_type_str));

        VersionedModelId model = VersionedModelId(name, version);
        log_info(LOGGING_TAG_RPC, "Container added");

        // Note that if the map does not have an entry for this model,
        // a new entry will be created with the default value (0).
        // This use of operator[] avoids the boilerplate of having to
        // check if the key is present in the map.
        int cur_replica_id = replica_ids_[model];
        replica_ids_[model] = cur_replica_id + 1;
        redis::add_container(*redis_connection, model, cur_replica_id,
                             zmq_connection_id, input_type);
        connections_containers_map.emplace(
            connection_id,
            std::pair<VersionedModelId, int>(model, cur_replica_id));

        TaskExecutionThreadPool::create_queue(model, cur_replica_id);
        zmq_connection_id += 1;
      }
    } break;
    case MessageType::ContainerContent: {
      // This message is a response to a container query
      message_t msg_id;
      message_t msg_output_header_size;
      message_t msg_output_header;
      socket.recv(&msg_id, 0);
      socket.recv(&msg_output_header_size, 0);
      socket.recv(&msg_output_header, 0);

      uint64_t* output_header = static_cast<uint64_t*>(msg_output_header.data());
      uint64_t num_outputs = output_header[0];
      output_header++;
      uint64_t output_data_size =
          static_cast<uint64_t>(std::accumulate(output_header, output_header + num_outputs, 0));
      SharedPoolPtr<void> output_data(malloc(output_data_size), free);
      uint8_t *output_data_raw = static_cast<uint8_t*>(output_data.get());
      vector<ByteBuffer> content;
      content.reserve(num_outputs);
      size_t curr_start = 0;
      for (uint64_t i = 0; i < num_outputs; ++i) {
        uint64_t& output_size = output_header[i];
        socket.recv(output_data_raw, output_size, 0);
        content.emplace_back(std::make_tuple(output_data, curr_start, output_size));
        output_data_raw += output_size;
        curr_start += output_size;
      }

      if(!new_connection) {
        uint32_t id = static_cast<uint32_t*>(msg_id.data())[0];
        RPCResponse response(id, std::move(content));

        auto container_info_entry =
            connections_containers_map.find(connection_id);
        if (container_info_entry == connections_containers_map.end()) {
          throw std::runtime_error(
              "Failed to find container that was previously registered via "
                  "RPC");
        }
        std::pair<VersionedModelId, int> container_info =
            container_info_entry->second;

        VersionedModelId vm = container_info.first;
        int replica_id = container_info.second;
        TaskExecutionThreadPool::submit_job(vm, replica_id,
                                            new_response_callback_, std::move(response));
        TaskExecutionThreadPool::submit_job(
            vm, replica_id, container_ready_callback_, vm, replica_id);
      }
    } break;
    case MessageType::Heartbeat:
      send_heartbeat_response(socket, connection_id, new_connection);
      break;
  }
}

void RPCService::send_heartbeat_response(socket_t &socket,
                                         const vector<uint8_t> &connection_id,
                                         bool request_container_metadata) {
  message_t type_message(sizeof(int));
  message_t heartbeat_type_message(sizeof(int));
  static_cast<int *>(type_message.data())[0] =
      static_cast<int>(MessageType::Heartbeat);
  static_cast<int *>(heartbeat_type_message.data())[0] = static_cast<int>(
      request_container_metadata ? HeartbeatType::RequestContainerMetadata
                                 : HeartbeatType::KeepAlive);
  socket.send(connection_id.data(), connection_id.size(), ZMQ_SNDMORE);
  socket.send("", 0, ZMQ_SNDMORE);
  socket.send(type_message, ZMQ_SNDMORE);
  socket.send(heartbeat_type_message);
}

}  // namespace rpc

}  // namespace clipper
