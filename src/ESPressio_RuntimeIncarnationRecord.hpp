#pragma once
#include <array>
#include <cstdint>
#include <ESPressio_DeviceIdentifier.hpp>

namespace ESPressio::Persistence {
/// <summary>Versioned device-bound durable high-water record. Zero is provisioned history, not a usable incarnation.</summary>
struct RuntimeIncarnationRecord {
    System::DeviceIdentifier Device{};
    std::uint32_t LastCommitted=0;
    /// <summary>Exact 40-byte little-endian representation, independent of native padding.</summary>
    using Bytes=std::array<std::uint8_t,40>;
private:
    static constexpr std::uint32_t Crc(const Bytes& bytes) noexcept {
        std::uint32_t crc=0xffffffffu;
        for (std::size_t i=0;i<36;++i) {
            crc^=bytes[i];
            for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
        }
        return ~crc;
    }
    static constexpr void Put(Bytes& bytes,std::size_t offset,std::uint64_t value,unsigned count) noexcept {
        for (unsigned i=0;i<count;++i) { bytes[offset+i]=static_cast<std::uint8_t>(value); value>>=8; }
    }
    static constexpr std::uint64_t Get(const Bytes& bytes,std::size_t offset,unsigned count) noexcept {
        std::uint64_t value=0;
        for (unsigned i=0;i<count;++i) value|=std::uint64_t(bytes[offset+i])<<(8*i);
        return value;
    }
public:
    /// <summary>Encodes magic, format/length, device, generation, high water and integrity. No native struct copy.</summary>
    constexpr Bytes Encode() const noexcept {
        Bytes bytes{};
        bytes[0]='E'; bytes[1]='P'; bytes[2]='R'; bytes[3]='I';
        Put(bytes,4,1,2); Put(bytes,6,bytes.size(),2);
        for (std::size_t i=0;i<16;++i) bytes[8+i]=Device.Bytes()[i];
        // Generation is H+1, including generation one for explicit provisioning.
        // Its 64-bit representation can encode the final uint32 high water without wrap.
        Put(bytes,24,std::uint64_t(LastCommitted)+1,8); Put(bytes,32,LastCommitted,4);
        Put(bytes,36,Crc(bytes),4); return bytes;
    }
    /// <summary>Validates the entire record before publication; malformed/version/integrity/generation failures leave output unchanged.</summary>
    static constexpr bool Decode(const Bytes& bytes,RuntimeIncarnationRecord& output) noexcept {
        if (bytes[0]!='E' || bytes[1]!='P' || bytes[2]!='R' || bytes[3]!='I' || Get(bytes,4,2)!=1 || Get(bytes,6,2)!=bytes.size()) return false;
        if (Get(bytes,36,4)!=Crc(bytes)) return false;
        auto high=static_cast<std::uint32_t>(Get(bytes,32,4));
        if (Get(bytes,24,8)!=std::uint64_t(high)+1) return false;
        System::DeviceIdentifier::Storage device{};
        for (std::size_t i=0;i<16;++i) device[i]=bytes[8+i];
        RuntimeIncarnationRecord record{System::DeviceIdentifier(device),high};
        if (!record.Device) return false;
        output=record; return true;
    }
};
}
