#include "fbvector/udr_entry.h"
#include "fbvector/distance.h"
#include "fbvector/binary_layout.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <optional>
#include <span>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace Firebird;
using fbvector::AutoRelease;

namespace fbvector {

int send_sidecar_sync(int id, int action, std::span<const float> vec) {
#ifdef _WIN32
    return -5; // not supported on Windows
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms timeout
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5005);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -2;
    }

    uint32_t magic = 0xF1B10001;
    uint8_t act_val = static_cast<uint8_t>(action);
    uint32_t id_val = static_cast<uint32_t>(id);

    // Simple robust POSIX write calls
    if (write(sock, &magic, sizeof(magic)) <= 0 ||
        write(sock, &act_val, sizeof(act_val)) <= 0 ||
        write(sock, &id_val, sizeof(id_val)) <= 0) {
        close(sock);
        return -6;
    }

    if (action == 1) {
        uint32_t dim = vec.size();
        if (write(sock, &dim, sizeof(dim)) <= 0 ||
            write(sock, vec.data(), dim * sizeof(float)) <= 0) {
            close(sock);
            return -6;
        }
    }

    close(sock);
    return 0;
#endif
}

void raise_error(ThrowStatusWrapper* status, const char* message) {
    ISC_STATUS_ARRAY statusVector = {
        isc_arg_gds, isc_random,
        isc_arg_string, reinterpret_cast<intptr_t>(message),
        isc_arg_end
    };
    status->setErrors(statusVector);
    ThrowStatusWrapper::checkException(status);
}

std::vector<uint8_t> read_blob_bytes(ThrowStatusWrapper* status, IAttachment* attachment, ITransaction* transaction, const ISC_QUAD* blob_id) {
    std::vector<uint8_t> bytes;
    if (!blob_id) {
        return bytes;
    }
    
    ISC_QUAD temp_id = *blob_id;
    AutoRelease<IBlob> blob(attachment->openBlob(status, transaction, &temp_id, 0, nullptr));
    uint8_t buffer[4096];
    unsigned int bytes_read = 0;
    bool done = false;
    
    while (!done) {
        int result = blob->getSegment(status, sizeof(buffer), buffer, &bytes_read);
        if (bytes_read > 0) {
            bytes.insert(bytes.end(), buffer, buffer + bytes_read);
        }
        if (result == IStatus::RESULT_OK) {
            // Done reading
            done = true;
        } else if (result == IStatus::RESULT_SEGMENT) {
            // Segment truncated, more data remains in current segment
        } else {
            // EOF or error
            done = true;
        }
    }
    blob->close(status);
    return bytes;
}

void write_blob_bytes(ThrowStatusWrapper* status, IAttachment* attachment, ITransaction* transaction, ISC_QUAD* blob_id, std::span<const uint8_t> data) {
    AutoRelease<IBlob> blob(attachment->createBlob(status, transaction, blob_id, 0, nullptr));
    size_t offset = 0;
    while (offset < data.size()) {
        size_t chunk_size = std::min(data.size() - offset, size_t(65535));
        blob->putSegment(status, chunk_size, data.data() + offset);
        offset += chunk_size;
    }
    blob->close(status);
}

} // namespace fbvector

namespace {

struct VectorHolder {
    std::span<const float> span;
    std::vector<float> storage;

