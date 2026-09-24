#include "mini_rpc/codec.h"

#include <cstring>
#include <netinet/in.h>
#include <stdexcept>

void encodeFrame(Buffer* out, uint16_t type, uint32_t seq, const std::string& body) {
    if (body.size() > kMaxBodyLen) {
          throw std::length_error("RPC body is too large");
    }
    char header[kHeaderLen];
    char* p = header;
    // 1. type    → 2 bytes big-endian 寫進 p，p 前進 2
    uint16_t networkType = htons(type);
    std::memcpy(p, &networkType, sizeof(networkType));
    p += sizeof(networkType);

    // 2. seq     → 4 bytes big-endian 寫進 p，p 前進 4
    uint32_t networkSeq = htonl(seq);
    std::memcpy(p, &networkSeq, sizeof(networkSeq));
    p += sizeof(networkSeq);

    // 3. bodyLen → 4 bytes big-endian（值是 body.size()）
    uint32_t networkBodyLen = htonl(static_cast<uint32_t>(body.size()));
    std::memcpy(p, &networkBodyLen, sizeof(networkBodyLen));

    // 4. out->append(header, kHeaderLen);
    out->append(header, kHeaderLen);
    // 5. out->append(body.data(), body.size());
    out->append(body.data(), body.size());
  }

DecodeResult decodeFrame(Buffer &buf, Frame *out){
    if(buf.readableBytes() < kHeaderLen){
        return DecodeResult::kNeedMore;
    }
    const char* p = buf.peek();

    uint16_t networkType;
    std::memcpy(&networkType, p, sizeof(networkType));
    p += sizeof(networkType);

    uint32_t networkSeq;
    std::memcpy(&networkSeq, p, sizeof(networkSeq));
    p += sizeof(networkSeq);

    uint32_t networkBodyLen;
    std::memcpy(&networkBodyLen, p, sizeof(networkBodyLen));

    const uint16_t type = ntohs(networkType);
    const uint32_t seq = ntohl(networkSeq);
    const uint32_t bodyLen = ntohl(networkBodyLen);

    if (bodyLen > kMaxBodyLen) {
        return DecodeResult::kError;
    }

    if (buf.readableBytes() < kHeaderLen + bodyLen) {
        return DecodeResult::kNeedMore;
    }

    out->type = type;
    out->seq = seq;
    out->body.assign(buf.peek() + kHeaderLen, bodyLen);

    buf.retrieve(kHeaderLen + bodyLen);
    return DecodeResult::kSuccess;
}
