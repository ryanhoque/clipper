#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/functional/hash.hpp>
#include <clipper/constants.hpp>
#include <clipper/datatypes.hpp>
#include <clipper/logging.hpp>
#include <clipper/util.hpp>

namespace clipper {

std::string get_readable_input_type(DataType type) {
  switch (type) {
    case DataType::Bytes: return std::string("bytes");
    case DataType::Ints: return std::string("integers");
    case DataType::Floats: return std::string("floats");
    case DataType::Doubles: return std::string("doubles");
    case DataType::Strings: return std::string("strings");
    case DataType::Invalid:
    default: return std::string("Invalid input type");
  }
}

DataType parse_input_type(std::string type_string) {
  if (type_string == "bytes" || type_string == "byte" || type_string == "b") {
    return DataType::Bytes;
  } else if (type_string == "integers" || type_string == "ints" ||
             type_string == "integer" || type_string == "int" ||
             type_string == "i") {
    return DataType::Ints;
  } else if (type_string == "floats" || type_string == "float" ||
             type_string == "f") {
    return DataType::Floats;
  } else if (type_string == "doubles" || type_string == "double" ||
             type_string == "d") {
    return DataType::Doubles;
  } else if (type_string == "strings" || type_string == "string" ||
             type_string == "str" || type_string == "strs" ||
             type_string == "s") {
    return DataType::Strings;
  } else {
    throw std::invalid_argument(type_string + " is not a valid input string");
  }
}

VersionedModelId::VersionedModelId(const std::string name, const std::string id)
    : name_(name), id_(id) {}

std::string VersionedModelId::get_name() const { return name_; }

std::string VersionedModelId::get_id() const { return id_; }

std::string VersionedModelId::serialize() const {
  std::stringstream ss;
  ss << name_;
  ss << ITEM_PART_CONCATENATOR;
  ss << id_;
  return ss.str();
}

VersionedModelId VersionedModelId::deserialize(std::string str) {
  auto split = str.find(ITEM_PART_CONCATENATOR);
  std::string model_name = str.substr(0, split);
  std::string model_version = str.substr(split + 1, str.size());
  return VersionedModelId(model_name, model_version);
}

bool VersionedModelId::operator==(const VersionedModelId &rhs) const {
  return (name_ == rhs.name_ && id_ == rhs.id_);
}

bool VersionedModelId::operator!=(const VersionedModelId &rhs) const {
  return !(name_ == rhs.name_ && id_ == rhs.id_);
}

Output::Output(const std::string y_hat,
               const std::vector<VersionedModelId> models_used)
    : y_hat_(y_hat), models_used_(models_used) {}

bool Output::operator==(const Output &rhs) const {
  return (y_hat_ == rhs.y_hat_ && models_used_ == rhs.models_used_);
}

bool Output::operator!=(const Output &rhs) const {
  return !(y_hat_ == rhs.y_hat_ && models_used_ == rhs.models_used_);
}

ByteVector::ByteVector(UniquePoolPtr<uint8_t>& data, size_t size)
    : data_(std::move(data)), size_(size) {}

ByteVector::ByteVector(UniquePoolPtr<void>& data, size_t byte_size)
    : data_(UniquePoolPtr<uint8_t>(static_cast<uint8_t*>(data.release()))),
      size_(static_cast<size_t>(byte_size / sizeof(uint8_t))) {}

DataType ByteVector::type() const { return DataType::Bytes; }

size_t ByteVector::hash() const { return boost::hash_range(data_.get(), data_.get() + size_); }

size_t ByteVector::size() const { return size_; }

size_t ByteVector::byte_size() const { return size_; }

size_t ByteVector::serialize(std::vector<UniquePoolPtr<void>> &buf) {
  buf.push_back(std::move(data_));
  return size_;
}

const void* ByteVector::get_data() {
  return data_.release();
}

IntVector::IntVector(UniquePoolPtr<int>& data, size_t size)
    : data_(std::move(data)), size_(size) {}

IntVector::IntVector(UniquePoolPtr<void> &data, size_t byte_size)
    : data_(UniquePoolPtr<int>(static_cast<int*>(data.release()))),
      size_(static_cast<size_t>(byte_size / sizeof(int))) {}

DataType IntVector::type() const { return DataType::Ints; }

size_t IntVector::hash() const { return boost::hash_range(data_.get(), data_.get() + size_); }

size_t IntVector::size() const { return size_; }

size_t IntVector::byte_size() const { return size_ * sizeof(int); }

size_t IntVector::serialize(std::vector<UniquePoolPtr<void>> &buf) {
  buf.push_back(std::move(data_));
  return size_;
}

const void* IntVector::get_data() {
  return data_.release();
}

FloatVector::FloatVector(UniquePoolPtr<float>& data, size_t size)
    : data_(std::move(data)), size_(size) {}

FloatVector::FloatVector(UniquePoolPtr<void>& data, size_t byte_size)
    : data_(UniquePoolPtr<float>(static_cast<float*>(data.release()))),
      size_(static_cast<size_t>(byte_size / sizeof(float))) {}

DataType FloatVector::type() const { return DataType::Floats; }

size_t FloatVector::hash() const {
  // TODO [CLIPPER-63]: Find an alternative to hashing floats directly, as this
  // is generally a bad idea due to loss of precision from floating point)
  // representations
  return boost::hash_range(data_.get(), data_.get() + size_);
}

size_t FloatVector::size() const { return size_; }

size_t FloatVector::byte_size() const { return size_ * sizeof(float); }

size_t FloatVector::serialize(std::vector<UniquePoolPtr<void>> &buf) {
  buf.push_back(std::move(data_));
  return size_;
}

const void* FloatVector::get_data() {
  return data_.release();
}

DoubleVector::DoubleVector(UniquePoolPtr<double>& data, size_t size)
    : data_(std::move(data)), size_(size) {}

DoubleVector::DoubleVector(UniquePoolPtr<void>& data, size_t size_bytes)
    : data_(UniquePoolPtr<double>(static_cast<double*>(data.release()))),
      size_(static_cast<size_t>(size_bytes / sizeof(double))) {}

DataType DoubleVector::type() const { return DataType::Doubles; }

size_t DoubleVector::hash() const {
  // TODO [CLIPPER-63]: Find an alternative to hashing doubles directly, as
  // this is generally a bad idea due to loss of precision from floating point
  // representations
  return boost::hash_range(data_.get(), data_.get() + size_);
}

size_t DoubleVector::size() const { return size_; }

size_t DoubleVector::byte_size() const { return size_ * sizeof(double); }

size_t DoubleVector::serialize(std::vector<UniquePoolPtr<void>> &buf) {
  buf.push_back(std::move(data_));
  return size_;
}

const void* DoubleVector::get_data() {
  return data_.release();
}

SerializableString::SerializableString(UniquePoolPtr<char>& data, size_t size)
    : data_(std::move(data)), size_(size) {}

SerializableString::SerializableString(UniquePoolPtr<void>& data, size_t byte_size)
    : data_(UniquePoolPtr<char>(static_cast<char*>(data.release()))),
      size_(static_cast<size_t>(byte_size / sizeof(char))) {}

DataType SerializableString::type() const { return DataType::Strings; }

size_t SerializableString::hash() const {
  return boost::hash_range(data_.get(), data_.get() + size_);
}

size_t SerializableString::size() const { return 1; }

size_t SerializableString::byte_size() const {
  return size_ * sizeof(char);
}

const void* SerializableString::get_data() {
  return data_.release();
}

size_t SerializableString::serialize(std::vector<UniquePoolPtr<void>> &buf) {
  buf.push_back(std::move(data_));
  return size_;
}

rpc::PredictionRequest::PredictionRequest(DataType input_type)
    : input_type_(input_type) {}

rpc::PredictionRequest::PredictionRequest(
    std::vector<std::shared_ptr<PredictionData>> inputs, DataType input_type)
    : inputs_(inputs), input_type_(input_type) {
  for (int i = 0; i < (int)inputs.size(); i++) {
    validate_input_type(inputs[i]);
    input_data_size_ += inputs[i]->byte_size();
  }
}

void rpc::PredictionRequest::validate_input_type(
    std::shared_ptr<PredictionData> &input) const {
  if (input->type() != input_type_) {
    std::ostringstream ss;
    ss << "Attempted to add an input of type "
       << get_readable_input_type(input->type())
       << " to a prediction request with input type "
       << get_readable_input_type(input_type_);
    log_error(LOGGING_TAG_CLIPPER, ss.str());
    throw std::invalid_argument(ss.str());
  }
}

void rpc::PredictionRequest::add_input(std::shared_ptr<PredictionData> input) {
  validate_input_type(input);
  inputs_.push_back(input);
  input_data_size_ += input->byte_size();
}

std::vector<ByteBuffer> rpc::PredictionRequest::serialize() {
  if (input_data_size_ <= 0) {
    throw std::length_error(
        "Attempted to serialize a request with no input data!");
  }

  size_t request_metadata_size = 1 * sizeof(uint32_t);
  UniquePoolPtr<uint8_t> request_metadata(
      static_cast<uint8_t *>(malloc(request_metadata_size)), free);
  uint32_t *request_metadata_raw =
      reinterpret_cast<uint32_t *>(request_metadata.get());
  request_metadata_raw[0] = static_cast<uint32_t>(RequestType::PredictRequest);

  size_t input_metadata_size = (2 + (inputs_.size() - 1)) * sizeof(uint32_t);
  UniquePoolPtr<uint8_t> input_metadata(
      static_cast<uint8_t *>(malloc(input_metadata_size)), free);
  uint32_t *input_metadata_raw =
      reinterpret_cast<uint32_t *>(input_metadata.get());
  input_metadata_raw[0] = static_cast<uint32_t>(input_type_);
  input_metadata_raw[1] = static_cast<uint32_t>(inputs_.size());

  std::vector<UniquePoolPtr<void>> input_bufs;
  for (size_t i = 0; i < inputs_.size(); i++) {
    inputs_[i]->serialize(input_bufs);
    input_metadata_raw[i + 2] = static_cast<uint32_t>(inputs_[i]->byte_size());
  }

  size_t input_metadata_size_buf_size = 1 * sizeof(long);
  UniquePoolPtr<uint8_t> input_metadata_size_buf(
      static_cast<uint8_t *>(malloc(input_metadata_size_buf_size)), free);
  long *input_metadata_size_buf_raw =
      reinterpret_cast<long *>(input_metadata_size_buf.get());
  // Add the size of the input metadata in bytes. This will be
  // sent prior to the input metadata to allow for proactive
  // buffer allocation in the receiving container
  input_metadata_size_buf_raw[0] = input_metadata_size;

  std::vector<ByteBuffer> serialized_request;
  serialized_request.emplace_back(
      std::make_pair(request_metadata, request_metadata_size));
  serialized_request.emplace_back(
      std::make_pair(input_metadata_size_buf, input_metadata_size_buf_size));
  serialized_request.emplace_back(
      std::make_pair(input_metadata, input_metadata_size));
  for (size_t i = 0; i < input_bufs.size(); i++) {
    serialized_request.emplace_back(std::make_pair(input_bufs[i], input_metadata_raw[i + 2]));
  }
  return serialized_request;
}

rpc::PredictionResponse::PredictionResponse(
    const std::vector<std::string> outputs)
    : outputs_(outputs) {}

rpc::PredictionResponse
rpc::PredictionResponse::deserialize_prediction_response(ByteBuffer response) {
  std::vector<std::shared_ptr<char>> outputs;
  uint32_t *output_lengths_data = reinterpret_cast<uint32_t *>(response.first);
  uint32_t num_outputs = output_lengths_data[0];
  output_lengths_data++;
  char *output_string_data = reinterpret_cast<char *>(
      response.first + sizeof(uint32_t) + (num_outputs * sizeof(uint32_t)));
  for (uint32_t i = 0; i < num_outputs; i++) {
    uint32_t output_length = output_lengths_data[i];
    std::string output(output_string_data, output_length);
    outputs.push_back(std::move(output));
    output_string_data += output_length;
  }
  return PredictionResponse(outputs);
}

Query::Query(std::string label, long user_id, std::shared_ptr<PredictionData> input,
             long latency_budget_micros, std::string selection_policy,
             std::vector<VersionedModelId> candidate_models)
    : label_(std::move(label)),
      user_id_(user_id),
      input_(std::move(input)),
      latency_budget_micros_(latency_budget_micros),
      selection_policy_(std::move(selection_policy)),
      candidate_models_(std::move(candidate_models)),
      create_time_(std::chrono::high_resolution_clock::now()) {}

Response::Response(Query query, QueryId query_id, const long duration_micros,
                   Output output, const bool output_is_default,
                   const boost::optional<std::string> default_explanation)
    : query_(std::move(query)),
      query_id_(query_id),
      duration_micros_(duration_micros),
      output_(std::move(output)),
      output_is_default_(output_is_default),
      default_explanation_(default_explanation) {}

std::string Response::debug_string() const noexcept {
  std::string debug;
  debug.append("Query id: ");
  debug.append(std::to_string(query_id_));
  debug.append(" Output: ");
  debug.append(output_.y_hat_);
  return debug;
}

Feedback::Feedback(std::shared_ptr<PredictionData> input, double y)
    : y_(y), input_(std::move(input)) {}

FeedbackQuery::FeedbackQuery(std::string label, long user_id, Feedback feedback,
                             std::string selection_policy,
                             std::vector<VersionedModelId> candidate_models)
    : label_(std::move(label)),
      user_id_(user_id),
      feedback_(std::move(feedback)),
      selection_policy_(std::move(selection_policy)),
      candidate_models_(std::move(candidate_models)) {}

PredictTask::PredictTask(std::shared_ptr<PredictionData> input, VersionedModelId model,
                         float utility, QueryId query_id,
                         long latency_slo_micros)
    : input_(std::move(input)),
      model_(std::move(model)),
      utility_(utility),
      query_id_(query_id),
      latency_slo_micros_(latency_slo_micros) {}

FeedbackTask::FeedbackTask(Feedback feedback, VersionedModelId model,
                           QueryId query_id, long latency_slo_micros)
    : feedback_(feedback),
      model_(model),
      query_id_(query_id),
      latency_slo_micros_(latency_slo_micros) {}

}  // namespace clipper