    VectorHolder() = default;
    VectorHolder(std::span<const float> s) : span(s) {}
    VectorHolder(std::vector<float>&& v) : storage(std::move(v)) {
        span = std::span<const float>(storage.data(), storage.size());
    }
};

VectorHolder extract_vector_cached(ThrowStatusWrapper* status, IExternalContext* context, const char* in_buf,
                                   unsigned nullOffset, unsigned offset, unsigned type) {
    if (*(ISC_SHORT*)(in_buf + nullOffset)) {
        fbvector::raise_error(status, "Vector parameter cannot be NULL");
        return {};
    }

    if (type == SQL_VARYING || type == SQL_TEXT) {
        unsigned short len = *(unsigned short*)(in_buf + offset);
        const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(in_buf + offset + 2);
        
        std::span<const uint8_t> bytes(raw_data, len);
        auto opt_span = fbvector::deserialize_vector(bytes);
        if (!opt_span) {
            fbvector::raise_error(status, "Invalid vector binary format in VARCHAR");
            return {};
        }
        return VectorHolder(*opt_span);
    }
    else if (type == SQL_BLOB) {
        const ISC_QUAD* blob_id = reinterpret_cast<const ISC_QUAD*>(in_buf + offset);
        
        AutoRelease<IAttachment> attachment(context->getAttachment(status));
        AutoRelease<ITransaction> transaction(context->getTransaction(status));
        
        std::vector<uint8_t> bytes = fbvector::read_blob_bytes(status, attachment, transaction, blob_id);
        auto opt_span = fbvector::deserialize_vector(bytes);
        if (!opt_span) {
            fbvector::raise_error(status, "Invalid vector binary format in BLOB");
            return {};
        }
        
        std::vector<float> owned(opt_span->begin(), opt_span->end());
        return VectorHolder(std::move(owned));
    }
    else {
        fbvector::raise_error(status, "Unsupported parameter type. Expected VARCHAR or BLOB");
        return {};
    }
}

std::optional<std::vector<float>> parse_vector_string(const std::string& str) {
    std::vector<float> vec;
    size_t start = str.find('[');
    size_t end = str.rfind(']');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return std::nullopt;
    }
    
    std::string content = str.substr(start + 1, end - start - 1);
    size_t pos = 0;
    while (pos < content.size()) {
        size_t next_comma = content.find(',', pos);
        std::string token = (next_comma == std::string::npos) ? content.substr(pos) : content.substr(pos, next_comma - pos);
        
        size_t first = token.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            size_t last = token.find_last_not_of(" \t\r\n");
            std::string clean_token = token.substr(first, last - first + 1);
            if (!clean_token.empty()) {
                try {
                    size_t processed = 0;
                    float val = std::stof(clean_token, &processed);
                    if (processed < clean_token.size()) {
                        return std::nullopt;
                    }
                    vec.push_back(val);
                } catch (...) {
                    return std::nullopt;
                }
            }
        } else {
            if (next_comma != std::string::npos || pos > 0) {
                return std::nullopt;
            }
        }
        
        if (next_comma == std::string::npos) break;
        pos = next_comma + 1;
    }
    return vec;
}

std::string vector_to_string(std::span<const float> vec) {
    std::string s = "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        s += std::to_string(vec[i]);
        if (i + 1 < vec.size()) {
            s += ",";
        }
    }
    s += "]";
    return s;
}

} // namespace

//------------------------------------------------------------------------------
// UDR Functions Implementation
//------------------------------------------------------------------------------

// 1. vector_l2_distance
FB_UDR_BEGIN_FUNCTION(vector_l2_distance)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffsets[0] = inMetadata->getNullOffset(status, 0);
        inOffsets[0] = inMetadata->getOffset(status, 0);
        inTypes[0] = inMetadata->getType(status, 0);

        inNullOffsets[1] = inMetadata->getNullOffset(status, 1);
        inOffsets[1] = inMetadata->getOffset(status, 1);
        inTypes[1] = inMetadata->getType(status, 1);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v1 = extract_vector_cached(status, context, in_buf, inNullOffsets[0], inOffsets[0], inTypes[0]);
        if (v1.span.empty()) return;
        auto v2 = extract_vector_cached(status, context, in_buf, inNullOffsets[1], inOffsets[1], inTypes[1]);
        if (v2.span.empty()) return;

        auto dist = fbvector::l2_distance(v1.span, v2.span);
        if (!dist) {
            fbvector::raise_error(status, "L2 distance failed: dimension mismatch");
            return;
        }

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_DOUBLE) {
            *(double*)(out_buf + outOffset) = *dist;
        } else if (outType == SQL_FLOAT) {
            *(float*)(out_buf + outOffset) = *dist;
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected FLOAT or DOUBLE PRECISION");
        }
    }

    unsigned inNullOffsets[2];
    unsigned inOffsets[2];
    unsigned inTypes[2];
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 2. vector_cosine_distance
FB_UDR_BEGIN_FUNCTION(vector_cosine_distance)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffsets[0] = inMetadata->getNullOffset(status, 0);
        inOffsets[0] = inMetadata->getOffset(status, 0);
        inTypes[0] = inMetadata->getType(status, 0);

        inNullOffsets[1] = inMetadata->getNullOffset(status, 1);
        inOffsets[1] = inMetadata->getOffset(status, 1);
        inTypes[1] = inMetadata->getType(status, 1);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v1 = extract_vector_cached(status, context, in_buf, inNullOffsets[0], inOffsets[0], inTypes[0]);
        if (v1.span.empty()) return;
        auto v2 = extract_vector_cached(status, context, in_buf, inNullOffsets[1], inOffsets[1], inTypes[1]);
        if (v2.span.empty()) return;

        auto dist = fbvector::cosine_distance(v1.span, v2.span);
        if (!dist) {
            fbvector::raise_error(status, "Cosine distance failed: dimension mismatch or zero vector magnitude");
            return;
        }

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_DOUBLE) {
            *(double*)(out_buf + outOffset) = *dist;
        } else if (outType == SQL_FLOAT) {
            *(float*)(out_buf + outOffset) = *dist;
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected FLOAT or DOUBLE PRECISION");
        }
    }

    unsigned inNullOffsets[2];
    unsigned inOffsets[2];
    unsigned inTypes[2];
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 3. vector_inner_product
FB_UDR_BEGIN_FUNCTION(vector_inner_product)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffsets[0] = inMetadata->getNullOffset(status, 0);
        inOffsets[0] = inMetadata->getOffset(status, 0);
        inTypes[0] = inMetadata->getType(status, 0);

        inNullOffsets[1] = inMetadata->getNullOffset(status, 1);
        inOffsets[1] = inMetadata->getOffset(status, 1);
        inTypes[1] = inMetadata->getType(status, 1);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v1 = extract_vector_cached(status, context, in_buf, inNullOffsets[0], inOffsets[0], inTypes[0]);
        if (v1.span.empty()) return;
        auto v2 = extract_vector_cached(status, context, in_buf, inNullOffsets[1], inOffsets[1], inTypes[1]);
        if (v2.span.empty()) return;

        auto dist = fbvector::dot_product(v1.span, v2.span);
        if (!dist) {
            fbvector::raise_error(status, "Vector inner product failed: dimension mismatch");
            return;
        }

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_DOUBLE) {
            *(double*)(out_buf + outOffset) = *dist;
        } else if (outType == SQL_FLOAT) {
            *(float*)(out_buf + outOffset) = *dist;
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected FLOAT or DOUBLE PRECISION");
        }
    }

    unsigned inNullOffsets[2];
    unsigned inOffsets[2];
    unsigned inTypes[2];
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 7. vector_l1_distance
FB_UDR_BEGIN_FUNCTION(vector_l1_distance)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffsets[0] = inMetadata->getNullOffset(status, 0);
        inOffsets[0] = inMetadata->getOffset(status, 0);
        inTypes[0] = inMetadata->getType(status, 0);

        inNullOffsets[1] = inMetadata->getNullOffset(status, 1);
        inOffsets[1] = inMetadata->getOffset(status, 1);
        inTypes[1] = inMetadata->getType(status, 1);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v1 = extract_vector_cached(status, context, in_buf, inNullOffsets[0], inOffsets[0], inTypes[0]);
        if (v1.span.empty()) return;
        auto v2 = extract_vector_cached(status, context, in_buf, inNullOffsets[1], inOffsets[1], inTypes[1]);
        if (v2.span.empty()) return;

        auto dist = fbvector::l1_distance(v1.span, v2.span);
        if (!dist) {
            fbvector::raise_error(status, "L1 distance failed: dimension mismatch");
            return;
        }

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_DOUBLE) {
            *(double*)(out_buf + outOffset) = *dist;
        } else if (outType == SQL_FLOAT) {
            *(float*)(out_buf + outOffset) = *dist;
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected FLOAT or DOUBLE PRECISION");
        }
    }

    unsigned inNullOffsets[2];
    unsigned inOffsets[2];
    unsigned inTypes[2];
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 4. vector_dims
FB_UDR_BEGIN_FUNCTION(vector_dims)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffset = inMetadata->getNullOffset(status, 0);
        inOffset = inMetadata->getOffset(status, 0);
        inType = inMetadata->getType(status, 0);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v = extract_vector_cached(status, context, in_buf, inNullOffset, inOffset, inType);
        if (v.span.empty()) return;

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_LONG) {
            *(ISC_LONG*)(out_buf + outOffset) = static_cast<ISC_LONG>(v.span.size());
        } else if (outType == SQL_SHORT) {
            *(ISC_SHORT*)(out_buf + outOffset) = static_cast<ISC_SHORT>(v.span.size());
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected INTEGER");
        }
    }

    unsigned inNullOffset;
    unsigned inOffset;
    unsigned inType;
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 8. vector_norm
FB_UDR_BEGIN_FUNCTION(vector_norm)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffset = inMetadata->getNullOffset(status, 0);
        inOffset = inMetadata->getOffset(status, 0);
        inType = inMetadata->getType(status, 0);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v = extract_vector_cached(status, context, in_buf, inNullOffset, inOffset, inType);
        if (v.span.empty()) return;

        auto norm = fbvector::vector_norm(v.span);
        if (!norm) {
            fbvector::raise_error(status, "Vector norm failed: empty vector");
            return;
        }

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_DOUBLE) {
            *(double*)(out_buf + outOffset) = *norm;
        } else if (outType == SQL_FLOAT) {
            *(float*)(out_buf + outOffset) = *norm;
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected FLOAT or DOUBLE PRECISION");
        }
    }

    unsigned inNullOffset;
    unsigned inOffset;
    unsigned inType;
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 5. vector_from_text
FB_UDR_BEGIN_FUNCTION(vector_from_text)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffset = inMetadata->getNullOffset(status, 0);
        inOffset = inMetadata->getOffset(status, 0);
        inType = inMetadata->getType(status, 0);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        if (*(ISC_SHORT*)(in_buf + inNullOffset)) {
            fbvector::raise_error(status, "Input text cannot be NULL");
            return;
        }

        std::string text_str;
        if (inType == SQL_VARYING || inType == SQL_TEXT) {
            unsigned short len = *(unsigned short*)(in_buf + inOffset);
            text_str.assign(in_buf + inOffset + 2, len);
        } else if (inType == SQL_BLOB) {
            const ISC_QUAD* blob_id = reinterpret_cast<const ISC_QUAD*>(in_buf + inOffset);
            AutoRelease<IAttachment> attachment(context->getAttachment(status));
            AutoRelease<ITransaction> transaction(context->getTransaction(status));
            std::vector<uint8_t> bytes = fbvector::read_blob_bytes(status, attachment, transaction, blob_id);
            text_str.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else {
            fbvector::raise_error(status, "Unsupported input type. Expected VARCHAR or BLOB text representation");
            return;
        }

        auto parsed_opt = parse_vector_string(text_str);
        if (!parsed_opt) {
            fbvector::raise_error(status, "Failed to parse vector text representation");
            return;
        }
        std::vector<float> parsed = std::move(*parsed_opt);

        std::vector<uint8_t> serialized = fbvector::serialize_vector(parsed);

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_VARYING || outType == SQL_TEXT) {
            AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
            unsigned max_len = outMetadata->getLength(status, 0);
            if (serialized.size() + 2 > max_len) {
                fbvector::raise_error(status, "Serialized vector size exceeds the output VARCHAR capacity");
                return;
            }
            *(unsigned short*)(out_buf + outOffset) = static_cast<unsigned short>(serialized.size());
            std::memcpy(out_buf + outOffset + 2, serialized.data(), serialized.size());
        } else if (outType == SQL_BLOB) {
            ISC_QUAD* out_blob_id = reinterpret_cast<ISC_QUAD*>(out_buf + outOffset);
            AutoRelease<IAttachment> attachment(context->getAttachment(status));
            AutoRelease<ITransaction> transaction(context->getTransaction(status));
            fbvector::write_blob_bytes(status, attachment, transaction, out_blob_id, serialized);
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected VARCHAR or BLOB");
        }
    }

    unsigned inNullOffset;
    unsigned inOffset;
    unsigned inType;
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 6. vector_to_text
FB_UDR_BEGIN_FUNCTION(vector_to_text)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffset = inMetadata->getNullOffset(status, 0);
        inOffset = inMetadata->getOffset(status, 0);
        inType = inMetadata->getType(status, 0);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        auto v = extract_vector_cached(status, context, in_buf, inNullOffset, inOffset, inType);
        if (v.span.empty()) return;

        std::string text = vector_to_string(v.span);

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        if (outType == SQL_VARYING || outType == SQL_TEXT) {
            AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
            unsigned max_len = outMetadata->getLength(status, 0);
            if (text.size() + 2 > max_len) {
                fbvector::raise_error(status, "Vector text representation exceeds the output VARCHAR capacity");
                return;
            }
            *(unsigned short*)(out_buf + outOffset) = static_cast<unsigned short>(text.size());
            std::memcpy(out_buf + outOffset + 2, text.data(), text.size());
        } else if (outType == SQL_BLOB) {
            ISC_QUAD* out_blob_id = reinterpret_cast<ISC_QUAD*>(out_buf + outOffset);
            AutoRelease<IAttachment> attachment(context->getAttachment(status));
            AutoRelease<ITransaction> transaction(context->getTransaction(status));
            std::span<const uint8_t> text_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
            fbvector::write_blob_bytes(status, attachment, transaction, out_blob_id, text_bytes);
        } else {
            fbvector::raise_error(status, "Unsupported return type. Expected VARCHAR or BLOB");
        }
    }

    unsigned inNullOffset;
    unsigned inOffset;
    unsigned inType;
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// 9. vector_sidecar_sync
FB_UDR_BEGIN_FUNCTION(vector_sidecar_sync)
    FB_UDR_CONSTRUCTOR
    {
        AutoRelease<IMessageMetadata> inMetadata(metadata->getInputMetadata(status));
        inNullOffsets[0] = inMetadata->getNullOffset(status, 0);
        inOffsets[0] = inMetadata->getOffset(status, 0);
        inTypes[0] = inMetadata->getType(status, 0);

        inNullOffsets[1] = inMetadata->getNullOffset(status, 1);
        inOffsets[1] = inMetadata->getOffset(status, 1);
        inTypes[1] = inMetadata->getType(status, 1);

        inNullOffsets[2] = inMetadata->getNullOffset(status, 2);
        inOffsets[2] = inMetadata->getOffset(status, 2);
        inTypes[2] = inMetadata->getType(status, 2);

        AutoRelease<IMessageMetadata> outMetadata(metadata->getOutputMetadata(status));
        outNullOffset = outMetadata->getNullOffset(status, 0);
        outOffset = outMetadata->getOffset(status, 0);
        outType = outMetadata->getType(status, 0);
    }

    FB_UDR_EXECUTE_FUNCTION
    {
        const char* in_buf = reinterpret_cast<const char*>(in);
        char* out_buf = reinterpret_cast<char*>(out);

        if (*(ISC_SHORT*)(in_buf + inNullOffsets[0]) == FB_TRUE ||
            *(ISC_SHORT*)(in_buf + inNullOffsets[1]) == FB_TRUE) {
            *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
            *(ISC_LONG*)(out_buf + outOffset) = -3;
            return;
        }

        int id = *(int*)(in_buf + inOffsets[0]);
        int action = *(int*)(in_buf + inOffsets[1]);

        std::span<const float> vec_span;
        std::vector<float> empty_vec;
        
        if (action == 1) {
            auto v = extract_vector_cached(status, context, in_buf, inNullOffsets[2], inOffsets[2], inTypes[2]);
            if (v.span.empty()) {
                *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
                *(ISC_LONG*)(out_buf + outOffset) = -4;
                return;
            }
            vec_span = v.span;
        }

        int ret = fbvector::send_sidecar_sync(id, action, vec_span);

        *(ISC_SHORT*)(out_buf + outNullOffset) = FB_FALSE;
        *(ISC_LONG*)(out_buf + outOffset) = ret;
    }

    unsigned inNullOffsets[3];
    unsigned inOffsets[3];
    unsigned inTypes[3];
    unsigned outNullOffset;
    unsigned outOffset;
    unsigned outType;
FB_UDR_END_FUNCTION

// Registrations of UDR entry points
FB_UDR_IMPLEMENT_ENTRY_POINT
